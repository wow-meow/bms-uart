#ifndef BMS_MONITOR_H
#define BMS_MONITOR_H

#include <stdint.h>
#include <stdio.h>     /* FILE* */

/*
 * 长期监控模式 -- 跟手动菜单共享串口但走独立 CSV (logs/monitoring/)
 *
 * 两层 API:
 *
 *   bms_poll_loop() -- 内部循环体, 接已打开的 FILE* (监控 CSV).
 *                      只响应 SIGINT + 用户输入 'q' 退出.
 *                      不做信号/sigaction 自装, 让上层决定.
 *                      返回总记录条数, -1 错.
 *
 *   bms_run_monitor() -- 顶层: CLI 路由用.
 *                        自己装 SIGINT 钩 + 开 logs/monitoring/ 下的 CSV +
 *                        调 bms_poll_loop + fclose.
 *                        返回 0 正常退出, -1 入参错.
 *
 * 都假设: cmds 元素是协议号 (0x03 / 0x04), cmd 数 1..3, interval_sec 1..3600.
 * 内部 bms_read_* 有 4 次自动重试, 仍超时则跳过本行 CSV.
 */

/* 跑轮询循环. f_basic/f_cells 是 logs/monitoring/ 已 append 打开的 FILE*,
 * 不需要这套的可以传 NULL.
 * 停止条件: SIGINT 或 stdin 收到首字符 'q'/'Q'.
 * 返回成功写入的记录条数, 入参非法返回 -1.
 */
int bms_poll_loop(int fd,
                  const uint8_t *cmds, int n_cmds,
                  int interval_sec,
                  FILE *f_basic, FILE *f_cells);

/* CLI 顶层入口 --monitor ... 用这个. */
int bms_run_monitor(int fd,
                    const uint8_t *cmds, int n_cmds,
                    int interval_sec);

#endif /* BMS_MONITOR_H */
