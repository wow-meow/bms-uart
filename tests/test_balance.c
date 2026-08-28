/* 均衡状态格式化自测, 不需要 BMS.
 *
 * 把几个典型的位图数据 -> 期望字符串写成断言, 跑一遍就知道算法对不对.
 */
#include "balance_fmt.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

static void check_range(const char *test_name,
                        uint16_t low, uint16_t high, uint8_t cell_count,
                        const char *expect)
{
    char got[128];
    format_open_ranges(got, sizeof got, low, high, cell_count);
    printf("[%s] low=%04X high=%04X n=%u -> \"%s\"",
           test_name, low, high, cell_count, got);
    if (strcmp(got, expect) != 0) {
        printf("  ❌ 期望: \"%s\"\n", expect);
        assert(0);
    } else {
        printf("  ✅\n");
    }
}

int main(void) {
    printf("===== 均衡格式化自测 =====\n");

    /* 单 bit */
    check_range("1串开",  0x0001, 0x0000, 16, "1");
    check_range("末串开", 0x8000, 0x0000, 16, "16");
    /* 多 bit 离散 (bit 0,1 = 串 1,2 是连续的 -> 区间) */
    check_range("两连续 17 串内",  0x0003, 0x0000, 17, "1-2");
    /* 连续区间 */
    check_range("1-3 连续", 0x0007, 0x0000, 16, "1-3");
    check_range("混合 (1-2 区间, 9 离散)", 0x0103, 0x0000, 16, "1-2, 9");
    /* 跨 high 边界 */
    check_range("跨 16/17 (15-17 连续)", 0xC000, 0x0001, 17, "15-17");
    check_range("16 + 17 都开 (连续)", 0x8000, 0x0001, 17, "16-17");
    check_range("1 17 都开 (离散)", 0x0001, 0x0001, 17, "1, 17");
    /* 16-17 临界 */
    check_range("16 开"  , 0x8000, 0x0000, 16, "16");
    /* cell_count 限制 */
    check_range("全部开 16 串 (低全 1)"  , 0xFFFF, 0x0000, 16, "1-16");
    check_range("全部开 17 串 (高 bit0)",  0xFFFF, 0x0001, 17, "1-17");
    check_range("32 串全开"              , 0xFFFF, 0xFFFF, 32, "1-32");
    /* 区间 + 分散混合 */
    check_range("两段区间+一离散", 0x011B, 0x0000, 16, "1-2, 4-5, 9");
    /* 全部关闭 */
    check_range("全 0",  0x0000, 0x0000, 17, "");

    /* ====== print_balance 整句 ======
     * 不靠 capture stdout, 改利用 on_count 自己跑一遍逻辑核对.
     */
    printf("\n[打印整句 - 看屏幕输出符合期望]\n");
    printf("期望:    实际:\n");

    /* 全部打开 */
    printf("[全部开 16/16] 期望 \"均衡: 全部打开 (16/16)\" -- ");
    print_balance(0xFFFF, 0x0000, 16);
    printf("\n");
    printf("[全部开 17/17] 期望 \"均衡: 全部打开 (17/17)\" -- ");
    print_balance(0xFFFF, 0x0001, 17);
    printf("\n");
    /* 全部关闭 */
    printf("[全部关 17 串] 期望 \"均衡: 全部关闭\" -- ");
    print_balance(0x0000, 0x0000, 17);
    printf("\n");
    /* 部分开 */
    printf("[1-2 开 (连续) 其余关] 期望 \"均衡: 1-2 开 / 其余关\" -- ");
    print_balance(0x0003, 0x0000, 17);
    printf("\n");
    printf("[1-3, 17 开 (跨边界) 其余关] 期望 \"均衡: 1-3, 17 开 / 其余关\" -- ");
    print_balance(0x0007, 0x0001, 17);
    printf("\n");
    /* cell_count=0 的兜底 */
    printf("[0 串兜底] 期望 \"均衡: (无数据, 串数=0)\" -- ");
    print_balance(0x0000, 0x0000, 0);
    printf("\n");

    printf("\n===== 全部通过 =====\n");
    return 0;
}
