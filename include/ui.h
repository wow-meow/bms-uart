/*
 * ui.h — 交互式菜单接口 / Interactive Menu UI Interface
 * (实现 / Implementation: src/ui.c)
 *
 * 包含端口选择器及菜单主循环.
 * Contains serial port selector and interactive menu loop.
 */

#ifndef BMS_UI_H
#define BMS_UI_H

#include <stdio.h>
#include "bms.h"

/**
 * @brief 扫描 /dev/ttyUSB* / /dev/ttyACM* 串口设备并让用户交互式选择或手动输入路径
 *        Scan /dev/ttyUSB* & /dev/ttyACM* ports and let user interactively select or type path
 * @param out 输出设备路径缓冲区 / Output buffer for selected device path
 * @param cap 缓冲区容量 / Buffer capacity
 * @return 0 成功, -1 失败或取消 / 0 on success, -1 on failure/cancellation
 */
int  ui_pick_serial_port(char *out, size_t cap);

/**
 * @brief 打印主菜单选项到 stdout
 *        Print main interactive menu options to stdout
 */
void ui_print_menu(void);

/**
 * @brief 运行交互式主菜单循环 (读取用户选择 -> 分发 BMS 查询命令 -> 打印结果并写入 CSV)
 *        Run interactive menu loop (reads user input -> dispatches BMS queries -> prints & logs to CSV)
 * @param fd 已打开配置好的串口描述符 / Configured serial file descriptor
 * @param f_basic 基本信息 CSV 文件句柄 / FILE pointer for basic info CSV
 * @param f_cells 单体电压 CSV 文件句柄 / FILE pointer for cell voltages CSV
 * @param f_hwver 硬件版本 CSV 文件句柄 / FILE pointer for hardware version CSV
 * @return 0 正常退出 / 0 on normal exit
 */
int  ui_run_menu(int fd, FILE *f_basic, FILE *f_cells, FILE *f_hwver);

#endif /* BMS_UI_H */
