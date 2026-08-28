/*
 * csvlog.c — CSV 日志落地层 / CSV Logging Storage Layer
 *
 * 包含两套日志记录机制 (统一的追加与安全写入风格):
 * Two logging mechanisms sharing consistent append & safe flushing design:
 *   1. 手动菜单日志 / Manual Menu Logs:
 *      路径 / Path: logs/<name>.csv (如 logs/battery_basic_info.csv)
 *      模式 / Mode: append 追加模式, 跨次启动续写同一份日志文件.
 *   2. 监控日志 / Monitoring Logs:
 *      路径 / Path: logs/monitoring/<name>_<YYYYMMDD_HHMMSS>.csv
 *      模式 / Mode: 每次启动独立生成带时间戳的新文件, 记录计算后的紧凑摘要数据.
 *
 * 约定与可靠性保证 / Design Conventions & Reliability:
 *   - 表头仅在空文件 (ftell == 0) 时自动写入 / Headers are written only when file is brand new.
 *   - 每写一行数据立即调用 fflush(), 确保掉电/异常中断时最多仅丢失最后一行 /
 *     Immediate fflush() per record minimizes data loss on power cuts.
 *   - 列数固定 (如 NTC_COLS=4, CELL_COLS=48), 缺位输出空逗号占位, 保证 Python pandas/Excel 可直接无歧义解析 /
 *     Fixed column schema with empty entries ensures flawless parsing in pandas/Excel.
 */

#include "csvlog.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>

/* 表格里预留的 NTC 温度与电池串数列数 (固定表头, 缺位留空)
 * Fixed column counts for NTC sensors and cell voltages (empty values if unpopulated) */
#define NTC_COLS     4
#define CELL_COLS    48

/*
 * @brief 将 UNIX 时间戳格式化输出为 ISO 风格字符串: YYYY-MM-DD HH:MM:SS
 *        Format UNIX timestamp into ISO string: YYYY-MM-DD HH:MM:SS
 */
static void print_iso_time(FILE *f, time_t ts) {
    struct tm tm;
    localtime_r(&ts, &tm);
    char buf[32];
    strftime(buf, sizeof buf, "%Y-%m-%d %H:%M:%S", &tm);
    fputs(buf, f);
}

/*
 * @brief 确保 logs/ 目录存在 (若不存在则自动创建, 具有幂等性)
 *        Ensure logs/ directory exists (creates if not present, idempotent)
 * @return 0 成功或已存在, -1 创建失败 / 0 on success/exists, -1 on failure
 */
static int ensure_logs_dir(void) {
    struct stat st;
    if (stat("logs", &st) == 0 && S_ISDIR(st.st_mode)) return 0;
    if (mkdir("logs", 0755) == 0) return 0;
    if (errno == EEXIST) return 0;
    return -1;
}

/*
 * @brief 手动菜单日志类别 -> 文件基名映射
 *        Map manual log kind string to base filename
 *   "basic" -> "battery_basic_info"
 *   "cells" -> "battery_cell_voltages"
 *   "hwver" -> "battery_hardware_version"
 */
static const char *csv_filename_of(const char *kind) {
    if (strcmp(kind, "basic") == 0) return "battery_basic_info";
    if (strcmp(kind, "cells") == 0) return "battery_cell_voltages";
    if (strcmp(kind, "hwver") == 0) return "battery_hardware_version";
    return NULL;
}

/*
 * @brief 打开 (或创建) 手动菜单 CSV 文件 (位于 logs/ 目录下, append 追加模式)
 *        Open or create manual interactive menu CSV under logs/ (append mode)
 * @param kind 日志类型 ("basic" / "cells" / "hwver") / Log kind
 * @param out 输出打开的文件指针 FILE** / Output pointer to FILE*
 * @return 0 成功, -1 失败 / 0 on success, -1 on failure
 */
