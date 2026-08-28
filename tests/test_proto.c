/* protocol 层自测, 对照 PDF 例帧.
 *
 * PDF 帧结构表 (第 1 页):
 *   cs = ~(sum of [DATA + LEN + CMD]) + 1,  高字节在前
 *
 * 例 1 -- 主机发送 (PDF 第 1 页):
 *   字节: DD A5 03 00 FF FD 77
 *   sum  = bytes [2..3] = CMD(0x03) + LEN(0x00) = 0x03
 *   cs   = ~0x03 + 1 = 0xFFFD
 *
 * 例 2 -- BMS 响应 (PDF 第 1 页):
 *   字节: DD 03 00 1B [27 data] FB FF 77
 *   sum  = bytes [2..30] = STATUS(0x00) + LEN(0x1B) + DATA
 *   cs   = ~sum + 1 = 0xFBFF
 */
#include "protocol.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

static void hexdump(const uint8_t *p, size_t n) {
    for (size_t i = 0; i < n; i++) printf("%02X ", p[i]);
    printf("\n");
}

/* ============= 测试 1: PDF 主机发送例帧的校验码 ============= */
static int test_checksum_host_example(void) {
    /* bytes [2..3] = CMD(0x03) + LEN(0x00) */
    const uint8_t cmd_then_len[] = { 0x03, 0x00 };
    uint16_t cs = proto_checksum(cmd_then_len, sizeof cmd_then_len);
    uint8_t cs_be[2] = { (uint8_t)(cs >> 8), (uint8_t)(cs & 0xFF) };
    printf("[1] CMD+LEN = 03 00 -> cs=%04X, send=[ ", cs);
    hexdump(cs_be, 2);
    printf("    期望 cs=FFFD, send=[ FF FD ]\n");
    assert(cs == 0xFFFD);

    /* 用 pack 接口组装整个 7 字节请求 */
    uint8_t pkt[7] = {0};
    size_t n = proto_pack_read(pkt, sizeof pkt, 0x03);
    assert(n == 7);
    printf("[1] pack_read(0x03) -> ");
    hexdump(pkt, n);
    printf("    期望: DD A5 03 00 FF FD 77\n");
    assert(memcmp(pkt, "\xDD\xA5\x03\x00\xFF\xFD\x77", 7) == 0);
    return 0;
}

/* ============= 测试 2: PDF BMS 响应例帧的校验与 validate ============= */
static int test_validate_bms_response(void) {
    /* PDF 第 1 页 BMS 响应例帧 */
    const uint8_t resp[] = {
        0xDD, 0x03, 0x00, 0x1B,
        0x17, 0x00, 0x00, 0x00, 0x02, 0xD0, 0x03, 0xE8,
        0x00, 0x00, 0x20, 0x78,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x48,
        0x03, 0x0F, 0x02, 0x0B, 0x76, 0x0B, 0x82,
        0xFB, 0xFF, 0x77
    };
    size_t rlen = sizeof resp;

    /* bytes [2..30] = STATUS+LEN+DATA, 长度 = data_len + 2 = 29 字节 */
    uint16_t cs_calc = proto_checksum(resp + 2, (size_t)resp[3] + 2);
    uint8_t cs_calc_be[2] = { (uint8_t)(cs_calc >> 8), (uint8_t)(cs_calc & 0xFF) };
    printf("[2] RESP bytes[2..30] -> cs=%04X, frame=[ ", cs_calc);
    hexdump(cs_calc_be, 2);
    printf("    期望 cs=FBFF, frame=[ FB FF ]\n");
    assert(cs_calc == 0xFBFF);

    /* validate 应该认可这帧 */
    bms_response_t out = {0};
    bms_err_t e = proto_validate(resp, rlen, 0x03, &out);
    printf("[2] validate: err=%d (期望 0), cmd=%02X, status=%02X, data_len=%u\n",
           e, out.cmd, out.status, out.data_len);
    assert(e == BMS_OK);
    assert(out.cmd == 0x03);
    assert(out.status == 0x00);
    assert(out.data_len == 0x1B);
    return 0;
}

/* ============= 测试 3: 错误状态帧 (STATUS=0x80) ============= */
static int test_validate_error_status(void) {
    /* 响应: DD 03 80 00 CS_H CS_L 77
     * sum = bytes[2..3] = 0x80 + 0x00 = 0x80
     * cs  = ~0x80 + 1 = 0xFF7F + 1 = 0xFF80  -> CS_H=0xFF, CS_L=0x80
     */
    const uint8_t err_resp[] = { 0xDD, 0x03, 0x80, 0x00, 0xFF, 0x80, 0x77 };
    bms_err_t e = proto_validate(err_resp, sizeof err_resp, 0x03, NULL);
    printf("[3] 错误状态帧 -> err=%d (期望 4 = BMS_ERR_STATUS)\n", e);
    assert(e == BMS_ERR_STATUS);
    return 0;
}

/* ============= 测试 4: 校验位被破坏, 应该报 BAD_CHECKSUM ============= */
static int test_bad_checksum(void) {
    /* 用 PDF 的响应帧, 把最后一个数据字节 0x82 改成 0x83 */
    const uint8_t good[] = {
        0xDD, 0x03, 0x00, 0x1B,
        0x17, 0x00, 0x00, 0x00, 0x02, 0xD0, 0x03, 0xE8,
        0x00, 0x00, 0x20, 0x78,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x48,
        0x03, 0x0F, 0x02, 0x0B, 0x76, 0x0B, 0x83,    /* 82 -> 83 */
        0xFB, 0xFF, 0x77
    };
    bms_err_t e = proto_validate(good, sizeof good, 0x03, NULL);
    printf("[4] 损坏数据帧 -> err=%d (期望 3 = BMS_ERR_BAD_CHECKSUM)\n", e);
    assert(e == BMS_ERR_BAD_CHECKSUM);
    return 0;
}

/* ============= 测试 5: 帧长度不符 (BAD_FRAME) ============= */
static int test_short_frame(void) {
    const uint8_t too_short[] = { 0xDD, 0x03, 0x00 };
    bms_err_t e = proto_validate(too_short, sizeof too_short, 0x03, NULL);
    printf("[5] 短帧 (3 字节) -> err=%d (期望 2 = BMS_ERR_BAD_FRAME)\n", e);
    assert(e == BMS_ERR_BAD_FRAME);
    return 0;
}

int main(void) {
    printf("===== protocol 自测 =====\n");
    int fails = 0;
    fails += test_checksum_host_example();
    fails += test_validate_bms_response();
    fails += test_validate_error_status();
    fails += test_bad_checksum();
    fails += test_short_frame();
    printf("\n===== 全部通过 (5/5) =====\n");
    return fails == 0 ? 0 : 1;
}
