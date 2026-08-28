/*
 * serial.c — 串口 (RS485/RS232/TTL) 底层收发驱动层
 *            Serial Port (RS485/RS232/TTL) Low-Level Communication Driver
 *
 * 流程 / Lifecycle:
 *   打开设备 / Open -> 配置 8-N-1 raw 模式与 RS485 半双工方向切换 / Config 8-N-1 raw & RS485 -> 帧级读写 / Frame I/O.
 *
 * RS485 半双工方向控制要点 / RS485 Half-Duplex Direction Control (Three-Tier Fallback):
 *   1. 内核级自动控制 / Kernel RS485 control: 在 serial_config 中调用 ioctl(TIOCSRS485) 启用 RTS_ON_SEND;
 *   2. 驱动手动切换 / Manual modem line toggling: write 前通过 ioctl(TIOCMBIS) 拉高 RTS/DTR, 写完后通过 TIOCMBIC 拉低;
 *   3. 延时切换排空 / Drain & turnaround delay: write 完毕后执行 tcdrain() 排空 FIFO 并等待 1ms, 让物理收发芯片切回 RX.
 *
 * 超时与非阻塞控制 / Timeout & Non-Blocking Design:
 *   文件描述符设为 O_NONBLOCK (VMIN=0, VTIME=0), 所有读等待统一由 select() 精确控制毫秒级超时.
 */

#include "serial.h"
#include "protocol.h"   /* BMS_SOF / BMS_EOF */
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <errno.h>
#include <string.h>
#include <sys/select.h>
#include <sys/ioctl.h>
#include <time.h>
#include <linux/serial.h>   /* struct serial_rs485, SER_RS485_RTS_ON_SEND */

/*
 * @brief 打开串口设备文件 (设置 O_NONBLOCK 非阻塞模式)
 *        Open serial port device file in non-blocking mode
 * @param path 设备路径 (如 /dev/ttyUSB0) / Device path (e.g. /dev/ttyUSB0)
 * @return 打开的文件描述符 fd, 失败返回 -1 / File descriptor fd, or -1 on failure
 */
int serial_open(const char *path) {
    if (!path) return -1;
    int fd = open(path, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) return -1;
    return fd;
}

/*
 * @brief 将串口配置为 8-N-1 raw 模式并尝试开启 RS485 硬件流向控制
 *        Configure serial fd to 8-N-1 raw mode and enable RS485 direction control
 *
 * 配置要点 / Configuration Details:
 * - ~PARENB    : 无奇偶校验位 / No parity
 * - ~CSTOPB    : 1 位停止位 / 1 stop bit
 * - CS8        : 8 位数据位 / 8 data bits
 * - CREAD      : 启用接收器 / Enable receiver
 * - CLOCAL     : 忽略调制解调器状态线 / Ignore modem control lines
 * - 关 IXON/IXOFF/IXANY : 禁用软件流控 / Disable software XON/XOFF flow control
 * - 关 CRTSCTS : 禁用硬件 CTS/RTS 流控 / Disable hardware CTS/RTS handshake
 * - cfmakeraw  : 禁用换行转换、回显等特殊字符处理 / Disable canonical mode and echoes
 * - VMIN=0, VTIME=0 : read() 立即返回, 超时交由 select() 控制 / Timeout controlled by select()
 */
