#include "bms.h"
#include "protocol.h"
#include "serial.h"
#include <string.h>
#include <stdio.h>
#include <time.h>

/* 三条命令读出来后, 等响应超时的统一时长 (ms) */
#define BMS_RESPONSE_TIMEOUT_MS  1000

/* BMS 进深睡后首字节唤醒有延迟, 协议层自动重试 */
#define BMS_MAX_RETRIES           3
#define BMS_RETRY_DELAY_MS        200

/* 0x03 基本信息返回的最小数据段长度: 19 字节固定 + NTC 字段 */
#define BMS_BASIC_MIN_LEN         23

/* 调试: 设为 1 后会把原始发送/接收的字节流打印到 stderr */
extern int bms_debug;   /* 在 main.c 里定义 */

/* 工具宏: 解 2 字节大端无符号 16 位 */
#define BE16(p) (((uint16_t)(p)[0] << 8) | (p)[1])

/* 短睡, 单位 ms, 不依赖 librt */
static void sleep_ms(int ms) {
    struct timespec ts = { ms / 1000, (long)(ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
}

/*
 * 通用: 发送一个读命令, 同步等回一帧, 校验.
 *
 * 唤醒重试: 在 1 秒单帧超时后等 200ms 再重发, 最多 3 次重试 (= 4 次总尝试).
 * BMS_ERR_TIMEOUT 才重试; 拿到 SOF 后任何非 OK 结果都立刻返回 (BMS 回了).
 */
static bms_err_t bms_send_and_recv(int fd, uint8_t cmd,
                                   uint8_t *rx, size_t cap,
                                   size_t *rx_len,
                                   bms_response_t *resp) {
    uint8_t tx[8];
    size_t  tx_len = proto_pack_read(tx, sizeof tx, cmd);
    if (tx_len == 0) return BMS_ERR_BAD_FRAME;

    int total_attempts = BMS_MAX_RETRIES + 1;
    for (int attempt = 1; attempt <= total_attempts; attempt++) {
        if (attempt > 1) {
            /* 重试间隔让 BMS 完成唤醒 */
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
            /* 写失败也走重试路径 (下次重写) */
            continue;
        }

        if (serial_read_frame(fd, rx, cap, BMS_RESPONSE_TIMEOUT_MS, rx_len) < 0) {
            /* 超时 -> 重试 (若还有机会) */
            continue;
        }

        if (bms_debug) {
            fprintf(stderr, "[debug] RX (%zu 字节): ", *rx_len);
            for (size_t i = 0; i < *rx_len; i++) fprintf(stderr, "%02X ", rx[i]);
            fprintf(stderr, "\n");
            fflush(stderr);
        }

        /* BMS 回了 (SOF..EOF 已收全), 立即返回校验结果
         * BAD_FRAME/BAD_CHECKSUM/ERR_STATUS 都不再重试. */
        return proto_validate(rx, *rx_len, cmd, resp);
    }

    /* 4 次都没拿到 SOF: 真超时 */
    return BMS_ERR_TIMEOUT;
}

/* ============= 0x03 基本信息及状态 ============= */
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

    /* 0..1   总电压 (10mV) -> V */
    out->total_voltage_v = BE16(d + 0) * 0.01;

    /* 2..3   电流 (10mA) 有符号, 高位置 1 表示放电 */
    uint16_t cur_raw = BE16(d + 2);
    int32_t  cur_val;
    if (cur_raw & 0x8000)
        cur_val = (int32_t)cur_raw - 65536;
    else
        cur_val = (int32_t)cur_raw;
    out->current_a = cur_val * 0.01;

    /* 4..5   剩余容量 (10mAh) -> mAh */
    out->remaining_capacity_mah = (uint32_t)BE16(d + 4) * 10;

    /* 6..7   标称容量 (10mAh) -> mAh */
    out->nominal_capacity_mah = (uint32_t)BE16(d + 6) * 10;

    /* 8..9   循环次数 */
    out->cycle_count = BE16(d + 8);

    /* 10..11 生产日期 (压缩编码)
     *   day  = raw & 0x1F
     *   mon  = (raw >> 5) & 0x0F
     *   year = 2000 + (raw >> 9)
     */
    {
        uint16_t date_raw = BE16(d + 10);
        int day   = date_raw & 0x1F;
        int month = (date_raw >> 5) & 0x0F;
        int year  = 2000 + (date_raw >> 9);
        snprintf(out->prod_date, sizeof out->prod_date,
                 "%04d-%02d-%02d", year, month, day);
    }

    /* 12..13 均衡低 (1~16 串) */
    out->balance_low = BE16(d + 12);
    /* 14..15 均衡高 (17~32 串) */
    out->balance_high = BE16(d + 14);
    /* 16..17 保护状态 */
    out->protection_bits = BE16(d + 16);

    /* 18   软件版本 (0x10 -> 1.0, 高 nibble 主, 低 nibble 次) */
    out->sw_version_major = (d[18] >> 4) & 0x0F;
    out->sw_version_minor = d[18] & 0x0F;

    /* 19 RSOC 百分比 */
    out->rsoc_pct = d[19];
    /* 20 FET bit0=CHG bit1=DIS */
    out->fet_state = d[20];
    /* 21 电池串数 */
    out->cell_count = d[21];
    /* 22 NTC 个数 N */
    out->ntc_count = d[22];

    if (out->ntc_count > 16) out->ntc_count = 16;

    /* 23 .. 23+2N-1  NTC 温度 (绝对温度编码) */
    size_t need = 23 + (size_t)out->ntc_count * 2;
    if (resp.data_len < need) return BMS_ERR_BAD_FRAME;

    for (int i = 0; i < out->ntc_count; i++) {
        uint16_t t_raw = BE16(d + 23 + i * 2);
        out->ntc_temp_c[i] = ((int)t_raw - 2731) / 10.0;
    }

    return BMS_OK;
}

/* ============= 0x04 单体电压 ============= */
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
    if (out->cell_count > 48) out->cell_count = 48;

    for (uint16_t i = 0; i < out->cell_count; i++) {
        out->cell_mv[i] = BE16(resp.data + i * 2);    /* 单位 mV */
    }
    return BMS_OK;
}

/* ============= 0x05 硬件版本号 ============= */
bms_err_t bms_read_hwver(int fd, bms_hw_version_t *out) {
    if (!out) return BMS_ERR_BAD_FRAME;
    memset(out, 0, sizeof *out);

    uint8_t rx[256];
    size_t  rx_len = 0;
    bms_response_t resp;
    bms_err_t e = bms_send_and_recv(fd, 0x05, rx, sizeof rx, &rx_len, &resp);
    if (e != BMS_OK) return e;

    /* ASCII 字符串, 协议最长 31 字节 */
    size_t n = resp.data_len;
    if (n > sizeof out->hw_version - 1) n = sizeof out->hw_version - 1;
    if (n > 0) memcpy(out->hw_version, resp.data, n);
    out->hw_version[n] = '\0';
    return BMS_OK;
}
