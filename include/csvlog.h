#ifndef BMS_CSVLOG_H
#define BMS_CSVLOG_H

#include <stdio.h>
#include <time.h>
#include "bms.h"

/*
 * 在 logs/ 下创建一个带时间戳的 CSV, 写好表头, 通过 *out 返回 FILE*.
 * kind: "basic" / "cells" / "hwver" 之一.
 * 成功返回 0, 失败返回 -1.
 */
int  csvlog_open(const char *kind, FILE **out);

/* 把一条解码结果追加到对应 CSV. */
void csvlog_append_basic(FILE *f, const bms_basic_info_t *b, time_t ts);
void csvlog_append_cells(FILE *f, const bms_cell_voltages_t *c, time_t ts);
void csvlog_append_hwver(FILE *f, const bms_hw_version_t *h, time_t ts);

/* ============== 长期监控 (logs/monitoring/) ============== */
/* 跟手动菜单用同一个 kind ("basic" / "cells"), 写 logs/monitoring/<fullname>.csv */
int  csvlog_open_monitor(const char *kind, FILE **out);

/* 监控专用: 摘要版. 保护列表是逗号分隔中文名 (无触发则空字符串) */
void csvlog_append_monitor_basic(FILE *f,
    double total_voltage_v,
    double current_a,
    uint32_t remaining_mah,
    uint32_t nominal_capacity_mah,
    uint8_t ntc_count,
    const double *ntc_temp_c,         /* 至少 4 个, 多的写空 */
    uint16_t protection_bits,
    time_t ts);

void csvlog_append_monitor_cells(FILE *f,
    uint16_t cell_count,
    uint32_t min_mv,
    uint32_t max_mv,
    uint32_t spread_mv,
    uint32_t avg_mv,
    time_t ts);

#endif /* BMS_CSVLOG_H */