int serial_config(int fd, speed_t baud) {
    struct termios tio;
    if (tcgetattr(fd, &tio) < 0) return -1;

    cfmakeraw(&tio);

    /* 设置波特率 (如 B9600) / Set baud rate (e.g. B9600) */
    if (cfsetspeed(&tio, baud) < 0) return -1;

    /* 配置 8-N-1 格式 + 启用接收 + 本地连接 / Configure 8-N-1 + enable RX + local line */
    tio.c_cflag &= ~(PARENB | PARODD);
    tio.c_cflag &= ~CSTOPB;
    tio.c_cflag &= ~CSIZE;
    tio.c_cflag |=  CS8 | CREAD | CLOCAL;

    /* 禁用硬件流控 / Disable hardware flow control */
    tio.c_cflag &= ~CRTSCTS;
    tio.c_iflag &= ~(IXON | IXOFF | IXANY);

    /* 禁用特殊字符处理与回显 / Disable special input processing & echoing */
    tio.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | ICRNL | IXANY);
    tio.c_oflag &= ~OPOST;
    tio.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);

    /* ----------- RS485 收发方向控制 / RS485 Direction Control -----------
     * 大多数 USB 转 RS485 适配器 (CH340/FT232 等) 需要 RTS 或 DTR 来切换发送使能 (DE/RE).
     * 优先尝试 Linux 内核级 TIOCSRS485 自动控制, 在 write 期间由驱动自动拉高 RTS.
     * Most USB-to-RS485 adapters use RTS/DTR for DE/RE transceiver direction switching.
     * Try kernel-level TIOCSRS485 first, fallback to manual toggling in serial_write.
     */
    struct serial_rs485 rs485cfg;
    if (ioctl(fd, TIOCGRS485, &rs485cfg) == 0) {
        rs485cfg.flags |= SER_RS485_ENABLED;
        rs485cfg.flags |= SER_RS485_RTS_ON_SEND;       /* 发送期间 RTS 拉高 / RTS high when sending */
        rs485cfg.flags &= ~SER_RS485_RTS_AFTER_SEND;   /* 发送完毕 RTS 拉低 / RTS low after sending */
        rs485cfg.delay_rts_before_send = 0;
        rs485cfg.delay_rts_after_send  = 0;            /* 发送后切换延时 (ms) / Post-send delay */
        if (ioctl(fd, TIOCSRS485, &rs485cfg) == 0) {
            /* 内核 RS485 自动控制启用成功 / Kernel RS485 mode enabled */
        }
    }

    /* read() 非阻塞控制 / Non-blocking read parameters */
    tio.c_cc[VMIN]  = 0;
    tio.c_cc[VTIME] = 0;

    if (tcsetattr(fd, TCSANOW, &tio) < 0) return -1;

    /* 清空串口残留的输入/输出缓冲区 / Flush pending input/output buffers */
    tcflush(fd, TCIOFLUSH);

    /* 保持文件描述符处于非阻塞状态 / Ensure non-blocking flag */
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return -1;
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) return -1;

    return 0;
}

/*
 * @brief 阻塞写完整 n 字节数据 (处理中断与短写, 并管理 RS485 收发切换)
 *        Blocking write of n bytes (handles EINTR/short writes and RS485 line toggling)
 *
 * 发送流程 / Transmission Steps:
 *   1. 手动拉高 RTS/DTR 控制引脚 (兜底适配各种硬件转换器) / Pull RTS/DTR high
 *   2. 循环 write 写入数据, 遇 EINTR 自动重试 / Loop write until all n bytes written
 *   3. tcdrain() 等待物理 FIFO 发送完全排空 / Wait for hardware FIFO to drain
 *   4. 休眠 ~1ms 给半双工收发芯片留足切回 RX 模式的时间 / Sleep 1ms for turnaround time
 *   5. 手动拉低 RTS/DTR 引脚回到接收状态 / Pull RTS/DTR low to return to RX mode
 */
int serial_write(int fd, const uint8_t *p, size_t n) {
    /* 手动拉高 RTS + DTR 引脚 / Manually set RTS & DTR lines high */
    int lines = TIOCM_RTS | TIOCM_DTR;
    ioctl(fd, TIOCMBIS, &lines);    /* 驱动不支持时忽略 / Ignore if unsupported */

    size_t written = 0;
    while (written < n) {
        ssize_t r = write(fd, p + written, n - written);
        if (r < 0) {
            if (errno == EINTR) continue;
            /* 出错时恢复 RTS/DTR 接收模式 / Restore lines on error */
            ioctl(fd, TIOCMBIC, &lines);
            return -1;
        }
        if (r == 0) {
            ioctl(fd, TIOCMBIC, &lines);
            return -1;
        }
        written += (size_t)r;
    }

    /* 等待串口物理输出缓冲排空 / Wait until all output written to fd has been transmitted */
    tcdrain(fd);

    /* 为 RS485 芯片方向切换预留 1ms / 1ms delay for RS485 RX turnaround */
    struct timespec ts = { 0, 1000 * 1000 };   /* 1ms */
    nanosleep(&ts, NULL);

    /* 拉低 RTS/DTR 切回接收模式 / Pull RTS/DTR low back to RX mode */
    ioctl(fd, TIOCMBIC, &lines);

    return 0;
}

