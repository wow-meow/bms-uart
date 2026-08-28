/*
 * prot_fmt.c — 保护状态位图 (0x03 响应 bytes 16..17) 的人类可读格式化
 *             Protection Status Bitmap (0x03 response bytes 16..17) Human-Readable Formatter
 *
 * 位定义参考协议 V4 PDF 第 2 页 "注 1: 保护状态说明":
 * Bit definitions per Protocol V4 Doc page 2 "Note 1: Protection Status":
 *   bit 0..12  各对应一种电池保护状态 / Each bit maps to a specific fault/protection condition
 *   bit 13..15 保留未定义 / Reserved (ignored in formatting)
 *
 * 支持两种输出形态 / Two formatting variants:
 *   1. 终端整句 / Full sentence (format_protection_status / print_protection_status):
 *      "保护 0x018A: 单体欠压, 整组欠压, 放电低温, 充电过流\n"
 *   2. CSV 名单 / Comma-separated names for CSV (format_protection_names):
 *      "单体欠压, 整组欠压, 放电低温, 充电过流" (无前缀, 无换行)
 */

#include "prot_fmt.h"
#include <stddef.h>
#include <stdio.h>

/*
 * @brief 协议 V4 第 2 页保护状态 bit i (0..12) -> 中文名映射
 *        Map protection bit index (0..12) to Chinese protection name string
 */
static const char *prot_bit_name(int bit) {
    switch (bit) {
        case 0:  return "单体过压";       /* Cell over-voltage */
        case 1:  return "单体欠压";       /* Cell under-voltage */
        case 2:  return "整组过压";       /* Pack over-voltage */
        case 3:  return "整组欠压";       /* Pack under-voltage */
        case 4:  return "充电过温";       /* Charge over-temperature */
        case 5:  return "充电低温";       /* Charge under-temperature */
        case 6:  return "放电过温";       /* Discharge over-temperature */
        case 7:  return "放电低温";       /* Discharge under-temperature */
        case 8:  return "充电过流";       /* Charge over-current */
        case 9:  return "放电过流";       /* Discharge over-current */
        case 10: return "短路";           /* Short circuit */
        case 11: return "前端IC错误";     /* Front-end IC error (AFE fault) */
        case 12: return "软件锁MOS";      /* Software locked MOS */
        default: return NULL;
    }
}

/*
 * @brief 把保护状态位图格式化为整句写入 buf (截断安全, 末尾自动换行)
 *        Format protection status bitmap into full sentence in buf (truncation-safe, trailing newline)
 *
 * 输出规则 / Output Rules:
 *   bits == 0 -> "保护 0x0000\n" (无任何保护触发 / No faults triggered)
 *   bits != 0 -> "保护 0x0XXX: 名字1, 名字2, ...\n"
 */
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

/*
 * @brief 打印保护状态整句到 stdout
 *        Print full protection status sentence directly to stdout
 */
void print_protection_status(uint16_t bits) {
    char buf[256];
    format_protection_status(buf, sizeof buf, bits);
    fputs(buf, stdout);
}

/*
 * @brief 仅格式化已触发保护的中文名称列表 (无 0xXXXX 前缀, 无末尾换行, 用于监控 CSV)
 *        Format comma-separated triggered protection names only (for monitoring CSV)
 *
 *   bits == 0 -> ""
 *   bits != 0 -> "名字1, 名字2"
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
