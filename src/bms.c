/*
 * bms.c — BMS 读命令的业务层封装 (0x03 / 0x04 / 0x05)
 *         BMS Read Commands Business Layer Implementation (0x03 / 0x04 / 0x05)
 *
 * 数据流 / Data Flow:
 *   组帧 / Assembly (protocol.c) -> 串口收发 / TX/RX (serial.c) -> 帧校验 / Validation (protocol.c)
 *   -> 按协议 V4 布局解码填充结构体 / Field unpacking into bms.h structs.
 *
 * 设计要点 / Key Design Principles:
 *   - 三条命令共用 bms_send_and_recv(): 发送 + 自动重试唤醒 + 帧校验一条龙;
 *     Unified bms_send_and_recv(): packet transmission, auto-retry on wake-up delay, and frame validation.
 *   - 唤醒机制 / Wake-up handling: BMS 深睡时首字节可能被唤醒过程吞噬, 采用超时自动整帧重发兜底;
 *     When BMS is in deep sleep, initial bytes may be missed; automatic frame retransmission handles wake-up.
 *   - 纯内存解码 / Safe memory operations: 内存越界与坏帧保护, 仅返回错误码, 不 panic/exit.
 *     Safe bounded parsing returning BMS_ERR_* without abnormal process termination.
 */

#include "bms.h"
#include "protocol.h"
#include "serial.h"
#include <string.h>
#include <stdio.h>
#include <time.h>

/* 三条读命令发送后等待响应帧的单次超时时长 (毫秒)
 * Single frame response timeout in milliseconds */
#define BMS_RESPONSE_TIMEOUT_MS  1000

/* BMS 进深睡后首字节唤醒有延迟, 协议层最大自动重试次数及重试间隔 (ms)
 * Max retry attempts and delay (ms) between retries to wake BMS up from deep sleep */
#define BMS_MAX_RETRIES           3
#define BMS_RETRY_DELAY_MS        200

/* 0x03 基本信息响应返回的最小数据段长度: 23 字节固定头部 + NTC 字段
 * Minimum expected payload length for 0x03 basic info: 23-byte fixed header + NTC data */
#define BMS_BASIC_MIN_LEN         23

/* 调试开关: 设为 1 后会把原始 TX/RX 字节流打印到 stderr
 * Debug flag: when 1, prints raw hex byte streams of TX and RX to stderr */
extern int bms_debug;   /* 在 main.c 里定义 / Defined in main.c */

/* 工具宏: 解码 2 字节大端无符号 16 位整数
 * Utility macro: decode 2-byte Big-Endian uint16_t */
#define BE16(p) (((uint16_t)(p)[0] << 8) | (p)[1])

/* 短暂休眠 (毫秒), 基于 POSIX nanosleep 实现, 不依赖 librt
 * Short sleep helper in milliseconds using POSIX nanosleep */
