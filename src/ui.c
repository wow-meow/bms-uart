#include "ui.h"
#include "bms.h"
#include "csvlog.h"
#include "protocol.h"
#include "balance_fmt.h"
#include "prot_fmt.h"
#include "monitor.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <errno.h>

#include <dirent.h>     /* opendir / readdir */
#include <sys/select.h> /* select */

/* ============= 端口选择器 ============= */

#define MAX_PORTS 32

static int collect_serial_ports(char paths[][64], int cap) {
    int n = 0;
    DIR *d = opendir("/dev");
    if (!d) return 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL && n < cap) {
        const char *name = e->d_name;
        int match = 0;
        if (strncmp(name, "ttyUSB", 6) == 0) match = 1;
        else if (strncmp(name, "ttyACM", 6) == 0) match = 1;
        if (!match) continue;
        /* name 留 58 字节保险, snprintf 不会越界 */
        snprintf(paths[n], 64, "/dev/%.*s", 58, name);
        n++;
    }
    closedir(d);
    return n;
}

int ui_pick_serial_port(char *out, size_t cap) {
    if (!out || cap == 0) return -1;

    char ports[MAX_PORTS][64];
    int n = collect_serial_ports(ports, MAX_PORTS);

    if (n == 0) {
        puts("没找到 /dev/ttyUSB* 或 /dev/ttyACM*, 请直接输入串口设备路径 (回车取消):");
        printf(">> "); fflush(stdout);
    } else {
        puts("找到以下串口:");
        for (int i = 0; i < n; i++)
            printf("  [%d] %s\n", i + 1, ports[i]);
        printf("选一个 (1-%d), 或直接输入其它路径 (回车取消):\n", n);
        printf(">> "); fflush(stdout);
    }

    char line[256];
    if (!fgets(line, sizeof line, stdin)) return -1;
    line[strcspn(line, "\r\n")] = '\0';
    if (line[0] == '\0') return -1;

    /* 数字选择? */
    if (n > 0) {
        char *endp = NULL;
        long k = strtol(line, &endp, 10);
        if (endp != line && *endp == '\0' && k >= 1 && k <= (long)n) {
            size_t l = strlen(ports[k - 1]);
            if (l >= cap) return -1;
            memcpy(out, ports[k - 1], l + 1);
            return 0;
        }
    }

    /* 当成路径 */
    if (strlen(line) >= cap) {
        fprintf(stderr, "路径太长\n");
        return -1;
    }
    strcpy(out, line);
    return 0;
}

/* ============= 菜单打印 ============= */

void ui_print_menu(void) {
    puts("");
    puts("====== BMS Query Tool (Jiabaida V4) ======");
    puts("  [1] 基本信息       (0x03)");
    puts("  [2] 单体电压       (0x04)");
    puts("  [3] 硬件版本号     (0x05)");
    puts("  [4] 进入监控模式   (logs/monitoring/, 按 q 停)");
    puts("  [q] 退出");
}

/* ============= 漂亮打印 + CSV 追加 ============= */

static void print_now_time(char *buf, size_t cap) {
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    strftime(buf, cap, "%H:%M:%S", &tm);
}

