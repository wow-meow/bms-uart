#include "monitor.h"

#include "bms.h"
#include "csvlog.h"
#include "prot_fmt.h"
#include "serial.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <signal.h>
#include <sys/select.h>

/* SIGINT 一来置 1, 主循环看到就退出 (sleep 也会被打断) */
static volatile sig_atomic_t g_stop = 0;
static void on_sigint(int sig) {
    (void)sig;
    g_stop = 1;
}

static void install_sigint_hook(void) {
    struct sigaction sa = {0};
    sa.sa_handler = on_sigint;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
}

/* 算细胞串的统计量 (min/max/spread/avg). cell_count==0 时不写 out. */
static void calc_cell_stats(const uint16_t *mv, int n,
                             uint32_t *out_min, uint32_t *out_max,
                             uint32_t *out_spread, uint32_t *out_avg)
{
    if (n <= 0) {
        *out_min = *out_max = *out_spread = *out_avg = 0;
        return;
    }
    uint32_t vmin = mv[0], vmax = mv[0];
    uint64_t sum = 0;
    for (int i = 0; i < n; i++) {
        if (mv[i] < vmin) vmin = mv[i];
        if (mv[i] > vmax) vmax = mv[i];
        sum += mv[i];
    }
    *out_min    = vmin;
    *out_max    = vmax;
    *out_spread = vmax - vmin;
    *out_avg    = (uint32_t)(sum / (uint64_t)n);
}

/* 把时间戳打到终端的小辅助 (HH:MM:SS) */
static void print_hms(char *buf, size_t cap, time_t ts) {
    struct tm tm;
    localtime_r(&ts, &tm);
    strftime(buf, cap, "%H:%M:%S", &tm);
}

static void do_cmd_03(int fd, FILE *fcsv, uint64_t *n_records) {
    bms_basic_info_t b;
    bms_err_t e = bms_read_basic(fd, &b);
    if (e != BMS_OK) {
        fprintf(stderr, "[warn] cmd 0x03 本轮超时, 跳过 (err=%d)\n", e);
        fflush(stderr);
        return;
    }

    time_t now = time(NULL);

    /* 抽取触发保护名 (空=无) */
    char names[256];
    format_protection_names(names, sizeof names, b.protection_bits);

    /* 写监控 CSV (摘要) */
    csvlog_append_monitor_basic(fcsv,
        b.total_voltage_v,
        b.current_a,
        b.remaining_capacity_mah,
        b.prod_date,
        b.ntc_count,
        b.ntc_temp_c,
        b.protection_bits,
        names,
        now);

    /* 终端打印: 一行紧凑 */
    char tbuf[16];
    print_hms(tbuf, sizeof tbuf, now);
    printf("[%s] V=%.2fV I=%+.2fA Ah=%umAh T=[",
           tbuf, b.total_voltage_v, b.current_a,
           b.remaining_capacity_mah);
    for (uint8_t i = 0; i < b.ntc_count; i++) {
        printf("%.1f%s", b.ntc_temp_c[i],
               (i + 1 < b.ntc_count) ? "," : "");
    }
    printf("] protection_status=0x%04X (%s)\n",
           b.protection_bits,
           (b.protection_bits ? names : "无"));
    fflush(stdout);

    (*n_records)++;
}

static void do_cmd_04(int fd, FILE *fcsv, uint64_t *n_records) {
    bms_cell_voltages_t c;
    bms_err_t e = bms_read_cells(fd, &c);
    if (e != BMS_OK) {
        fprintf(stderr, "[warn] cmd 0x04 本轮超时, 跳过 (err=%d)\n", e);
        fflush(stderr);
        return;
    }

    time_t now = time(NULL);

    uint32_t vmin, vmax, vspread, vavg;
    calc_cell_stats(c.cell_mv, c.cell_count,
                    &vmin, &vmax, &vspread, &vavg);

    csvlog_append_monitor_cells(fcsv,
        c.cell_count, vmin, vmax, vspread, vavg, now);

    char tbuf[16];
    print_hms(tbuf, sizeof tbuf, now);
    printf("[%s] cells=%u min=%umV max=%umV spread=%umV avg=%umV (mV)\n",
           tbuf, c.cell_count, vmin, vmax, vspread, vavg);
    fflush(stdout);

    (*n_records)++;
}