static void sleep_ms(int ms) {
    struct timespec ts = { ms / 1000, (long)(ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
}

/*
 * @brief 通用底层收发函数: 打包读命令 -> 串口发送 -> 同步等待接收 -> 协议校验
 *        Generic send & receive helper: pack read cmd -> send -> sync wait frame -> validate
 *
 * 唤醒与重试策略 / Wake-up and Retry Strategy:
 *   若发生 BMS_ERR_TIMEOUT (无应答), 在等待 BMS_RETRY_DELAY_MS 后重新发帧;
 *   最多重试 BMS_MAX_RETRIES 次 (总共尝试 1 + 3 = 4 次);
 *   一旦收到完整帧 (检测到 SOF..EOF), 无论校验成功与否立即返回, 不做盲目重试.
 *   On BMS_ERR_TIMEOUT, waits delay and retries up to BMS_MAX_RETRIES times.
 *   Once a complete frame is received, results (OK/ERR) are returned immediately without retrying.
 */
static bms_err_t bms_send_and_recv(int fd, uint8_t cmd,
                                   uint8_t *rx, size_t cap,
                                   size_t *rx_len,
                                   bms_response_t *resp) {
    /* 组装读命令帧: 7 字节 DD A5 CMD 00 CS_H CS_L 77 / Assemble 7-byte read command frame */
    uint8_t tx[8];
    size_t  tx_len = proto_pack_read(tx, sizeof tx, cmd);
    if (tx_len == 0) return BMS_ERR_BAD_FRAME;   /* cap 不足, 实际不会发生 / Capacity guard */

    int total_attempts = BMS_MAX_RETRIES + 1;    /* 1 次首发 + 3 次重试 = 4 次尝试 / Total 4 attempts */
    for (int attempt = 1; attempt <= total_attempts; attempt++) {

        if (attempt > 1) {
            /* 重试间隔让 BMS 硬件完成唤醒准备 / Retry delay allows BMS hardware wake-up */
            sleep_ms(BMS_RETRY_DELAY_MS);
            if (bms_debug) {
                fprintf(stderr, "[debug] 第 %d 次重试 cmd=0x%02X\n",
                        attempt - 1, cmd);
                fflush(stderr);
            }
        }

        if (bms_debug) {
            fprintf(stderr, "[debug] TX: ");
            for (size_t i = 0; i < tx_len; i++) fprintf(stderr, "%02X ", tx[i]);
            fprintf(stderr, "\n");
            fflush(stderr);
        }

        if (serial_write(fd, tx, tx_len) < 0) {
            /* 串口写入失败也进入下一次重试 / Serial write failure, continue to next retry */
            continue;
        }

        if (serial_read_frame(fd, rx, cap, BMS_RESPONSE_TIMEOUT_MS, rx_len) < 0) {
            /* 接收超时 -> 进入重试 (若还有剩余次数) / Frame receive timeout -> retry */
            continue;
        }

        if (bms_debug) {
            fprintf(stderr, "[debug] RX (%zu 字节): ", *rx_len);
            for (size_t i = 0; i < *rx_len; i++) fprintf(stderr, "%02X ", rx[i]);
            fprintf(stderr, "\n");
            fflush(stderr);
        }

        /* BMS 已应答且整帧接收完毕, 立即执行校验并返回结果 /
         * BMS responded with complete frame; validate and return immediately. */
        return proto_validate(rx, *rx_len, cmd, resp);
    }

    /* 尝试耗尽仍无响应: 判定为超时 / All attempts exhausted: timeout */
    return BMS_ERR_TIMEOUT;
}

/* ============= 0x03 基本信息及状态 / Basic Info & Status (0x03) ============= */
/*
 * @brief 解码 0x03 命令响应数据段并填充 bms_basic_info_t 结构体
 *        Decode payload of 0x03 response into bms_basic_info_t struct
 *
 * 字段偏移布局 (per Protocol V4 Doc p.1-2) / Field offset layout:
 *   0..1   总电压 (单位 10mV) -> 换算为 V / Total voltage (unit 10mV -> V)
 *   2..3   电流 (单位 10mA, 有符号数, 最高位 1 为放电) -> A / Current (unit 10mA, signed, MSB=1 discharge -> A)
 *   4..5   剩余容量 (单位 10mAh) -> mAh / Remaining capacity (unit 10mAh -> mAh)
 *   6..7   标称容量 (单位 10mAh) -> mAh / Nominal capacity (unit 10mAh -> mAh)
 *   8..9   循环次数 / Cycle count
 *   10..11 生产日期 (压缩编码) / Production date (compressed bit fields)
 *   12..13 均衡低位 (1~16 串) / Balance low word (cells 1-16)
 *   14..15 均衡高位 (17~32 串) / Balance high word (cells 17-32)
 *   16..17 保护状态位图 / Protection status bitmap
 *   18     软件版本 (高 4 位主版本, 低 4 位次版本) / Software version (upper nibble major, lower nibble minor)
 *   19     RSOC 剩余电量百分比 / RSOC percentage (0-100%)
 *   20     MOSFET 状态 (bit0=充, bit1=放) / FET switch state (bit0=CHG, bit1=DIS)
 *   21     电池串数 / Cell count
 *   22     NTC 温度探头数量 N / NTC sensor count N
 *   23..   各 NTC 绝对温度值 (开尔文 0.1K, 换算为 °C: (raw - 2731) / 10.0) /
 *          NTC temperature values in 0.1K (converted to °C: (raw - 2731)/10.0)
 */
bms_err_t bms_read_basic(int fd, bms_basic_info_t *out) {
    if (!out) return BMS_ERR_BAD_FRAME;
    memset(out, 0, sizeof *out);

    uint8_t rx[256];
    size_t  rx_len = 0;
    bms_response_t resp;
    bms_err_t e = bms_send_and_recv(fd, 0x03, rx, sizeof rx, &rx_len, &resp);
    if (e != BMS_OK) return e;
    if (resp.data_len < BMS_BASIC_MIN_LEN) return BMS_ERR_BAD_FRAME;

    const uint8_t *d = resp.data;

    /* 0..1 总电压 (10mV) -> V / Total pack voltage (10mV) -> V */
    out->total_voltage_v = BE16(d + 0) * 0.01;

    /* 2..3 电流 (10mA) 有符号, 补码解码 / Current (10mA), signed two's complement */
    uint16_t cur_raw = BE16(d + 2);
    int32_t  cur_val;
    if (cur_raw & 0x8000)
        cur_val = (int32_t)cur_raw - 65536;
    else
        cur_val = (int32_t)cur_raw;
    out->current_a = cur_val * 0.01;

    /* 4..5 剩余容量 (10mAh) -> mAh / Remaining capacity (10mAh) -> mAh */
    out->remaining_capacity_mah = (uint32_t)BE16(d + 4) * 10;

    /* 6..7 标称容量 (10mAh) -> mAh / Nominal capacity (10mAh) -> mAh */
    out->nominal_capacity_mah = (uint32_t)BE16(d + 6) * 10;

    /* 8..9 循环次数 / Cycle count */
    out->cycle_count = BE16(d + 8);

    /* 10..11 生产日期 (位压缩编码) / Production date (bitfield compressed):
     *   day  = raw & 0x1F (5 bits)
     *   mon  = (raw >> 5) & 0x0F (4 bits)
     *   year = 2000 + (raw >> 9) (7 bits)
     */
    {
        uint16_t date_raw = BE16(d + 10);
        int day   = date_raw & 0x1F;
        int month = (date_raw >> 5) & 0x0F;
        int year  = 2000 + (date_raw >> 9);
        snprintf(out->prod_date, sizeof out->prod_date,
                 "%04d-%02d-%02d", year, month, day);
    }

    /* 12..13 均衡状态低 16 串 / Balance low (cells 1-16) */
    out->balance_low = BE16(d + 12);
    /* 14..15 均衡状态高 16 串 / Balance high (cells 17-32) */
    out->balance_high = BE16(d + 14);
    /* 16..17 保护状态位图 / Protection status bitmap */
    out->protection_bits = BE16(d + 16);

    /* 18 软件版本号 (0x10 -> 1.0) / Software version (upper nibble major, lower nibble minor) */
    out->sw_version_major = (d[18] >> 4) & 0x0F;
    out->sw_version_minor = d[18] & 0x0F;

    /* 19 RSOC 剩余百分比 / RSOC percentage */
    out->rsoc_pct = d[19];
    /* 20 FET 状态: bit0=充, bit1=放 / FET switch state: bit0=CHG, bit1=DIS */
    out->fet_state = d[20];
    /* 21 电池串数 / Number of battery cells */
    out->cell_count = d[21];
    /* 22 NTC 温度传感器个数 / Number of NTC sensors */
    out->ntc_count = d[22];

    if (out->ntc_count > 16) out->ntc_count = 16;   /* 限制数组上限防溢出 / Clamp to array bound */

    /* 23 .. 23+2N-1 NTC 温度值 (开氏度 0.1K 编码, 273.15K = 0°C) /
     * NTC temperature encoding (0.1K absolute temp, (raw - 2731) / 10.0 = °C) */
    size_t need = 23 + (size_t)out->ntc_count * 2;
    if (resp.data_len < need) return BMS_ERR_BAD_FRAME;

    for (int i = 0; i < out->ntc_count; i++) {
        uint16_t t_raw = BE16(d + 23 + i * 2);
        out->ntc_temp_c[i] = ((int)t_raw - 2731) / 10.0;
    }

    return BMS_OK;
}

/* ============= 0x04 单体电压 / Cell Voltages (0x04) ============= */
/*
 * @brief 解码 0x04 单体电压响应
 *        Decode 0x04 cell voltages response
 *
 * DATA 载荷段为一系列 2 字节大端整数 (单位 mV), 串数 = data_len / 2.
 * Payload consists of 2-byte Big-Endian mV values, cell_count = data_len / 2.
 */
bms_err_t bms_read_cells(int fd, bms_cell_voltages_t *out) {
    if (!out) return BMS_ERR_BAD_FRAME;
    memset(out, 0, sizeof *out);

    uint8_t rx[256];
    size_t  rx_len = 0;
    bms_response_t resp;
    bms_err_t e = bms_send_and_recv(fd, 0x04, rx, sizeof rx, &rx_len, &resp);
    if (e != BMS_OK) return e;
    if (resp.data_len == 0 || (resp.data_len & 1))
        return BMS_ERR_BAD_FRAME;

    out->cell_count = resp.data_len / 2;
    if (out->cell_count > 48) out->cell_count = 48;   /* 限制数组容量 / Bound to array capacity */

    for (uint16_t i = 0; i < out->cell_count; i++) {
        out->cell_mv[i] = BE16(resp.data + i * 2);    /* 单位 mV / Voltage in mV */
    }
    return BMS_OK;
}

/* ============= 0x05 硬件版本号 / Hardware Version (0x05) ============= */
/*
 * @brief 解码 0x05 硬件版本响应 (ASCII 字符串)
 *        Decode 0x05 hardware version response (ASCII string)
 */
bms_err_t bms_read_hwver(int fd, bms_hw_version_t *out) {
    if (!out) return BMS_ERR_BAD_FRAME;
    memset(out, 0, sizeof *out);

    uint8_t rx[256];
    size_t  rx_len = 0;
    bms_response_t resp;
    bms_err_t e = bms_send_and_recv(fd, 0x05, rx, sizeof rx, &rx_len, &resp);
    if (e != BMS_OK) return e;

    /* ASCII 字符串拷贝, 最长 31 字节并确保以 '\0' 结尾 / Copy ASCII string (max 31 bytes, null-terminated) */
    size_t n = resp.data_len;
    if (n > sizeof out->hw_version - 1) n = sizeof out->hw_version - 1;
    if (n > 0) memcpy(out->hw_version, resp.data, n);
    out->hw_version[n] = '\0';
    return BMS_OK;
}
