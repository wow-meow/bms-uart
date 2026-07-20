#ifndef BMS_UI_H
#define BMS_UI_H

#include <stdio.h>
#include "bms.h"

/* 扫描 /dev/ttyUSB* / /dev/ttyACM*, 让用户选一个.
 * 成功把选中的路径写入 out (cap 大小), 返回 0; 失败返回 -1.
 * 用户取消 (直接回车) 也算失败.
 */
int  ui_pick_serial_port(char *out, size_t cap);

/* 打印菜单 */
void ui_print_menu(void);

/* 主循环入口: 读一行 -> 解析 -> 分发命令 -> 打印 + 追加 CSV. */
int  ui_run_menu(int fd, FILE *f_basic, FILE *f_cells, FILE *f_hwver);

#endif /* BMS_UI_H */
