#ifndef BMS_H
#define BMS_H

#include <stdint.h>
#include "protocol.h"   /* bms_err_t */

/* 调试模式: 1=打印原始收发字节, 0=静默. 在 main.c 里定义并可由 --debug 设置. */
extern int bms_debug;

/* 基本信息 (0x03 命令) 解码结果 */
typedef struct {
    double   total_voltage_v;        /* 总电压 (V) */
    double   current_a;              /* 电流 (A), 负=放电 */
    uint32_t remaining_capacity_mah;
    uint32_t nominal_capacity_mah;
    uint16_t cycle_count;
    char     prod_date[16];          /* "YYYY-MM-DD" */
    uint16_t balance_low;            /* bit0..bit15 对应 1..16 串均衡 */
    uint16_t balance_high;           /* bit0..bit15 对应 17..32 串均衡 */
    uint16_t protection_bits;        /* 见 PDF 第 2 页 bit 表 */
    uint8_t  sw_version_major;
    uint8_t  sw_version_minor;
    uint8_t  rsoc_pct;               /* 剩余容量百分比 */
    uint8_t  fet_state;              /* bit0=CHG, bit1=DIS, 1=开 */
    uint8_t  cell_count;             /* 电池串数 */
    uint8_t  ntc_count;              /* NTC 个数 */
    double   ntc_temp_c[16];         /* 实际 ntc_count 个有效 */
} bms_basic_info_t;

/* 单体电压 (0x04 命令) 解码结果 */
typedef struct {
    uint16_t cell_count;
    uint16_t cell_mv[48];            /* 单位 mV, 最多 48 串 */
} bms_cell_voltages_t;

/* 硬件版本号 (0x05 命令) 解码结果 */
typedef struct {
    char hw_version[32];             /* ASCII 型号, 最长 31 + '\0' */
} bms_hw_version_t;

/*
 * 三条 BMS 读命令封装: send read cmd -> wait frame -> validate -> parse.
 * 任何一个失败返回 BMS_ERR_*, 成功 BMS_OK.
 * 写命令 0xE1 (控制 MOS) 本期不做.
 */
bms_err_t bms_read_basic(int fd, bms_basic_info_t *out);
bms_err_t bms_read_cells(int fd, bms_cell_voltages_t *out);
bms_err_t bms_read_hwver(int fd, bms_hw_version_t *out);

#endif /* BMS_H */
