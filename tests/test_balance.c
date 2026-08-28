/*
 * test_balance.c — 电池均衡状态格式化单元测试 / Battery Balancing Formatter Unit Test
 *
 * 本测试独立验证 balance_fmt.c 的算法逻辑, 无需连接实体 BMS 硬件.
 * Independently validates bitmap range formatting in balance_fmt.c without BMS hardware.
 *
 * 测试覆盖点 / Test Coverage:
 *   - 单 bit 开启 (首串 / 末串) / Single bit active (first / last cell)
 *   - 连续区间合并 (如 "1-3") / Consecutive ranges (e.g. "1-3")
 *   - 离散与连续混合 (如 "1-2, 9") / Mixed discrete & range cells (e.g. "1-2, 9")
 *   - 跨 16 串边界跨越 (low 16 与 high 16 拼接, 如 "15-17") / Boundary crossing across cell 16/17 ("15-17")
 *   - 电池总串数限制与全开/全关 / Total cell count bounds, all active, and all inactive
 *   - print_balance 整句文案输出校验 / Full summary sentence display check
 */

#include "balance_fmt.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

/*
 * @brief 单条断言辅助函数: 验证 format_open_ranges(low, high, cell_count) 输出与期望字符串完全匹配
 *        Assertion helper: verifies format_open_ranges output strictly matches expected string
 */
static void check_range(const char *test_name,
                        uint16_t low, uint16_t high, uint8_t cell_count,
                        const char *expect)
{
    char got[128];
    format_open_ranges(got, sizeof got, low, high, cell_count);
    printf("[%s] low=%04X high=%04X n=%u -> \"%s\"",
           test_name, low, high, cell_count, got);
    if (strcmp(got, expect) != 0) {
        printf("  ❌ 期望 (Expected): \"%s\"\n", expect);
        assert(0);
    } else {
        printf("  ✅\n");
    }
}

int main(void) {
    printf("===== 均衡格式化自测 / Battery Balance Formatting Test =====\n");

    /* 1. 单 bit 开启 / Single bit */
    check_range("1串开",  0x0001, 0x0000, 16, "1");
    check_range("末串开", 0x8000, 0x0000, 16, "16");

    /* 2. 多 bit 离散与连续区间 / Multiple bits: discrete & consecutive */
    check_range("两连续 17 串内",  0x0003, 0x0000, 17, "1-2");
    check_range("1-3 连续", 0x0007, 0x0000, 16, "1-3");
    check_range("混合 (1-2 区间, 9 离散)", 0x0103, 0x0000, 16, "1-2, 9");

    /* 3. 跨 low/high 边界 (16/17 串拼接) / Crossing low/high 16-bit word boundary */
    check_range("跨 16/17 (15-17 连续)", 0xC000, 0x0001, 17, "15-17");
    check_range("16 + 17 都开 (连续)", 0x8000, 0x0001, 17, "16-17");
    check_range("1 17 都开 (离散)", 0x0001, 0x0001, 17, "1, 17");
    check_range("16 开", 0x8000, 0x0000, 16, "16");

    /* 4. 总串数限制与全开 / Cell count bounds and all-active cases */
    check_range("全部开 16 串 (低全 1)",  0xFFFF, 0x0000, 16, "1-16");
    check_range("全部开 17 串 (高 bit0)",  0xFFFF, 0x0001, 17, "1-17");
    check_range("32 串全开",              0xFFFF, 0xFFFF, 32, "1-32");

    /* 5. 多段复杂区间混合 / Complex multi-range combination */
    check_range("两段区间+一离散", 0x011B, 0x0000, 16, "1-2, 4-5, 9");

    /* 6. 全部关闭 / All closed */
    check_range("全 0",  0x0000, 0x0000, 17, "");

    /* ====== print_balance 整句打印视觉验证 / Full sentence print verification ====== */
    printf("\n[打印整句 - 看屏幕输出符合期望 / Visual check for sentence printing]\n");
    printf("期望 (Expected): -> 实际 (Actual):\n");

    /* 全部打开 / All active */
    printf("[全部开 16/16] 期望 \"均衡: 全部打开 (16/16)\" -- ");
    print_balance(0xFFFF, 0x0000, 16);
    printf("\n");
    printf("[全部开 17/17] 期望 \"均衡: 全部打开 (17/17)\" -- ");
    print_balance(0xFFFF, 0x0001, 17);
    printf("\n");

    /* 全部关闭 / All closed */
    printf("[全部关 17 串] 期望 \"均衡: 全部关闭\" -- ");
    print_balance(0x0000, 0x0000, 17);
    printf("\n");

    /* 部分开启 / Partially active */
    printf("[1-2 开 (连续) 其余关] 期望 \"均衡: 1-2 开 / 其余关\" -- ");
    print_balance(0x0003, 0x0000, 17);
    printf("\n");
    printf("[1-3, 17 开 (跨边界) 其余关] 期望 \"均衡: 1-3, 17 开 / 其余关\" -- ");
    print_balance(0x0007, 0x0001, 17);
    printf("\n");

    /* cell_count=0 兜底 / Zero cell count fallback */
    printf("[0 串兜底] 期望 \"均衡: (无数据, 串数=0)\" -- ");
    print_balance(0x0000, 0x0000, 0);
    printf("\n");

    printf("\n===== 全部通过 / All Passed =====\n");
    return 0;
}
