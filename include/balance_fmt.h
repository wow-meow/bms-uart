/*
 * balance_fmt.h — 电池均衡位图格式化接口 / Battery Balance Bitmap Formatting Interface
 * (实现 / Implementation: src/balance_fmt.c)
 */

#ifndef BMS_BALANCE_FMT_H
#define BMS_BALANCE_FMT_H

#include <stdint.h>
#include <stddef.h>

/*
 * 把电池均衡位图 (per protocol V4 page 2) 格式化成人类可读的串号范围.
 * Format battery balance bitmap (per Jiabaida Protocol V4 Page 2) into human-readable cell range strings.
 *
 *   low    : 16 bit, bit i (0..15) 对应 (i+1) 串均衡 / bit i (0..15) corresponds to cell (i+1) balance
 *   high   : 16 bit, bit i (0..15) 对应 (i+16) 串均衡 / bit i (0..15) corresponds to cell (i+16) balance
 *   n_total: 实际串数 (1..32), 决定哪些 bit 有效 / Total valid cell count (1..32)
 *
 * 例 / Example (假设 17 串, balance_low=0x0003, balance_high=0x0001):
 *   串 1, 2 开 (bit 0,1 of low) + 串 17 开 (bit 0 of high) / Cell 1, 2 ON + Cell 17 ON
 *   范围 / Formatted Range: "1-2, 17"
 */

/**
 * @brief 单 bit 查询 / Query balance status for a single cell (0-based index)
 * @param low 低 16 串均衡位图 / Lower 16-cell balance bitmap (cells 1-16)
 * @param high 高 16 串均衡位图 / Higher 16-cell balance bitmap (cells 17-32)
 * @param i 电池串索引 (0-based, 0 表示第 1 串) / Cell index (0-based, 0 = cell 1)
 * @return 1 表示均衡开启, 0 表示关闭 / 1 if balancing is active, 0 if closed
 */
int bal_bit(uint16_t low, uint16_t high, uint8_t i);

/**
 * @brief 把开启的 bit 转成 "1, 2, 5-8, 12" 形式写入 buf (截断安全)
 *        Format active balancing bits into ranges like "1, 2, 5-8, 12" into buffer (truncation-safe)
 * @param buf 输出目标缓冲区 / Destination string buffer
 * @param cap 缓冲区容量 / Buffer capacity in bytes
 * @param low 低 16 串均衡位图 / Lower 16-cell balance bitmap
 * @param high 高 16 串均衡位图 / Higher 16-cell balance bitmap
 * @param cell_count 电池总串数 / Total cell count
 */
void format_open_ranges(char *buf, size_t cap,
                        uint16_t low, uint16_t high,
                        uint8_t cell_count);

/**
 * @brief 打印均衡状态整句到 stdout
 *        Print complete human-readable balance summary sentence to stdout
 * @param low 低 16 串均衡位图 / Lower 16-cell balance bitmap
 * @param high 高 16 串均衡位图 / Higher 16-cell balance bitmap
 * @param cell_count 电池总串数 / Total cell count
 */
void print_balance(uint16_t low, uint16_t high, uint8_t cell_count);

#endif /* BMS_BALANCE_FMT_H */
