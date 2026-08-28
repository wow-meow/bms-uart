/*
 * main.c — 程序入口 + 命令行解析 / Program Entry Point & CLI Argument Parsing
 *
 * 支持两种运行模式 / Supports two execution modes:
 *   1. 手动菜单模式 (默认) / Manual Interactive Menu Mode (default):
 *      ui_run_menu() 提供终端交互菜单, 记录查询日志到 logs/ 目录下的 CSV;
 *   2. 长期监控模式 (--monitor 03,04) / Long-term Monitoring Mode (--monitor 03,04):
 *      bms_run_monitor() 执行周期性轮询, 终端输出单行紧凑摘要, 每次生成独立 CSV 到 logs/monitoring/, 按 Ctrl+C 或 'q' 退出.
 *
 * 整体控制流程 / Overall Control Flow:
 *   解析命令行参数 (Flags) -> 打开指定或交互选取的串口设备 -> 配置串口波特率 (9600-8-N-1)
 *   -> 分发到对应运行模式 -> 关闭串口并优雅退出.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bms.h"          /* bms_debug extern */
#include "serial.h"
#include "ui.h"
#include "csvlog.h"
#include "monitor.h"

/* 调试开关: 1=开启, 打印 TX/RX 原始收发字节流; 由 --debug 命令行参数启用
 * Debug switch: 1 = enabled (prints raw TX/RX hex streams); toggled via --debug CLI flag */
int bms_debug = 0;

/* 监控模式参数: 包含要监控的命令字符串 (例如 "03,04") 及轮询间隔 (秒)
 * Monitoring parameters: comma-separated command string (e.g. "03,04") and polling interval (seconds) */
static const char *g_monitor_cmds = NULL;   /* 非空时进入监控模式 / Non-null triggers monitoring mode */
static int g_monitor_interval = 5;          /* 默认轮询间隔 5 秒 / Default polling interval: 5 seconds */

/*
 * @brief 打印致命错误信息到 stderr 并以退出码 1 终止程序
 *        Print fatal error message to stderr and terminate with exit code 1
 * @param msg 错误提示信息 / Error message string
 */
static void die(const char *msg) {
    fprintf(stderr, "[fatal] %s\n", msg);
    fflush(stderr);
    exit(1);
}

/*
 * @brief 打印帮助与使用说明文本到 stderr
 *        Print command line usage and options help to stderr
 * @param prog 程序可执行文件名 / Executable program name
 */
static void usage(const char *prog) {
    fprintf(stderr,
        "用法 1 (手动菜单) / Mode 1 (Interactive Menu):\n"
        "  %s [--debug] [串口设备 / serial_port]\n"
        "    无参数      弹出端口选择器 / Scan and pick port interactively\n"
        "    /dev/ttyXXX 直接打开指定端口 / Open specified serial device directly\n"
        "    --debug     打印 TX/RX 原始字节流 / Print raw TX/RX hex bytes\n"
        "\n"
        "用法 2 (长期监控, logs/monitoring/) / Mode 2 (Monitoring, logs/monitoring/):\n"
        "  %s --monitor <list> [--interval <sec>] [串口设备 / serial_port]\n"
        "    --monitor 03        只跑 0x03 摘要版 / Poll 0x03 basic info only\n"
        "    --monitor 04        只跑 0x04 摘要版 / Poll 0x04 cell voltages only\n"
        "    --monitor 03,04     两个一起轮询 / Poll both 0x03 and 0x04\n"
        "    --interval N        间隔 N 秒 (1..3600), 默认 5 / Interval in seconds (default 5)\n"
        "    Ctrl+C (SIGINT) 或 'q' 退出 / Press Ctrl+C or 'q' to stop\n",
        prog, prog);
}

