/*
 * balance_fmt.c — BMS 电池均衡位图 → 人类可读串号文本
 *                 BMS Battery Balance Bitmap -> Human-readable Cell Range Formatter
 *
 * 协议 V4 第 2 页用两个 16-bit 字描述最多 32 串的被动均衡开关状态:
 * Protocol V4 page 2 uses two 16-bit words to describe passive balance status for up to 32 cells:
 *   balance_low  bit0..15 -> 第 1..16 串 / Cells 1..16
 *   balance_high bit0..15 -> 第 17..32 串 / Cells 17..32
 *   1 = 该串均衡开启 (Balancing active), 0 = 关闭 (Inactive)
 *
 * 本文件将这两个 16 位位图转换压缩为简明区间字符串 (如 "1-3, 17 开 / 其余关"), 供 UI 和日志展示.
 * This file compresses balance bitmaps into compact range strings (e.g. "1-3, 17 ON / others OFF").
 */

#include "balance_fmt.h"
#include <stdio.h>
#include <stddef.h>

/*
 * @brief 读取第 i 串 (0-based) 的均衡 bit
 *        Read the balancing bit for cell index i (0-based)
 *
 * i < 16 索引对应 balance_low, i >= 16 对应 balance_high.
 * Index i < 16 accesses balance_low; i >= 16 accesses balance_high.
 */
int bal_bit(uint16_t low, uint16_t high, uint8_t i) {
    if (i < 16) return (low  >> i) & 1;
    return (high >> (i - 16)) & 1;
}

/*
 * @brief 把所有处于「开启」状态的电池串号压缩成区间字符串写入 buf (截断安全)
 *        Compress all active balancing cell numbers into range string in buf (truncation-safe)
 *
 * 连续开启的串号合并为 "a-b", 孤立串号写为 "a", 多个区间用 ", " 分隔.
 * 串号为 1-based (与协议文档一致).
 * Consecutive active cells are merged into "a-b", isolated cells as "a", separated by ", ".
 * Cell numbers are 1-based matching the protocol spec.
 *
 * 例 / Example: bits 0,1,2,16 开启 -> "1-3, 17"
 */
void format_open_ranges(char *buf, size_t cap,
                        uint16_t low, uint16_t high,
                        uint8_t cell_count)
{
    if (cap == 0) return;
    buf[0] = '\0';
    size_t off = 0;       /* 当前写入偏移 / Current write offset in buf */
    int in_run = 0;       /* 是否正处于连续开启区间内 / Whether currently inside a continuous active run */
    uint8_t run_start = 0; /* 当前连续区间的起始串号 (1-based) / Starting cell number of current run (1-based) */

    /* 把刚结束的 run (单串或区间) 写入 buf / Append the finished run (single cell or range) to buf */
    #define APPEND_RUN(end_idx) do {                                        \
        if (in_run) {                                                       \
            if (run_start == (end_idx)) {                                   \
                /* 单串: "N" / Single isolated cell */                      \
                off += snprintf(buf + off, cap - off, "%s%u",               \
                                off ? ", " : "", run_start);                \
            } else {                                                        \
                /* 连续区间: "A-B" / Consecutive range */                    \
                off += snprintf(buf + off, cap - off, "%s%u-%u",            \
                                off ? ", " : "", run_start, (end_idx));     \
            }                                                               \
            in_run = 0;                                                     \
        }                                                                   \
    } while (0)

    /* 扫描有效串 (最多 32), 遇「打开→关闭」或到达末尾时收束当前 run /
     * Scan active cells (up to 32); flush run when transitioning active->inactive or at end */
    for (uint8_t i = 0; i < cell_count && i < 32; i++) {
        if (bal_bit(low, high, i)) {
            if (!in_run) {
                in_run = 1;
                run_start = (uint8_t)(i + 1);     /* 1-based */
            }
            /* 判断当前 i 是否为当前连续区间的最后一串 / Check if current i is the end of this run */
            uint8_t next = (uint8_t)(i + 1);
            int run_end_here = (next >= cell_count) ||
                               (next >= 32) ||
                               !bal_bit(low, high, next);
            if (run_end_here) APPEND_RUN(i + 1);
        }
    }
    #undef APPEND_RUN
}

/*
 * @brief 打印一行均衡状态摘要到 stdout
 *        Print a one-line balance summary to stdout
 *
 * 特殊情况优先处理 / Special cases handled first:
 *   cell_count 非法 / Invalid cell count -> "均衡: (无数据, 串数=N)"
 *   全部开启 / All active               -> "均衡: 全部打开 (N/N)"
 *   全部关闭 / All closed               -> "均衡: 全部关闭"
 *   部分开启 / Partially active         -> "均衡: 1-3, 17 开 / 其余关"
 */
void print_balance(uint16_t low, uint16_t high, uint8_t cell_count) {
    if (cell_count == 0 || cell_count > 32) {
        printf("均衡: (无数据, 串数=%u)", cell_count);
        return;
    }

    /* 统计开启的串数, 决定输出文案分支 / Count active cells to choose output wording */
    uint8_t on = 0;
    for (uint8_t i = 0; i < cell_count; i++)
        if (bal_bit(low, high, i)) on++;

    if ((int)on == cell_count) {
        printf("均衡: 全部打开 (%u/%u)", cell_count, cell_count);
        return;
    }
    if (on == 0) {
        printf("均衡: 全部关闭");
        return;
    }

    char ranges[128];
    format_open_ranges(ranges, sizeof ranges, low, high, cell_count);
    printf("均衡: %s 开 / 其余关", ranges);
}