static void on_basic(int fd, FILE *f_csv) {
    fprintf(stderr, "[..] read_basic 开始...\n"); fflush(stderr);
    bms_basic_info_t b;
    bms_err_t e = bms_read_basic(fd, &b);
    if (e != BMS_OK) {
        const char *msg = "?";
        switch (e) {
            case BMS_ERR_TIMEOUT:       msg = "TIMEOUT (已重试 3 次仍无响应)"; break;
            case BMS_ERR_BAD_FRAME:     msg = "BAD_FRAME (帧结构错)"; break;
            case BMS_ERR_BAD_CHECKSUM:  msg = "BAD_CHECKSUM (校验不对)"; break;
            case BMS_ERR_STATUS:        msg = "BMS 报错误状态 0x80"; break;
            default: break;
        }
        fprintf(stderr, "[err] read_basic: %s (code=%d)\n", msg, e);
        fflush(stderr);
        return;
    }
    fprintf(stderr, "[ok ] read_basic 成功\n");
    fflush(stderr);

    char tbuf[16];
    print_now_time(tbuf, sizeof tbuf);

    printf("[%s] 总电压 %.2fV | 电流 %+.2fA | 剩余 %umAh / %umAh (%u%%)\n",
           tbuf,
           b.total_voltage_v, b.current_a,
           b.remaining_capacity_mah, b.nominal_capacity_mah, b.rsoc_pct);
    printf("        循环 %u 次 | 生产 %s | 串数 %u | NTC %u\n",
           b.cycle_count, b.prod_date, b.cell_count, b.ntc_count);

    printf("        温度[°C]");
    for (int i = 0; i < b.ntc_count; i++) {
        printf(" %.1f", b.ntc_temp_c[i]);
    }
    printf("\n");

    {
        const char *chg = (b.fet_state & 0x01) ? "ON" : "OFF";
        const char *dis = (b.fet_state & 0x02) ? "ON" : "OFF";
        printf("        MOS 充=%s 放=%s | ", chg, dis);
        print_balance(b.balance_low, b.balance_high, b.cell_count);
        printf(" | ");
        print_protection_status(b.protection_bits);
    }

    printf("        软件 V%d.%d\n", b.sw_version_major, b.sw_version_minor);

    csvlog_append_basic(f_csv, &b, time(NULL));
}

static void on_cells(int fd, FILE *f_csv) {
    fprintf(stderr, "[..] read_cells 开始...\n"); fflush(stderr);
    bms_cell_voltages_t c;
    bms_err_t e = bms_read_cells(fd, &c);
    if (e != BMS_OK) {
        fprintf(stderr, "[err] read_cells: code=%d\n", e);
        fflush(stderr);
        return;
    }
    fprintf(stderr, "[ok ] read_cells 成功 (cell_count=%u)\n", c.cell_count);
    fflush(stderr);

    char tbuf[16];
    print_now_time(tbuf, sizeof tbuf);

    printf("[%s] 串数 %u (单位 mV):\n", tbuf, c.cell_count);

    /* 多列打印, 每行 8 串 */
    int per_row = 8;
    for (uint16_t i = 0; i < c.cell_count; i++) {
        if (i % per_row == 0) printf("        ");
        printf("[%2u] %4u  ", i + 1, c.cell_mv[i]);
        if ((i + 1) % per_row == 0 || i + 1 == c.cell_count) printf("\n");
    }

    /* 极差统计 */
    if (c.cell_count > 0) {
        uint32_t vmin = c.cell_mv[0], vmax = c.cell_mv[0];
        uint64_t sum = 0;
        for (uint16_t i = 0; i < c.cell_count; i++) {
            if (c.cell_mv[i] < vmin) vmin = c.cell_mv[i];
            if (c.cell_mv[i] > vmax) vmax = c.cell_mv[i];
            sum += c.cell_mv[i];
        }
        printf("        min=%umV max=%umV spread=%umV avg=%umV\n",
               vmin, vmax, vmax - vmin, (uint32_t)(sum / c.cell_count));
    }

    csvlog_append_cells(f_csv, &c, time(NULL));
}

static void on_hwver(int fd, FILE *f_csv) {
    fprintf(stderr, "[..] read_hwver 开始...\n"); fflush(stderr);
    bms_hw_version_t h;
    bms_err_t e = bms_read_hwver(fd, &h);
    if (e != BMS_OK) {
        fprintf(stderr, "[err] read_hwver: code=%d\n", e);
        fflush(stderr);
        return;
    }
    fprintf(stderr, "[ok ] read_hwver 成功\n");
    fflush(stderr);

    char tbuf[16];
    print_now_time(tbuf, sizeof tbuf);
    printf("[%s] 硬件版本号: %s\n", tbuf, h.hw_version);

    csvlog_append_hwver(f_csv, &h, time(NULL));
}

