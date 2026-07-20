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
 * 打开串口设备. 设成 O_NONBLOCK 留作后续 select 用.
 */
int serial_open(const char *path) {
    if (!path) return -1;
    int fd = open(path, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) return -1;
    return fd;
}

/*
 * 配置 fd 为 8-N-1 raw, 速率由 baud 指定 (B9600 等).
 * - ~PARENB    : 无校验
 * - ~CSTOPB    : 1 停止位
 * - CS8        : 8 数据位
 * - CREAD      : 启用接收
 * - CLOCAL     : 不管 modem 控制线
 * - 关 IXON/IXOFF/IXANY : 无软件流控
 * - 关 CRTSCTS         : 无硬件流控 (485 不需要)
 * - cfmakeraw          : 关闭所有特殊字符处理
 * - VMIN=0, VTIME=0    : 由我们用 select() 控制超时
 */
int serial_config(int fd, speed_t baud) {
    struct termios tio;
    if (tcgetattr(fd, &tio) < 0) return -1;

    cfmakeraw(&tio);

    /* 速率 */
    if (cfsetspeed(&tio, baud) < 0) return -1;

    /* 8-N-1 + 接收 + 本地 */
    tio.c_cflag &= ~(PARENB | PARODD);
    tio.c_cflag &= ~CSTOPB;
    tio.c_cflag &= ~CSIZE;
    tio.c_cflag |=  CS8 | CREAD | CLOCAL;

    /* 不需要硬件流控 */
    tio.c_cflag &= ~CRTSCTS;
    tio.c_iflag &= ~(IXON | IXOFF | IXANY);

    /* 不需要特殊字符处理, raw 模式下基本无所谓, 但保险起见 */
    tio.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | ICRNL | IXANY);
    tio.c_oflag &= ~OPOST;
    tio.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);

    /* ----------- RS485 收发方向控制 -----------
     * 大多数 USB-485 适配器 (CH340/FT232 等) 用 RTS 或 DTR 来切收发.
     * 不拉高, 适配器压根不进发送模式.
     *
     * 先试内核级 TIOCSRS485 + RTS_ON_SEND: 由驱动在 write 期间自动拉高 RTS.
     * 老驱动/不识别 TIOCSRS485 的芯片也能在 serial_write 里手动拉高.
     */
    struct serial_rs485 rs485cfg;
    if (ioctl(fd, TIOCGRS485, &rs485cfg) == 0) {
        rs485cfg.flags |= SER_RS485_ENABLED;
        rs485cfg.flags |= SER_RS485_RTS_ON_SEND;       /* 发送时 RTS 拉高 */
        rs485cfg.flags &= ~SER_RS485_RTS_AFTER_SEND;   /* 发送完 RTS 拉低 */
        rs485cfg.delay_rts_before_send = 0;
        rs485cfg.delay_rts_after_send  = 0;            /* 拉低延迟 (ms) */
        if (ioctl(fd, TIOCSRS485, &rs485cfg) == 0) {
            /* 启用内核自动控制成功, 手动 toggle 留作 fallback */
            // fprintf(stderr, "[serial] 内核 RS485 自动控制已启用\n");
        }
    }

    /* read() 直接返回 - 由 select() 控制 */
    tio.c_cc[VMIN]  = 0;
    tio.c_cc[VTIME] = 0;

    if (tcsetattr(fd, TCSANOW, &tio) < 0) return -1;

    /* 把残留的输入/输出清掉 */
    tcflush(fd, TCIOFLUSH);

    /* 保持非阻塞, select() 控超时 */
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return -1;
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) return -1;

    return 0;
}

/*
 * 阻塞写, 直到写完 n 字节. EINTR 重试.
 * - 写前手动拉高 RTS/DTR (内核 RS485 自动控制不识别时的兜底)
 * - 写完后稍微等 1ms, 让 485 收发器从 TX 切回 RX
 */
int serial_write(int fd, const uint8_t *p, size_t n) {
    /* 手动拉高 RTS + DTR —— 适配内核 RS485 之外的兜底方案 */
    int lines = TIOCM_RTS | TIOCM_DTR;
    ioctl(fd, TIOCMBIS, &lines);    /* 失败忽略: 不是所有驱动都允许 */

    size_t written = 0;
    while (written < n) {
        ssize_t r = write(fd, p + written, n - written);
        if (r < 0) {
            if (errno == EINTR) continue;
            /* 失败时也要把 RTS/DTR 拉回去 */
            ioctl(fd, TIOCMBIC, &lines);
            return -1;
        }
        if (r == 0) {
            ioctl(fd, TIOCMBIC, &lines);
            return -1;
        }
        written += (size_t)r;
    }

    /* 等 FIFO 排空 */
    tcdrain(fd);

    /* 给 485 收发切换留时间, ~1ms */
    struct timespec ts = { 0, 1000 * 1000 };   /* 1ms */
    nanosleep(&ts, NULL);

    /* 拉低 RTS/DTR 切回接收模式 */
    ioctl(fd, TIOCMBIC, &lines);

    return 0;
}

/*
 * 读一帧: 字节流 -> 找 0xDD (SOF) 起, 收到 0x77 (EOF) 截断.
 * 整体超时 timeout_ms 毫秒.
 *
 * 算法:
 *   - 还没找到 SOF 之前收到非 0xDD 字节: 当垃圾丢掉, 不计超时延长
 *   - 找到 SOF 之后开始累计, 总耗时超过 timeout_ms 即放弃
 *
 * 成功返回 0 并把帧 copy 到 out (out_len 字节), 失败 -1.
 */
int serial_read_frame(int fd, uint8_t *out, size_t cap,
                      int timeout_ms, size_t *out_len) {
    if (out && out_len) *out_len = 0;

    struct timespec t0;
    clock_gettime(CLOCK_MONOTONIC, &t0);

    int sofd = 0;
    size_t total = 0;

    while (total < cap) {
        /* 检查总耗时 */
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
        if (r == 0) return -1; /* timeout */

        uint8_t b;
        ssize_t n = read(fd, &b, 1);
        if (n < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) continue;
            return -1;
        }
        if (n == 0) return -1; /* EOF */

        if (!sofd) {
            if (b != BMS_SOF) continue;
            sofd = 1;
            total = 0;
            /* 重置起始时间, 找到 SOF 才开始算超时 */
            clock_gettime(CLOCK_MONOTONIC, &t0);
        }

        out[total++] = b;

        /* 帧长上限保护: SOF/CMD/STATUS/LEN + 255 数据 + 2CS + EOF = 261 */
        if (total > 8 + 255 + 2) return -1;

        if (b == BMS_EOF) {
            if (out_len) *out_len = total;
            return 0;
        }
    }
    return -1;
}

void serial_close(int fd) {
    if (fd < 0) return;
    close(fd);
}
