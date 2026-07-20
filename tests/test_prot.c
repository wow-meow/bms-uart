/* 保护状态格式化自测, 不依赖 BMS 硬件.
 *
 * 调用 format_protection_status (buffer 版), 直接 strcmp 期望输出.
 * 比 stdout 重定向简单可靠.
 */
#include "../inc/prot_fmt.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

/* 行缓冲, 测试失败时差异能立刻打到屏幕 */
static void __attribute__((constructor)) setup_buffers(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    setvbuf(stderr, NULL, _IOLBF, 0);
}

static void check(const char *test_name, uint16_t bits, const char *expect) {
    char got[256];
    format_protection_status(got, sizeof got, bits);
    printf("[%-25s] bits=0x%04X\n  期望: \"%s\"\n  实际: \"%s\"\n",
           test_name, bits, expect, got);
    if (strcmp(got, expect) != 0) {
        printf("  ❌\n");
        fflush(stdout);
        assert(0);
    }
    printf("  ✅\n");
}

int main(void) {
    printf("===== 保护状态格式化自测 =====\n");

    /* 全 0 -> 只有 hex */
    check("全 0",                0x0000, "保护 0x0000\n");

    /* 单 bit (13 个) */
    check("单体过压 (bit0)",     0x0001, "保护 0x0001: 单体过压\n");
    check("单体欠压 (bit1)",     0x0002, "保护 0x0002: 单体欠压\n");
    check("整组过压 (bit2)",     0x0004, "保护 0x0004: 整组过压\n");
    check("整组欠压 (bit3)",     0x0008, "保护 0x0008: 整组欠压\n");
    check("充电过温 (bit4)",     0x0010, "保护 0x0010: 充电过温\n");
    check("充电低温 (bit5)",     0x0020, "保护 0x0020: 充电低温\n");
    check("放电过温 (bit6)",     0x0040, "保护 0x0040: 放电过温\n");
    check("放电低温 (bit7)",     0x0080, "保护 0x0080: 放电低温\n");
    check("充电过流 (bit8)",     0x0100, "保护 0x0100: 充电过流\n");
    check("放电过流 (bit9)",     0x0200, "保护 0x0200: 放电过流\n");
    check("短路 (bit10)",        0x0400, "保护 0x0400: 短路\n");
    check("前端IC错误 (bit11)",  0x0800, "保护 0x0800: 前端IC错误\n");
    check("软件锁MOS (bit12)",   0x1000, "保护 0x1000: 软件锁MOS\n");

    /* 多 bit */
    /* 0x018A = 0x0002 (bit1 单体欠压) + 0x0008 (bit3 整组欠压)
     *        + 0x0080 (bit7 放电低温) + 0x0100 (bit8 充电过流)
     */
    check("0x018A 多 bit",       0x018A,
          "保护 0x018A: 单体欠压, 整组欠压, 放电低温, 充电过流\n");
    /* 0x0403 = 0x0001 (bit0) + 0x0002 (bit1) + 0x0400 (bit10) */
    check("单过压+单欠压+短路",  0x0403,
          "保护 0x0403: 单体过压, 单体欠压, 短路\n");
    check("多个分散",            0x1103,
          "保护 0x1103: 单体过压, 单体欠压, 充电过流, 软件锁MOS\n");

    /* 全部 13 位 */
    check("13 位全置位",         0x1FFF,
          "保护 0x1FFF: 单体过压, 单体欠压, 整组过压, 整组欠压, 充电过温, 充电低温, 放电过温, 放电低温, 充电过流, 放电过流, 短路, 前端IC错误, 软件锁MOS\n");

    /* 保留位 (bit 13/14/15) 在协议 V4 里没定义, 当前实现不显示名字 */
    check("只保留位 0xE000",     0xE000, "保护 0xE000\n");
    check("保留位混已知",        0xE001, "保护 0xE001: 单体过压\n");

    printf("\n===== 全部通过 =====\n");
    return 0;
}