static int run_cmd(int fd, int cmd,
                   FILE *f_basic, FILE *f_cells, FILE *f_hwver) {
    switch (cmd) {
        case 1: on_basic(fd, f_basic); break;
        case 2: on_cells(fd, f_cells); break;
        case 3: on_hwver(fd, f_hwver); break;
        default: return -1;
    }
    return 0;
}

/* ============= 菜单 [4] 进入监控模式 (交互式 setup) ============= */
/* 把 "03,04" 拆成 [0x03, 0x04], 失败返回 -1. */
static int parse_monitor_cmds(const char *s, uint8_t *out, int cap) {
    if (!s || cap <= 0) return -1;
    int n = 0;
    while (*s) {
        while (*s == ' ' || *s == ',') s++;
        if (!*s) break;
        int cmd = 0;
        if (*s == '0' && (s[1] == '3' || s[1] == '4')) {
            cmd = (s[1] == '3') ? 0x03 : 0x04;
            s += 2;
        } else {
            return -1;
        }
        if (n >= cap) return -1;
        out[n++] = (uint8_t)cmd;
    }
    return n;
}

static void ui_poll_mode_setup(int fd) {
    char line[64];
    uint8_t cmds[4];
    int n;

    printf("\n");
    printf("进入监控模式. CSV 写到 logs/monitoring/\n");
    printf("请输入要查询的命令 (例如 03, 03,04): ");
    fflush(stdout);
    if (!fgets(line, sizeof line, stdin)) return;
    line[strcspn(line, "\r\n")] = '\0';

    n = parse_monitor_cmds(line, cmds, 4);
    if (n <= 0) {
        printf("错误: 必须是 03 / 04 / 03,04 等, 逗号分隔\n");
        return;
    }

    printf("请输入轮询间隔秒数 (1..3600, 直接回车用默认 5): ");
    fflush(stdout);
    int interval = 5;
    if (fgets(line, sizeof line, stdin)) {
        if (line[0] != '\n' && line[0] != '\r' && line[0] != '\0') {
            interval = atoi(line);
            if (interval < 1)   interval = 1;
            if (interval > 3600) interval = 3600;
        }
    }

    /* 打开监控 CSV, 调 poll_loop. 它会自己装 SIGINT 钩 */
    FILE *f_basic = NULL, *f_cells = NULL;
    for (int i = 0; i < n; i++) {
        if (cmds[i] == 0x03) csvlog_open_monitor("basic", &f_basic);
        if (cmds[i] == 0x04) csvlog_open_monitor("cells", &f_cells);
    }
    if (!f_basic && !f_cells) {
        fprintf(stderr, "[err] 打开监控 CSV 失败\n");
        return;
    }

    bms_poll_loop(fd, cmds, n, interval, f_basic, f_cells);

    if (f_basic) fclose(f_basic);
    if (f_cells) fclose(f_cells);
}

/* ============= 主循环 ============= */

int ui_run_menu(int fd, FILE *f_basic, FILE *f_cells, FILE *f_hwver) {
    char line[128];

    for (;;) {
        ui_print_menu();
        printf(">> ");
        fflush(stdout);
        if (!fgets(line, sizeof line, stdin))
            return 0;
        line[strcspn(line, "\r\n")] = '\0';
        if (line[0] == '\0') continue;

        if (line[0] == 'q' || line[0] == 'Q')
            return 0;

        /* 菜单 4: 进入监控模式 (交互式 setup) */
        if (line[0] == '4') {
            ui_poll_mode_setup(fd);
            continue;
        }

        /* 单次命令 */
        if (line[0] == '1' || line[0] == '2' || line[0] == '3') {
            int cmd = line[0] - '0';
            run_cmd(fd, cmd, f_basic, f_cells, f_hwver);
            continue;
        }

        puts("未知命令. 输入 q 退出");
    }
}
