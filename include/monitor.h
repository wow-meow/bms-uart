/*
 * monitor.h — 长期监控模式接口 / Long-term Monitoring Mode Interface
 * (实现 / Implementation: src/monitor.c)
 *
 * 两层 API (循环体 / CLI 顶层) 的职责划分.
 * Two-level API design: polling loop body & CLI top-level entry point.
 */

#ifndef BMS_MONITOR_H
#define BMS_MONITOR_H

#include <stdint.h>
#include <stdio.h>     /* FILE* */

/*
 * 长期监控模式 -- 跟手动菜单共享串口但写独立 CSV (logs/monitoring/)
 * Long-term Monitoring Mode -- Shares serial port with manual menu, writes dedicated CSV to logs/monitoring/
 *
 * 两层 API / Two-tier API:
 *
 *   bms_poll_loop() -- 内部循环体: 接收已打开的 FILE* 句柄, 仅响应 SIGINT 和终端 'q' 退出.
 *                      Inner polling loop: accepts opened FILE* handles, exits on SIGINT or 'q' keypress.
 *
 *   bms_run_monitor() -- 顶层 CLI 入口: 自动安装 SIGINT 信号钩子, 自动创建监控 CSV,
 *                        调用 bms_poll_loop, 退出时自动关闭文件.
 *                        Top-level CLI runner: sets up SIGINT signal handler, creates CSVs,
 *                        calls bms_poll_loop, and handles file closing.
 *
 * 命令参数说明 / Command arguments:
 *   cmds 数组元素为协议命令号 (0x03 / 0x04), n_cmds 范围 1..3, interval_sec 范围 1..3600 秒.
 *   cmds array contains command bytes (0x03 / 0x04), n_cmds (1..3), interval_sec (1..3600s).
 */

/**
 * @brief 执行轮询监控主循环
 *        Execute the monitoring polling loop
 * @param fd 串口文件描述符 / Serial port file descriptor
 * @param cmds 命令号数组 (如 0x03, 0x04) / Array of command bytes (e.g. 0x03, 0x04)
 * @param n_cmds 命令数量 / Number of commands in cmds array
 * @param interval_sec 轮询间隔秒数 / Polling interval in seconds
 * @param f_basic 基本信息监控 CSV 文件句柄 (可为 NULL) / File handle for basic info CSV (or NULL)
 * @param f_cells 单体电压监控 CSV 文件句柄 (可为 NULL) / File handle for cell voltages CSV (or NULL)
 * @return 成功写入的记录条数, 入参非法返回 -1 / Total records written, or -1 on invalid argument
 */
int bms_poll_loop(int fd,
                  const uint8_t *cmds, int n_cmds,
                  int interval_sec,
                  FILE *f_basic, FILE *f_cells);

/**
 * @brief CLI 顶层监控入口 (--monitor 选项调用)
 *        Top-level CLI monitoring entry point (invoked by --monitor flag)
 * @param fd 串口文件描述符 / Serial port file descriptor
 * @param cmds 命令号数组 / Array of command bytes
 * @param n_cmds 命令数量 / Number of commands
 * @param interval_sec 轮询间隔秒数 / Polling interval in seconds
 * @return 0 正常退出, -1 失败或参数错误 / 0 on normal exit, -1 on error
 */
int bms_run_monitor(int fd,
                    const uint8_t *cmds, int n_cmds,
                    int interval_sec);

#endif /* BMS_MONITOR_H */
