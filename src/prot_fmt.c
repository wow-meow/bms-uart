#include "prot_fmt.h"
#include <stddef.h>
#include <stdio.h>

/* 协议 V4 PDF 第 2 页 "注 1: 保护状态说明"
 * bit i (i ∈ [0,12]) -> 中文名
 */
static const char *prot_bit_name(int bit) {
    switch (bit) {
        case 0:  return "单体过压";
        case 1:  return "单体欠压";
        case 2:  return "整组过压";
        case 3:  return "整组欠压";
        case 4:  return "充电过温";
        case 5:  return "充电低温";
        case 6:  return "放电过温";
        case 7:  return "放电低温";
        case 8:  return "充电过流";
        case 9:  return "放电过流";
        case 10: return "短路";
        case 11: return "前端IC错误";
        case 12: return "软件锁MOS";
        default: return NULL;
    }
}

void format_protection_status(char *buf, size_t cap, uint16_t bits) {
    if (!buf || cap == 0) return;
    int off = snprintf(buf, cap, "保护 0x%04X", bits);
    if (off < 0 || (size_t)off >= cap) { buf[cap - 1] = '\0'; return; }

    if (bits == 0) {
        snprintf(buf + off, cap - (size_t)off, "\n");
        return;
    }

    int first = 1;
    for (int b = 0; b <= 12; b++) {
        if (bits & (1u << b)) {
            const char *n = prot_bit_name(b);
            const char *sep = first ? ": " : ", ";
            int w = snprintf(buf + off, cap - (size_t)off,
                             "%s%s", sep, n ? n : "?");
            if (w < 0 || (size_t)(off + w) >= cap) break;
            off += w;
            first = 0;
        }
    }
    if ((size_t)off < cap) snprintf(buf + off, cap - (size_t)off, "\n");
}

void print_protection_status(uint16_t bits) {
    char buf[256];
    format_protection_status(buf, sizeof buf, bits);
    fputs(buf, stdout);
}

/*
 * 只抽取保护名串, 无 hex / 前缀 / 换行.
 * 给监控 CSV 的 triggered_protection 列用.
 *   bits == 0 -> ""
 *   bits != 0 -> "name1, name2, name3" (逗号分隔, 末尾干净)
 */
void format_protection_names(char *buf, size_t cap, uint16_t bits) {
    if (!buf || cap == 0) return;
    buf[0] = '\0';
    if (bits == 0) return;

    size_t off = 0;
    int first = 1;
    for (int b = 0; b <= 12; b++) {
        if (bits & (1u << b)) {
            const char *n = prot_bit_name(b);
            int w = snprintf(buf + off, cap - off,
                             "%s%s", first ? "" : ", ",
                             n ? n : "?");
            if (w < 0 || (size_t)w >= cap - off) break;
            off += (size_t)w;
            first = 0;
        }
    }
}
