#include "csvlog.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>

/* 表格里预留的 NTC / 串数列数 - 让表头固定, 缺位留空 */
#define NTC_COLS     4
#define CELL_COLS    48

/* 把 timestamp 输出成 ISO 风格: 2026-07-16 14:32:01 */
static void print_iso_time(FILE *f, time_t ts) {
    struct tm tm;
    localtime_r(&ts, &tm);
    char buf[32];
    strftime(buf, sizeof buf, "%Y-%m-%d %H:%M:%S", &tm);
    fputs(buf, f);
}

/* 创建 logs/ (若不存在), 重复创建安全 */
static int ensure_logs_dir(void) {
    struct stat st;
    if (stat("logs", &st) == 0 && S_ISDIR(st.st_mode)) return 0;
    if (mkdir("logs", 0755) == 0) return 0;
    if (errno == EEXIST) return 0;
    return -1;
}

/* 内部 kind -> 文件名映射 (全英文, 无缩写)
 *   "basic" -> 菜单 1: 基本信息 (Battery Basic Info)
 *   "cells" -> 菜单 2: 电池单体电压 (Cell Voltages)
 *   "hwver" -> 菜单 3: 硬件版本号 (Hardware Version)
 * 多次启动程序继续往同一份 CSV 追加.
 */
static const char *csv_filename_of(const char *kind) {
    if (strcmp(kind, "basic") == 0) return "battery_basic_info";
    if (strcmp(kind, "cells") == 0) return "battery_cell_voltages";
    if (strcmp(kind, "hwver") == 0) return "battery_hardware_version";
    return NULL;
}

int csvlog_open(const char *kind, FILE **out) {
    if (!kind || !out) return -1;
    if (ensure_logs_dir() < 0) return -1;

    const char *name = csv_filename_of(kind);
    if (!name) return -1;

    char path[160];
    snprintf(path, sizeof path, "logs/%s.csv", name);

    FILE *f = fopen(path, "a");    /* append: 多次启动继续在同一文件追加 */
    if (!f) return -1;

    /* 表头仅在文件为空时写 */
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

    /* 固定列数, 缺位填空 */
    for (int i = 0; i < NTC_COLS; i++) {
        if (i < b->ntc_count)
            fprintf(f, ",%.1f", b->ntc_temp_c[i]);
        else
            fputc(',', f);
    }
    fputc('\n', f);
    fflush(f);
}

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

void csvlog_append_hwver(FILE *f, const bms_hw_version_t *h, time_t ts) {
    if (!f || !h) return;
    print_iso_time(f, ts);
    fprintf(f, ",%s\n", h->hw_version);
    fflush(f);
}

/* ============== 监控日志 (logs/monitoring/) ============== */

/* 确保 logs/monitoring/ 存在 (EEXIST 不算错) */
static int ensure_logs_monitoring_dir(void) {
    struct stat st;
    if (stat("logs/monitoring", &st) == 0 && S_ISDIR(st.st_mode)) return 0;
    if (mkdir("logs/monitoring", 0755) == 0) return 0;
    if (errno == EEXIST) return 0;
    return -1;
}

/* 监控日志文件名映射. 与手动 CSV 同名 (battery_basic_info / battery_cell_voltages),
 * 末尾追加时间戳 (YYYYMMDD_HHMMSS), 每次启动监控都生成独立文件.
 */
static const char *csv_monitor_filename_of(const char *kind) {
    if (strcmp(kind, "basic") == 0) return "battery_basic_info";
    if (strcmp(kind, "cells") == 0) return "battery_cell_voltages";
    return NULL;
}

int csvlog_open_monitor(const char *kind, FILE **out) {
    if (!kind || !out) return -1;
    if (ensure_logs_dir() < 0)        return -1;
    if (ensure_logs_monitoring_dir() < 0) return -1;

    const char *name = csv_monitor_filename_of(kind);
    if (!name) return -1;

    /* 时间戳后缀: 每次启动监控是一份独立文件 (跟手动 CSV 不同, 手动 CSV 反复追加) */
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    char path[200];
    snprintf(path, sizeof path,
             "logs/monitoring/%s_%04d%02d%02d_%02d%02d%02d.csv",
             name,
             tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
             tm.tm_hour, tm.tm_min, tm.tm_sec);

    FILE *f = fopen(path, "w");     /* "w" -- 每次起监控都是独立新文件 */
    if (!f) return -1;

    /* 必写表头 (新文件) */
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
        ? (double) remaining_mah / nominal_capacity_mah *100.0
        : 0.0;

    print_iso_time(f, ts);
    fprintf(f, ",%.2f,%+.3f,%u",
            total_voltage_v,
            current_a,
            remaining_mah
            );

    /* 固定 4 个 NTC 列, 缺位留空 */
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
