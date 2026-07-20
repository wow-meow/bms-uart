#ifndef BMS_BALANCE_FMT_H
#define BMS_BALANCE_FMT_H

#include <stdint.h>
#include <stddef.h>

/*
 * 把电池均衡位图 (per protocol V4 page 2) 格式化成人类可读的串号范围.
 *
 *   low    : 16 bit, bit i (0..15) 对应 (i+1) 串均衡
 *   high   : 16 bit, bit i (0..15) 对应 (i+16) 串均衡
 *   n_total: 实际串数 (1..32), 决定哪些 bit 有效
 *
 * 例 (假设 17 串, balance_low=0x0003, balance_high=0x0001):
 *   串 1, 2 开 (bit 0,1 of low) + 串 17 开 (bit 0 of high)
 *   范围: "1, 2, 17"
 */

/* 单 bit 查询 */
int bal_bit(uint16_t low, uint16_t high, uint8_t i);

/* 把开启的 bit 转成 "1, 2, 5-8, 12" 形式写入 buf. 截断安全. */
void format_open_ranges(char *buf, size_t cap,
                        uint16_t low, uint16_t high,
                        uint8_t cell_count);

/* 整句打印. 写到 stdout. */
void print_balance(uint16_t low, uint16_t high, uint8_t cell_count);

#endif /* BMS_BALANCE_FMT_H */
