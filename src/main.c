#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bms.h"          /* bms_debug extern */
#include "serial.h"
#include "ui.h"
#include "csvlog.h"
#include "monitor.h"

/* 调试开关, --debug 打开, ui.c 的 on_* 会显示原始字节 */
int bms_debug = 0;

/* 监控模式标记: 非空字符串时进入 monitor 而不是菜单 */
static const char *g_monitor_cmds = NULL;   /* 例如 "03,04" */
static int g_monitor_interval = 5;          /* 秒 */

static void die(const char *msg) {
    fprintf(stderr, "[fatal] %s\n", msg);
    fflush(stderr);
    exit(1);
}

static void usage(const char *prog) {
    fprintf(stderr,
        "用法 1 (手动菜单):\n"
        "  %s [--debug] [串口设备]\n"
        "    无参数      弹出端口选择器\n"
        "    /dev/ttyXXX 直接打开指定端口\n"
        "    --debug     打印 TX/RX 原始字节流\n"
        "\n"
        "用法 2 (长期监控, logs/monitoring/):\n"
        "  %s --monitor <list> [--interval <sec>] [串口设备]\n"
        "    --monitor 03        只跑 0x03 (摘要版)\n"
        "    --monitor 04        只跑 0x04 (摘要版)\n"
        "    --monitor 03,04     两个一起\n"
        "    --interval N        间隔 N 秒 (1..3600), 默认 5\n"
        "    Ctrl+C (SIGINT) 退出\n",
        prog, prog);
}

/* 解析 --monitor <list> -- 把 "03,04" 拆成 [0x03, 0x04] */
static int parse_monitor_list(const char *list, uint8_t *out, int cap) {
    if (!list || cap <= 0) return -1;
    int n = 0;
    const char *p = list;
    while (*p) {
        /* 跳空格 / 逗号 */
        while (*p == ' ' || *p == ',') p++;
        if (!*p) break;

        if (p[0] == '0' && (p[1] == '3' || p[1] == '4') &&
            (p[2] == '\0' || p[2] == ',' || p[2] == ' '))
        {
            if (n >= cap) return -1;
            out[n++] = (uint8_t)((p[1] - '0') | 0x00);
            p += 2;
        } else {
            return -1;  /* 非 03 / 04 */
        }
    }
    return n;
}

int main(int argc, char **argv) {
    const char *port_path = NULL;

    /* 解析 flag */
    int i = 1;
    while (i < argc && argv[i][0] == '-') {
        if (strcmp(argv[i], "--debug") == 0) {
            bms_debug = 1;
            i++;
        } else if (strcmp(argv[i], "--monitor") == 0) {
            if (i + 1 >= argc) {
                usage(argv[0]);
                return 1;
            }
            i++;
            g_monitor_cmds = argv[i++];
        } else if (strcmp(argv[i], "--interval") == 0) {
            if (i + 1 >= argc) {
                usage(argv[0]);
                return 1;
            }
            i++;
            g_monitor_interval = atoi(argv[i++]);
            if (g_monitor_interval < 1) g_monitor_interval = 1;
        } else {
            usage(argv[0]);
            return 1;
        }
    }
    if (i < argc) port_path = argv[i];

    /* 1. 选定串口 */
    int fd = -1;
    if (port_path) {
        fd = serial_open(port_path);
        if (fd < 0) die("serial_open 失败");
    } else if (!g_monitor_cmds) {
        /* 只有手动模式才弹端口选择器, 监控模式没端口就让用户写出来 */
        char picked[64] = {0};
        if (ui_pick_serial_port(picked, sizeof picked) < 0)
            die("未选择串口");
        fd = serial_open(picked);
        if (fd < 0) die("serial_open 失败");
    } else {
        usage(argv[0]);
        return 1;
    }

    fprintf(stderr, "[ok] 已打开端口 fd=%d, --debug=%d, 模式=%s\n",
            fd, bms_debug,
            g_monitor_cmds ? "monitor" : "menu");
    fflush(stderr);

    setvbuf(stdout, NULL, _IOLBF, 0);
    setvbuf(stderr, NULL, _IOLBF, 0);

    /* 2. 串口配置: 9600-8-N-1 */
    if (serial_config(fd, B9600) < 0)
        die("serial_config 失败");

    /* 3. 进入相应模式 */
    int rc = 0;
    if (g_monitor_cmds) {
        uint8_t cmds[4] = {0};
        int n_cmds = parse_monitor_list(g_monitor_cmds, cmds, 4);
        if (n_cmds <= 0) {
            fprintf(stderr,
                    "[fatal] --monitor 参数错, 必须是 03/04 逗号分隔\n");
            serial_close(fd);
            return 1;
        }
        rc = bms_run_monitor(fd, cmds, n_cmds, g_monitor_interval);
        serial_close(fd);
    } else {
        /* 手动菜单 */
        FILE *f_basic = NULL, *f_cells = NULL, *f_hwver = NULL;
        csvlog_open("basic", &f_basic);
        csvlog_open("cells", &f_cells);
        csvlog_open("hwver", &f_hwver);

        rc = ui_run_menu(fd, f_basic, f_cells, f_hwver);

        if (f_basic) fclose(f_basic);
        if (f_cells) fclose(f_cells);
        if (f_hwver) fclose(f_hwver);
        serial_close(fd);
    }
    return rc;
}