/* "轮询等待" 一秒, 期间查 stdin 是不是收到了 'q'/'Q'. 收到返回 1. */
static int wait_1s_check_q(void) {
    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(0, &rfds);
    struct timeval tv = { 1, 0 };
    int r = select(1, &rfds, NULL, NULL, &tv);
    if (r <= 0) return 0;

    char buf[64];
    if (!fgets(buf, sizeof buf, stdin)) return 0;
    if (buf[0] == 'q' || buf[0] == 'Q') return 1;
    return 0;
}

/* ============= 内部循环体 ============= */
int bms_poll_loop(int fd,
                  const uint8_t *cmds, int n_cmds,
                  int interval_sec,
                  FILE *f_basic, FILE *f_cells)
{
    if (!cmds || n_cmds <= 0) return -1;
    if (interval_sec < 1 || interval_sec > 3600) return -1;

    g_stop = 0;
    install_sigint_hook();

    fprintf(stderr, "[poll] 启动. 按 q 或 Ctrl+C 退出.\n");
    fflush(stderr);

    uint64_t n_records = 0;
    while (!g_stop) {
        for (int i = 0; i < n_cmds; i++) {
            if (g_stop) break;
            if (cmds[i] == 0x03 && f_basic) do_cmd_03(fd, f_basic, &n_records);
            else if (cmds[i] == 0x04 && f_cells) do_cmd_04(fd, f_cells, &n_records);
        }
        if (g_stop) break;

        /* 每秒查一次 stdin, 用户按 q 就退出 */
        for (int s = 0; s < interval_sec && !g_stop; s++) {
            if (wait_1s_check_q()) { g_stop = 1; break; }
        }
    }

    fprintf(stderr, "[poll] 停止. 共记录 %llu 条\n",
            (unsigned long long)n_records);
    fflush(stderr);
    return (int)n_records;
}

/* ============= CLI 顶层 ============= */
int bms_run_monitor(int fd,
                    const uint8_t *cmds, int n_cmds,
                    int interval_sec)
{
    if (!cmds || n_cmds <= 0) return -1;
    if (interval_sec < 1 || interval_sec > 3600) return -1;

    /* 打开监控 CSV (传 NULL / 跳过无效 cmd) */
    FILE *f_basic = NULL, *f_cells = NULL;
    for (int i = 0; i < n_cmds; i++) {
        if (cmds[i] == 0x03) {
            if (csvlog_open_monitor("basic", &f_basic) < 0) {
                fprintf(stderr, "[fatal] 打开监控 CSV (basic) 失败\n");
                return -1;
            }
        } else if (cmds[i] == 0x04) {
            if (csvlog_open_monitor("cells", &f_cells) < 0) {
                fprintf(stderr, "[fatal] 打开监控 CSV (cells) 失败\n");
                return -1;
            }
        } else {
            fprintf(stderr, "[warn] 跳过未知命令 0x%02X\n", cmds[i]);
        }
    }

    fprintf(stderr,
            "[monitor] 启动: cmds={");
    for (int i = 0; i < n_cmds; i++) {
        fprintf(stderr, "%s0x%02X", i ? "," : "", cmds[i]);
    }
    fprintf(stderr, "} interval=%ds\n", interval_sec);
    fflush(stderr);

    int rc = bms_poll_loop(fd, cmds, n_cmds, interval_sec, f_basic, f_cells);

    if (f_basic) fclose(f_basic);
    if (f_cells) fclose(f_cells);
    return rc;
}