int csvlog_open(const char *kind, FILE **out) {
    if (!kind || !out) return -1;
    if (ensure_logs_dir() < 0) return -1;

    const char *name = csv_filename_of(kind);
    if (!name) return -1;

    char path[160];
    snprintf(path, sizeof path, "logs/%s.csv", name);

    FILE *f = fopen(path, "a");    /* append: 多次启动继续在同一文件追加 / Append mode across runs */
    if (!f) return -1;

    /* 表头仅在文件为空时写入 / Write header only when file is newly created */
    if (ftell(f) == 0) {
        if (strcmp(kind, "basic") == 0) {
            fputs("timestamp,total_voltage_v,current_a,"
                  "remaining_capacity_mah,nominal_capacity_mah,cycle_count,"
                  "prod_date,balance_low,balance_high,protection_bits,"
                  "sw_version,rsoc_pct,fet_state,cell_count,ntc_count",
                  f);
            for (int i = 1; i <= NTC_COLS; i++)
                fprintf(f, ",ntc%d_c", i);
            fputc('\n', f);
        } else if (strcmp(kind, "cells") == 0) {
            fputs("timestamp,cell_count", f);
            for (int i = 1; i <= CELL_COLS; i++)
                fprintf(f, ",cell%d_mv", i);
            fputc('\n', f);
        } else if (strcmp(kind, "hwver") == 0) {
            fputs("timestamp,hw_version\n", f);
        } else {
            fclose(f);
            return -1;
        }
    }

    fflush(f);
    *out = f;
    return 0;
}

/*
 * @brief 追加一条 0x03 基本信息: 固定 15 列 + NTC_COLS 个温度列 (缺位留空)
 *        Append a 0x03 basic info record: 15 fixed columns + NTC_COLS temperature columns
 */
void csvlog_append_basic(FILE *f, const bms_basic_info_t *b, time_t ts) {
    if (!f || !b) return;
    print_iso_time(f, ts);
    fprintf(f, ",%.2f,%.3f,%u,%u,%u,%s,%u,%u,%u,%d.%d,%u,%u,%u,%u",
            b->total_voltage_v,
            b->current_a,
            b->remaining_capacity_mah,
            b->nominal_capacity_mah,
            b->cycle_count,
            b->prod_date,
            b->balance_low,
            b->balance_high,
            b->protection_bits,
            b->sw_version_major, b->sw_version_minor,
            b->rsoc_pct,
            b->fet_state,
            b->cell_count,
            b->ntc_count);

    /* 固定列数, 超出/不足部分以逗号对齐 / Fixed column count, pad with commas */
    for (int i = 0; i < NTC_COLS; i++) {
        if (i < b->ntc_count)
            fprintf(f, ",%.1f", b->ntc_temp_c[i]);
        else
            fputc(',', f);
    }
    fputc('\n', f);
    fflush(f);
}

/*
 * @brief 追加一条 0x04 单体电压: cell_count + CELL_COLS 个 mV 列 (缺位留空)
 *        Append a 0x04 cell voltages record: cell_count + CELL_COLS mV columns
 */
void csvlog_append_cells(FILE *f, const bms_cell_voltages_t *c, time_t ts) {
    if (!f || !c) return;
    print_iso_time(f, ts);
    fprintf(f, ",%u", c->cell_count);
    for (int i = 0; i < CELL_COLS; i++) {
        if (i < c->cell_count)
            fprintf(f, ",%u", c->cell_mv[i]);
        else
            fputc(',', f);
    }
    fputc('\n', f);
    fflush(f);
}

/*
 * @brief 追加一条 0x05 硬件版本号: timestamp, hw_version
 *        Append a 0x05 hardware version record: timestamp, hw_version
 */
void csvlog_append_hwver(FILE *f, const bms_hw_version_t *h, time_t ts) {
    if (!f || !h) return;
    print_iso_time(f, ts);
    fprintf(f, ",%s\n", h->hw_version);
    fflush(f);
}

/* ============== 监控日志 / Monitoring Logs (logs/monitoring/) ============== */

/*
 * @brief 确保 logs/monitoring/ 子目录存在
 *        Ensure logs/monitoring/ directory exists
 */
static int ensure_logs_monitoring_dir(void) {
    struct stat st;
    if (stat("logs/monitoring", &st) == 0 && S_ISDIR(st.st_mode)) return 0;
    if (mkdir("logs/monitoring", 0755) == 0) return 0;
    if (errno == EEXIST) return 0;
    return -1;
}

/*
 * @brief 监控日志类别 -> 文件名前缀映射
 *        Map monitoring kind to filename prefix
 */
