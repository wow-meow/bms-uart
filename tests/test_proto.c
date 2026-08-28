/*
 * test_proto.c — 协议帧打包与解析校验单元测试 / Protocol Packing & Validation Unit Test
 *
 * 对照《嘉百达 485/UART 通用协议 V4》标准例帧进行全套算法验证.
 * Validates protocol algorithms against official example frames in Jiabaida Protocol V4 Doc (p.1).
 *
 * 帧格式与校验规则回顾 / Protocol Frame Structure & Checksum Rules:
 *   校验和 / Checksum = ~(sum of [DATA + LEN + (CMD or STATUS)]) + 1 (高字节在前 / Big-Endian)
 *
 * 测试用例列表 / Test Cases:
 *   1. 主机发送读命令例帧 (DD A5 03 00 FF FD 77) 的校验和与组帧 / Host read frame assembly & checksum
 *   2. 官方标准 27 字节 BMS 响应帧的校验与解包 (proto_validate) / Official 27-byte BMS response validation
 *   3. BMS 错误状态帧响应处理 (STATUS=0x80 -> BMS_ERR_STATUS) / Error status frame (STATUS=0x80)
 *   4. 数据损坏校验和不匹配测试 (BAD_CHECKSUM) / Corrupted payload checksum mismatch
 *   5. 畸变短帧检测 (BAD_FRAME) / Truncated/short malformed frame detection
 */

#include "protocol.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

/*
 * @brief 打印十六进制字节流辅助函数
 *        Utility helper to hexdump byte buffers
 */
static void hexdump(const uint8_t *p, size_t n) {
    for (size_t i = 0; i < n; i++) printf("%02X ", p[i]);
    printf("\n");
}

/* ============= 测试 1: 官方主机发送例帧的校验码与打包 =============
 * Test 1: Host Read Command Frame Checksum & Assembly (PDF Example) */
static int test_checksum_host_example(void) {
    /* 校验覆盖字节 bytes [2..3] = CMD(0x03) + LEN(0x00) */
    const uint8_t cmd_then_len[] = { 0x03, 0x00 };
    uint16_t cs = proto_checksum(cmd_then_len, sizeof cmd_then_len);
    uint8_t cs_be[2] = { (uint8_t)(cs >> 8), (uint8_t)(cs & 0xFF) };
    printf("[1] CMD+LEN = 03 00 -> cs=%04X, send=[ ", cs);
    hexdump(cs_be, 2);
    printf("    期望 cs=FFFD, send=[ FF FD ]\n");
    assert(cs == 0xFFFD);

    /* 用 proto_pack_read 接口组装完整的 7 字节请求帧 / Assemble 7-byte read frame */
    uint8_t pkt[7] = {0};
    size_t n = proto_pack_read(pkt, sizeof pkt, 0x03);
    assert(n == 7);
    printf("[1] pack_read(0x03) -> ");
    hexdump(pkt, n);
    printf("    期望 (Expected): DD A5 03 00 FF FD 77\n");
    assert(memcmp(pkt, "\xDD\xA5\x03\x00\xFF\xFD\x77", 7) == 0);
    return 0;
}

/* ============= 测试 2: 官方 BMS 响应例帧的校验与解包 =============
 * Test 2: Standard 27-byte BMS Response Validation & Unpacking (PDF Example) */
static int test_validate_bms_response(void) {
    /* 协议 V4 第 1 页标准 BMS 响应例帧 / Protocol V4 Doc p.1 example frame */
    const uint8_t resp[] = {
        0xDD, 0x03, 0x00, 0x1B,
        0x17, 0x00, 0x00, 0x00, 0x02, 0xD0, 0x03, 0xE8,
        0x00, 0x00, 0x20, 0x78,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x48,
        0x03, 0x0F, 0x02, 0x0B, 0x76, 0x0B, 0x82,
        0xFB, 0xFF, 0x77
    };
    size_t rlen = sizeof resp;

    /* 校验覆盖 bytes [2..30] = STATUS(00) + LEN(1B) + DATA(27 bytes), 共 29 字节 / Checksum over 29 bytes */
    uint16_t cs_calc = proto_checksum(resp + 2, (size_t)resp[3] + 2);
    uint8_t cs_calc_be[2] = { (uint8_t)(cs_calc >> 8), (uint8_t)(cs_calc & 0xFF) };
    printf("[2] RESP bytes[2..30] -> cs=%04X, frame=[ ", cs_calc);
    hexdump(cs_calc_be, 2);
    printf("    期望 (Expected) cs=FBFF, frame=[ FB FF ]\n");
    assert(cs_calc == 0xFBFF);

    /* proto_validate 校验并解包 / Unpack & validate */
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

/* ============= 测试 3: 错误状态响应帧 (STATUS=0x80) =============
 * Test 3: BMS Returns Error Status (STATUS=0x80 -> BMS_ERR_STATUS) */
static int test_validate_error_status(void) {
    /* 响应帧 / Error frame: DD 03 80 00 CS_H CS_L 77
     * sum = bytes[2..3] = 0x80 + 0x00 = 0x80
     * cs  = ~0x80 + 1 = 0xFF7F + 1 = 0xFF80  -> CS_H=0xFF, CS_L=0x80
     */
    const uint8_t err_resp[] = { 0xDD, 0x03, 0x80, 0x00, 0xFF, 0x80, 0x77 };
    bms_err_t e = proto_validate(err_resp, sizeof err_resp, 0x03, NULL);
    printf("[3] 错误状态帧 -> err=%d (期望 4 = BMS_ERR_STATUS)\n", e);
    assert(e == BMS_ERR_STATUS);
    return 0;
}

/* ============= 测试 4: 载荷字节损坏校验和不符 (BAD_CHECKSUM) =============
 * Test 4: Corrupted Payload Checksum Mismatch */
static int test_bad_checksum(void) {
    /* 将官方例帧的最后一个载荷字节从 0x82 篡改为 0x83 / Corrupt last data byte 0x82 -> 0x83 */
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

/* ============= 测试 5: 帧长度缺失 (BAD_FRAME) =============
 * Test 5: Truncated Short Frame (BAD_FRAME) */
static int test_short_frame(void) {
    const uint8_t too_short[] = { 0xDD, 0x03, 0x00 };
    bms_err_t e = proto_validate(too_short, sizeof too_short, 0x03, NULL);
    printf("[5] 短帧 (3 字节) -> err=%d (期望 2 = BMS_ERR_BAD_FRAME)\n", e);
    assert(e == BMS_ERR_BAD_FRAME);
    return 0;
}

int main(void) {
    printf("===== protocol 自测 / Protocol Unit Tests =====\n");
    int fails = 0;
    fails += test_checksum_host_example();
    fails += test_validate_bms_response();
    fails += test_validate_error_status();
    fails += test_bad_checksum();
    fails += test_short_frame();
    printf("\n===== 全部通过 (5/5) / All 5 Tests Passed =====\n");
    return fails == 0 ? 0 : 1;
}
