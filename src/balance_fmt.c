#include "balance_fmt.h"
#include <stdio.h>
#include <stddef.h>

int bal_bit(uint16_t low, uint16_t high, uint8_t i) {
    if (i < 16) return (low  >> i) & 1;
    return (high >> (i - 16)) & 1;
}

void format_open_ranges(char *buf, size_t cap,
                        uint16_t low, uint16_t high,
                        uint8_t cell_count)
{
    if (cap == 0) return;
    buf[0] = '\0';
    size_t off = 0;
    int in_run = 0;
    uint8_t run_start = 0;

    /* 把刚结束的 run (单串或区间) 写入 buf */
    #define APPEND_RUN(end_idx) do {                                        \
        if (in_run) {                                                       \
            if (run_start == (end_idx)) {                                   \
                off += snprintf(buf + off, cap - off, "%s%u",               \
                                off ? ", " : "", run_start);                \
            } else {                                                        \
                off += snprintf(buf + off, cap - off, "%s%u-%u",            \
                                off ? ", " : "", run_start, (end_idx));     \
            }                                                               \
            in_run = 0;                                                     \
        }                                                                   \
    } while (0)

    for (uint8_t i = 0; i < cell_count && i < 32; i++) {
        if (bal_bit(low, high, i)) {
            if (!in_run) {
                in_run = 1;
                run_start = (uint8_t)(i + 1);     /* 1-based */
            }
            /* 当前 i 是这一段运行的最后一串? (范围内最后, 或下一串是 0) */
            uint8_t next = (uint8_t)(i + 1);
            int run_end_here = (next >= cell_count) ||
                               (next >= 32) ||
                               !bal_bit(low, high, next);
            if (run_end_here) APPEND_RUN(i + 1);
        }
    }
    #undef APPEND_RUN
}

void print_balance(uint16_t low, uint16_t high, uint8_t cell_count) {
    if (cell_count == 0 || cell_count > 32) {
        printf("均衡: (无数据, 串数=%u)", cell_count);
        return;
    }

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