static const char *csv_monitor_filename_of(const char *kind) {
    if (strcmp(kind, "basic") == 0) return "battery_basic_info";
    if (strcmp(kind, "cells") == 0) return "battery_cell_voltages";
    return NULL;
}

/*
 * @brief 创建监控 CSV 文件: logs/monitoring/<name>_<YYYYMMDD_HHMMSS>.csv ("w" 模式)
 *        Create monitoring CSV file under logs/monitoring/ with timestamp ("w" mode)
 *
 * 每次启动监控均生成独立新文件并写入表头. kind 支持 "basic" / "cells".
 * Creates a brand new file with headers written on every monitoring session start.
 */
int csvlog_open_monitor(const char *kind, FILE **out) {
    if (!kind || !out) return -1;
    if (ensure_logs_dir() < 0)        return -1;
    if (ensure_logs_monitoring_dir() < 0) return -1;

    const char *name = csv_monitor_filename_of(kind);
    if (!name) return -1;

    /* 时间戳后缀: 每次启动监控生成独立文件 / Timestamp suffix for distinct session file */
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    char path[200];
    snprintf(path, sizeof path,
             "logs/monitoring/%s_%04d%02d%02d_%02d%02d%02d.csv",
             name,
             tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
             tm.tm_hour, tm.tm_min, tm.tm_sec);

    FILE *f = fopen(path, "w");     /* "w" -- 每次启动监控创建独立文件 / Brand new file */
    if (!f) return -1;

    /* 必写表头 (新文件) / Write CSV header for new file */
    if (strcmp(kind, "basic") == 0) {
        fputs("timestamp,total_voltage_v,current_a,"
              "remaining_capacity_mah,"
              "ntc1_c,ntc2_c,ntc3_c,"
              "protection_status,capacity_percent\n", f);
    } else if (strcmp(kind, "cells") == 0) {
        fputs("timestamp,cell_count,cell_min_mv,"
              "cell_max_mv,cell_spread_mv,cell_avg_mv\n", f);
    } else {
        fclose(f);
        return -1;
    }

    fflush(f);
    *out = f;
    return 0;
}

/*
 * @brief 追加一条监控摘要 (basic): 电压/电流/剩余容量 + 3 个 NTC 列 + 保护位 (hex) + 容量百分比
 *        Append monitoring summary row (basic): V/I/Capacity + 3 NTC temps + protection hex + SOC %
 */
void csvlog_append_monitor_basic(FILE *f,
    double total_voltage_v,
    double current_a,
    uint32_t remaining_mah,
    uint32_t nominal_capacity_mah,
    uint8_t ntc_count,
    const double *ntc_temp_c,
    uint16_t protection_bits,
    time_t ts)
{
    if (!f) return;

    double cap_pct = (nominal_capacity_mah > 0)
        ? (double) remaining_mah / nominal_capacity_mah * 100.0
        : 0.0;

    print_iso_time(f, ts);
    fprintf(f, ",%.2f,%+.3f,%u",
            total_voltage_v,
            current_a,
            remaining_mah);

    /* 固定 3 个 NTC 列, 缺位留空 / Fixed 3 NTC columns, pad if missing */
    for (int i = 0; i < 3; i++) {
        if (i < ntc_count && ntc_temp_c)
            fprintf(f, ",%.1f", ntc_temp_c[i]);
        else
            fputc(',', f);
    }

    fprintf(f, ",0x%04X,%.2f\n",
            protection_bits,
            cap_pct);
    fflush(f);
}

/*
 * @brief 追加一条监控摘要 (cells): 串数 + min/max/spread/avg (单位 mV)
 *        Append monitoring summary row (cells): cell count + min/max/spread/avg in mV
 */
void csvlog_append_monitor_cells(FILE *f,
    uint16_t cell_count,
    uint32_t min_mv,
    uint32_t max_mv,
    uint32_t spread_mv,
    uint32_t avg_mv,
    time_t ts)
{
    if (!f) return;
    print_iso_time(f, ts);
    fprintf(f, ",%u,%u,%u,%u,%u\n",
            cell_count, min_mv, max_mv, spread_mv, avg_mv);
    fflush(f);
}
