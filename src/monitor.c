/*
 * monitor.c — 长期监控模式 (轮询 + 摘要终端输出 + 监控 CSV)
 *             Long-term Monitoring Mode (Polling + Terminal Summary + CSV Logging)
 *
 * 架构流程 / Architectural Flow:
 *   bms_run_monitor() 初始化日志文件 -> bms_poll_loop() 执行监控循环.
 *   每轮执行 / Each cycle:
 *     依次执行 cmds 数组中的命令 (0x03 / 0x04) -> 终端打印紧凑单行摘要 -> CSV 记录一行数据;
 *   轮询等待 / Wait interval:
 *     基于 select() 监听 stdin 每 1 秒轮询一次, 检查是否有按键 'q' / 'Q', 睡满 interval_sec 秒;
 *   退出响应 / Exit handling:
 *     支持 SIGINT (Ctrl+C) 或键盘 'q' 优雅退出. 单次通信超时仅打印警告并跳过本轮, 长期监控不中断.
 */

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

/* SIGINT 信号到达时置 1, 轮询主循环检测到该标志即安全退出
 * Volatile flag set to 1 on SIGINT; detected by main polling loop for graceful shutdown */
static volatile sig_atomic_t g_stop = 0;

/*
 * @brief SIGINT 信号处理函数 (异步信号安全)
 *        SIGINT signal handler (async-signal-safe, sets flag only)
 */
static void on_sigint(int sig) {
    (void)sig;
    g_stop = 1;
}

/*
 * @brief 安装 SIGINT 信号捕获钩子
 *        Install SIGINT signal action hook
 */
static void install_sigint_hook(void) {
    struct sigaction sa = {0};
    sa.sa_handler = on_sigint;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
}

/*
 * @brief 计算所有单体电池电压的统计指标: 最低、最高、极差压差、平均值 (单位 mV)
 *        Calculate cell voltage statistics: min, max, spread (max-min), and average in mV
 * @param mv 单体电压数组 / Cell voltage array
 * @param n 电池串数 / Number of cells
 * @param out_min 输出最低电压指针 / Pointer to store minimum mV
 * @param out_max 输出最高电压指针 / Pointer to store maximum mV
 * @param out_spread 输出极差指针 / Pointer to store spread (vmax - vmin) mV
 * @param out_avg 输出平均电压指针 / Pointer to store average mV
 */
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

/*
 * @brief 将时间戳格式化为时分秒 "HH:MM:SS" 供终端打印
 *        Format timestamp into "HH:MM:SS" for terminal display
 */
static void print_hms(char *buf, size_t cap, time_t ts) {
    struct tm tm;
    localtime_r(&ts, &tm);
    strftime(buf, cap, "%H:%M:%S", &tm);
}

/*
 * @brief 执行单次 0x03 基本信息查询: 读 BMS -> 写监控 CSV -> 终端打印一行紧凑摘要
 *        Execute single 0x03 basic info query: read BMS -> log CSV -> print terminal summary
 *
 * 单次超时仅警告并跳过 (避免 BMS 瞬态唤醒延迟中断长期监控).
 * Single timeout generates warning and skips row without crashing.
 */
static void do_cmd_03(int fd, FILE *fcsv, uint64_t *n_records) {
    bms_basic_info_t b;
    bms_err_t e = bms_read_basic(fd, &b);
    if (e != BMS_OK) {
        fprintf(stderr, "[warn] cmd 0x03 本轮超时, 跳过 (err=%d)\n", e);
        fflush(stderr);
        return;
    }

    time_t now = time(NULL);

    /* 抽取已触发的保护名称列表 (无触发则为空字符串) / Extract triggered protection names */
    char names[256];
    format_protection_names(names,sizeof names, b.protection_bits);

    /* 追加写入监控 CSV / Append to monitoring CSV */
    csvlog_append_monitor_basic(fcsv,
        b.total_voltage_v,
        b.current_a,
        b.remaining_capacity_mah,
        b.nominal_capacity_mah,
        b.ntc_count,
        b.ntc_temp_c,
        b.protection_bits,
        now);

    /* 终端打印紧凑摘要 / Compact one-line terminal print */
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

/*
 * @brief 执行单次 0x04 单体电压查询: 读 BMS -> 计算统计量 -> 写监控 CSV -> 终端打印摘要
 *        Execute single 0x04 cell voltages query: read BMS -> compute stats -> log CSV -> print summary
 */
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

/*
 * @brief 利用 select() 阻塞最多 1 秒同时监听 stdin:
 *        Wait up to 1 second using select() while monitoring stdin:
 *        收到 'q' 或 'Q' 返回 1 (请求退出), 超时无输入返回 0.
 *        Returns 1 if 'q'/'Q' received (quit requested), 0 on timeout.
 */
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

/* ============= 内部监控循环体 / Inner Polling Loop ============= */
/*
 * @brief 轮询主循环: 循环查询 cmds 中的命令, 间隔 interval_sec 秒
 *        Inner polling loop: periodically queries commands, sleeping interval_sec
 * @return 记录的总条数, 错误返回 -1 / Total records written, or -1 on error
 */
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

        /* 每秒查一次 stdin, 用户按 q 即刻退出 / Poll stdin every 1s to allow prompt exit on 'q' */
        for (int s = 0; s < interval_sec && !g_stop; s++) {
            if (wait_1s_check_q()) { g_stop = 1; break; }
        }
    }

    fprintf(stderr, "[poll] 停止. 共记录 %llu 条\n",
            (unsigned long long)n_records);
    fflush(stderr);
    return (int)n_records;
}

/* ============= CLI 顶层监控入口 / Top-Level CLI Monitoring Entry ============= */
/*
 * @brief 顶层监控入口: 检查入参 -> 打开监控 CSV -> 调用 bms_poll_loop -> 自动关闭文件
 *        Top-level monitor runner: validate args -> open CSVs -> run poll loop -> close files
 */
int bms_run_monitor(int fd,
                    const uint8_t *cmds, int n_cmds,
                    int interval_sec)
{
    if (!cmds || n_cmds <= 0) return -1;
    if (interval_sec < 1 || interval_sec > 3600) return -1;

    /* 打开监控 CSV 文件 (跳过无效命令) / Open monitoring CSV files */
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
