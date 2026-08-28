/*
 * csvlog.h — CSV 日志接口 / CSV Logging Interface
 * (实现 / Implementation: src/csvlog.c)
 *
 * 手动菜单日志 (logs/) 与监控日志 (logs/monitoring/) 两套体系.
 * Two logging sets: manual interactive menu logs (logs/) & automated monitoring logs (logs/monitoring/).
 */

#ifndef BMS_CSVLOG_H
#define BMS_CSVLOG_H

#include <stdio.h>
#include <time.h>
#include "bms.h"

/* ============== 手动菜单日志 / Manual Interactive Menu Logs ============== */

/**
 * @brief 在 logs/ 下创建或打开带表头的 CSV 文件 (追加模式)
 *        Open or create CSV log file under logs/ with header written if empty (append mode)
 * @param kind 日志类型: "basic" / "cells" / "hwver" / Log kind: "basic", "cells", or "hwver"
 * @param out 输出打开的文件句柄指针 / Output pointer to opened FILE*
 * @return 0 成功, -1 失败 / 0 on success, -1 on failure
 */
int  csvlog_open(const char *kind, FILE **out);

/**
 * @brief 追加一条 0x03 基本信息解码结果到 CSV
 *        Append a decoded basic info record (cmd 0x03) to CSV
 * @param f 文件句柄 / Open file pointer
 * @param b 基本信息结构体指针 / Pointer to basic info struct
 * @param ts 时间戳 / Record timestamp
 */
void csvlog_append_basic(FILE *f, const bms_basic_info_t *b, time_t ts);

/**
 * @brief 追加一条 0x04 单体电压解码结果到 CSV
 *        Append a decoded cell voltages record (cmd 0x04) to CSV
 * @param f 文件句柄 / Open file pointer
 * @param c 单体电压结构体指针 / Pointer to cell voltages struct
 * @param ts 时间戳 / Record timestamp
 */
void csvlog_append_cells(FILE *f, const bms_cell_voltages_t *c, time_t ts);

/**
 * @brief 追加一条 0x05 硬件版本号到 CSV
 *        Append a decoded hardware version record (cmd 0x05) to CSV
 * @param f 文件句柄 / Open file pointer
 * @param h 硬件版本结构体指针 / Pointer to hardware version struct
 * @param ts 时间戳 / Record timestamp
 */
void csvlog_append_hwver(FILE *f, const bms_hw_version_t *h, time_t ts);

/* ============== 长期监控日志 / Long-term Monitoring Logs (logs/monitoring/) ============== */

/**
 * @brief 打开监控专用 CSV 文件 (每次启动生成带时间戳的新文件)
 *        Open a monitoring CSV file under logs/monitoring/ with timestamped filename
 * @param kind 日志类型: "basic" / "cells" / Log kind: "basic" or "cells"
 * @param out 输出打开的文件句柄指针 / Output pointer to opened FILE*
 * @return 0 成功, -1 失败 / 0 on success, -1 on failure
 */
int  csvlog_open_monitor(const char *kind, FILE **out);

/**
 * @brief 监控专用: 追加基本信息摘要行
 *        Append summarized basic info row for monitoring mode
 * @param f 文件句柄 / Open file pointer
 * @param total_voltage_v 总电压 (V) / Total pack voltage (V)
 * @param current_a 电流 (A) / Current in Amperes
 * @param remaining_mah 剩余容量 (mAh) / Residual capacity (mAh)
 * @param nominal_capacity_mah 标称容量 (mAh) / Nominal capacity (mAh)
 * @param ntc_count NTC 个数 / Number of NTC sensors
 * @param ntc_temp_c NTC 温度数组 (前 3 个写入列) / NTC temperatures array (first 3 written to columns)
 * @param protection_bits 保护状态位 / Protection status bitmap
 * @param ts 时间戳 / Record timestamp
 */
void csvlog_append_monitor_basic(FILE *f,
    double total_voltage_v,
    double current_a,
    uint32_t remaining_mah,
    uint32_t nominal_capacity_mah,
    uint8_t ntc_count,
    const double *ntc_temp_c,
    uint16_t protection_bits,
    time_t ts);

/**
 * @brief 监控专用: 追加单体电压统计摘要行 (min/max/spread/avg)
 *        Append summarized cell voltage statistics row for monitoring mode (min/max/spread/avg)
 * @param f 文件句柄 / Open file pointer
 * @param cell_count 串数 / Number of cells
 * @param min_mv 最低单体电压 (mV) / Minimum cell voltage (mV)
 * @param max_mv 最高单体电压 (mV) / Maximum cell voltage (mV)
 * @param spread_mv 压差极差 (mV) / Voltage spread / delta (max - min in mV)
 * @param avg_mv 平均电压 (mV) / Average cell voltage (mV)
 * @param ts 时间戳 / Record timestamp
 */
void csvlog_append_monitor_cells(FILE *f,
    uint16_t cell_count,
    uint32_t min_mv,
    uint32_t max_mv,
    uint32_t spread_mv,
    uint32_t avg_mv,
    time_t ts);

#endif /* BMS_CSVLOG_H */
