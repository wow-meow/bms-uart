/*
 * prot_fmt.h — 保护状态位图格式化接口 / Protection Status Bitmap Formatting Interface
 * (实现 / Implementation: src/prot_fmt.c)
 */

#ifndef BMS_PROT_FMT_H
#define BMS_PROT_FMT_H

#include <stdint.h>
#include <stddef.h>

/*
 * 保护状态位图人话化打印 (per 协议 V4 PDF 第 2 页 "注 1: 保护状态说明").
 * Human-readable formatting for protection status bitmap (per Protocol V4 Doc p.2 "Note 1: Protection Status").
 *
 *   format_protection_status: 把结果写到 buf (用户自备 buffer, 截断安全, 末尾自动补 '\n').
 *                             Formats full description with prefix & hex into buf with trailing newline.
 *   print_protection_status : 调用 format 后把 buf 打到 stdout.
 *                             Prints formatted protection sentence directly to stdout.
 *   format_protection_names : 仅写出逗号分隔的保护名称列表 (用于 CSV).
 *                             Formats only comma-separated protection names without hex/newline (for CSV).
 *
 *   bits == 0  -> "保护 0x0000\n"
 *   bits != 0  -> "保护 0x0XXX: 名字1, 名字2, ...\n"
 *
 * 仅 bit 0..12 有定义 (按 PDF), 13..15 视为保留, 不显示名字.
 * Only bits 0..12 are defined by protocol; bits 13..15 are reserved and omitted from name lists.
 */

/**
 * @brief 格式化保护状态整句 (带 0xXXXX 前缀和末尾换行)
 *        Format full protection status sentence into buffer (with 0xXXXX prefix and trailing newline)
 * @param buf 输出缓冲区 / Output buffer
 * @param cap 缓冲区容量 / Buffer capacity
 * @param bits 保护状态 16 位位图 / 16-bit protection status bitmap
 */
void format_protection_status(char *buf, size_t cap, uint16_t bits);

/**
 * @brief 打印保护状态整句到 stdout
 *        Print full protection status sentence to stdout
 * @param bits 保护状态 16 位位图 / 16-bit protection status bitmap
 */
void print_protection_status(uint16_t bits);

/**
 * @brief 仅格式化保护名称列表 (无 hex 前缀、无末尾换行, 用于监控 CSV)
 *        Format triggered protection names only (no hex prefix, no newline; for monitoring CSV)
 *        bits == 0  -> ""
 *        bits != 0  -> "单体过压, 充电过流"
 * @param buf 输出缓冲区 / Output buffer
 * @param cap 缓冲区容量 / Buffer capacity
 * @param bits 保护状态 16 位位图 / 16-bit protection status bitmap
 */
void format_protection_names(char *buf, size_t cap, uint16_t bits);

#endif /* BMS_PROT_FMT_H */