/*
 * @brief 从串口读取一个完整的数据帧 (SOF 0xDD 开始, EOF 0x77 结束)
 *        Read a complete frame from serial (starts at SOF 0xDD, ends at EOF 0x77)
 *
 * 状态机与超时处理 / State Machine & Timeout:
 *   - 在检测到起始字节 SOF (0xDD) 之前, 所有总线噪声杂散字节全部自动丢弃, 且不计入超时时间;
 *     Noise bytes before SOF are dropped and not counted toward timeout.
 *   - 命中 0xDD (SOF) 后开始累加接收字节并启动毫秒级精确计时 (总耗时超过 timeout_ms 即放弃);
 *     Timer starts upon seeing SOF (0xDD); aborts if elapsed time exceeds timeout_ms.
 *   - 收到 0x77 (EOF) 时表示整帧接收完成, 写入 out_len 并返回 0.
 *     Returns 0 when terminating delimiter EOF (0x77) is received.
 *
 * @param fd 串口描述符 / Serial fd
 * @param out 输出缓冲区 / Frame buffer
 * @param cap 输出缓冲区容量 / Max buffer capacity
 * @param timeout_ms 超时毫秒数 / Timeout in ms
 * @param out_len 实际读取的整帧长度 / Pointer to store read frame length
 * @return 0 成功, -1 超时或读取出错 / 0 on success, -1 on timeout/error
 */
int serial_read_frame(int fd, uint8_t *out, size_t cap,
                      int timeout_ms, size_t *out_len) {

    if (out && out_len) *out_len = 0;

    struct timespec t0;
    clock_gettime(CLOCK_MONOTONIC, &t0);

    int sofd = 0;
    size_t total = 0;

    while (total < cap) {
        /* 检查总耗时是否已超过 timeout_ms / Check total elapsed time */
        if (sofd) {
            struct timespec now;
            clock_gettime(CLOCK_MONOTONIC, &now);
            long elapsed_ms = (long)(now.tv_sec - t0.tv_sec) * 1000
                            + (long)(now.tv_nsec - t0.tv_nsec) / 1000000;
            if (elapsed_ms >= timeout_ms) return -1;
        }

        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(fd, &rfds);

        int remaining = timeout_ms;
        if (sofd) {
            struct timespec now;
            clock_gettime(CLOCK_MONOTONIC, &now);
            long elapsed_ms = (long)(now.tv_sec - t0.tv_sec) * 1000
                            + (long)(now.tv_nsec - t0.tv_nsec) / 1000000;
            remaining = (int)(timeout_ms - elapsed_ms);
            if (remaining <= 0) return -1;
        } else {
            remaining = timeout_ms;
        }

        struct timeval tv;
        tv.tv_sec  = remaining / 1000;
        tv.tv_usec = (remaining % 1000) * 1000;

        int r = select(fd + 1, &rfds, NULL, NULL, &tv);
        if (r < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (r == 0) return -1; /* 超时 / Timeout */

        uint8_t b;
        ssize_t n = read(fd, &b, 1);
        if (n < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) continue;
            return -1;
        }
        if (n == 0) return -1; /* 连接中断或 EOF / EOF */

        /* 尚未找到 SOF: 丢弃噪声字节; 命中 0xDD 才开始收帧并重置计时器 /
         * Drop noise before SOF; start recording and reset timer upon seeing 0xDD */
        if (!sofd) {
            if (b != BMS_SOF) continue;
            sofd = 1;
            total = 0;
            /* 重置起始时间, 从捕获到 SOF 起计算帧接收超时 / Reset timer from SOF */
            clock_gettime(CLOCK_MONOTONIC, &t0);
        }

        out[total++] = b;

        /* 帧长上限保护: 防止畸变长帧导致溢出 / Max frame length guard */
        if (total > 8 + 255 + 2) return -1;

        if (b == BMS_EOF) {
            /* 帧完整闭合: 从 SOF 到 EOF 已全部收齐 / Complete frame received */
            if (out_len) *out_len = total;
            return 0;
        }
    }
    return -1;
}

/*
 * @brief 关闭串口设备描述符
 *        Close serial device file descriptor
 */
void serial_close(int fd) {
    if (fd < 0) return;
    close(fd);
}
