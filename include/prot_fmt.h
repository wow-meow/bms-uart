#ifndef BMS_PROT_FMT_H
#define BMS_PROT_FMT_H

#include <stdint.h>
#include <stddef.h>

/*
 * 保护状态位图人话化打印 (per 协议 V4 PDF 第 2 页 "注 1: 保护状态说明").
 *
 *   format_protection_status: 把结果写到 buf (用户自备 buffer, 截断安全).
 *                             末尾自动补 '\n'.
 *   print_protection_status : 调用 format 后把 buf 打到 stdout.
 *
 *   bits == 0  -> "保护 0x0000\n"
 *   bits != 0  -> "保护 0x0XXX: 名字1, 名字2, ...\n"
 *
 * 仅 bit 0..12 有定义 (按 PDF), 13..15 视为保留, 不显示.
 */

/* 写到 buf, 长度上限 cap, 截断安全 */
void format_protection_status(char *buf, size_t cap, uint16_t bits);

/* 整句打到 stdout */
void print_protection_status(uint16_t bits);

/*
 * 仅写出保护名 (无 hex 无前缀), 用于监控 CSV 的 triggered_protection 列.
 *   bits == 0  -> ""
 *   bits != 0  -> "name1, name2, name3"  (末尾无逗号, 无 \n)
 */
void format_protection_names(char *buf, size_t cap, uint16_t bits);

#endif /* BMS_PROT_FMT_H */