/*
 * @brief 解析 --monitor 参数字符串 (如 "03", "04", "03,04", "03, 04") 为命令字节数组
 *        Parse --monitor argument string into an array of command bytes
 *
 * 只允许 03 和 04 两个命令号, 逗号和空格作为有效分隔符.
 * Only accepts 03 and 04, delimited by comma or whitespace.
 *
 * @param list 待解析的字符串 / Input command list string
 * @param out 输出命令字节数组 / Output array for command bytes
 * @param cap 输出数组最大容量 / Maximum capacity of out array
 * @return 成功解析出的命令个数, 非法输入返回 -1 / Number of parsed commands, or -1 on invalid syntax
 */
static int parse_monitor_list(const char *list, uint8_t *out, int cap) {
    if (!list || cap <= 0) return -1;
    int n = 0;
    const char *p = list;
    while (*p) {
        /* 跳过前导空格与逗号 / Skip whitespace and commas */
        while (*p == ' ' || *p == ',') p++;
        if (!*p) break;

        if (p[0] == '0' && (p[1] == '3' || p[1] == '4') &&
            (p[2] == '\0' || p[2] == ',' || p[2] == ' '))
        {
            if (n >= cap) return -1;
            out[n++] = (uint8_t)((p[1] - '0') | 0x00);
            p += 2;
        } else {
            return -1;  /* 遇到未知或非法命令格式 / Invalid command token */
        }
    }
    return n;
}

/*
 * @brief 主程序入口
 *        Main application entry point
 */
int main(int argc, char **argv) {
    const char *port_path = NULL;

    /* 1. 解析命令行选项 Flag / Parse command-line flags */
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

    /* 2. 选定并打开串口设备 / Select and open serial port device */
    int fd = -1;
    if (port_path) {
        /* 用户命令行指定了串口路径 / Explicit path specified by user */
        fd = serial_open(port_path);
        if (fd < 0) die("serial_open 失败 (Failed to open serial port)");
    } else if (!g_monitor_cmds) {
        /* 手动菜单模式且未传路径: 弹出交互式端口选择列表 / Interactive port picker in menu mode */
        char picked[64] = {0};
        if (ui_pick_serial_port(picked, sizeof picked) < 0)
            die("未选择串口 (No serial port selected)");
        fd = serial_open(picked);
        if (fd < 0) die("serial_open 失败 (Failed to open serial port)");
    } else {
        /* 监控模式必须显式指定串口设备 / Monitoring mode requires explicit port argument */
        usage(argv[0]);
        return 1;
    }

    fprintf(stderr, "[ok] 已打开端口 fd=%d, --debug=%d, 模式=%s\n",
            fd, bms_debug,
            g_monitor_cmds ? "monitor" : "menu");
    fflush(stderr);

    /* 设置 stdout 和 stderr 为行缓冲, 保证终端输出即时刷新 / Line buffering for timely terminal output */
    setvbuf(stdout, NULL, _IOLBF, 0);
    setvbuf(stderr, NULL, _IOLBF, 0);

    /* 3. 配置串口参数: 9600 波特率, 8-N-1 / Configure serial port: 9600 baud, 8-N-1 */
    if (serial_config(fd, B9600) < 0)
        die("serial_config 失败 (Failed to configure serial port)");

    /* 4. 根据模式分发执行 / Dispatch execution according to mode */
    int rc = 0;
    if (g_monitor_cmds) {
        /* 长期监控模式 / Long-term Monitoring Mode */
        uint8_t cmds[4] = {0};
        int n_cmds = parse_monitor_list(g_monitor_cmds, cmds, 4);
        if (n_cmds <= 0) {
            fprintf(stderr,
                    "[fatal] --monitor 参数错, 必须是 03/04 逗号分隔 (Invalid --monitor commands, must be 03/04)\n");
            serial_close(fd);
            return 1;
        }
        rc = bms_run_monitor(fd, cmds, n_cmds, g_monitor_interval);
        serial_close(fd);
    } else {
        /* 手动交互菜单模式 / Manual Interactive Menu Mode */
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
