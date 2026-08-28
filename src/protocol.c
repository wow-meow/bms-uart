/*
 * protocol.c — 帧协议层: 校验和计算 / 组帧 / 响应帧校验
 *              Frame Protocol Layer: Checksum, Frame Assembly & Response Validation
 *
 * 协议帧结构 (Jiabaida Protocol V4 Doc p.1) / Frame Structures:
 *   主机读命令 / Host Read:  DD A5 CMD LEN(=00) CS_H CS_L 77          (7 字节 / 7 bytes)
 *   主机写命令 / Host Write: DD 5A CMD LEN DATA[..] CS_H CS_L 77      (7+LEN 字节 / 7+LEN bytes)
 *   BMS 响应帧 / BMS Response: DD CMD STATUS LEN DATA[..] CS_H CS_L 77  (7+LEN 字节 / 7+LEN bytes)
 *
 * 本模块全部为无 I/O 的可重入纯函数, 单元测试覆盖在 tests/test_proto.c.
 * Pure reentrant functions without I/O; tested in tests/test_proto.c.
 */

#include "protocol.h"
#include <string.h>

/*
 * @brief 计算协议校验和 / Calculate Protocol Checksum
 *
 * 校验和算法 (per Protocol V4 Doc p.1) / Algorithm:
 *   cs = ~(sum of covered bytes) + 1  (按位取反加一, 即二的补码; 16 位大端存储)
 *
 * 范围说明 / Covered Ranges:
 *   - 主机读: 校验覆盖 CMD + LEN 两个字节 (index 2..3);
 *   - 主机写: 校验覆盖 CMD + LEN + DATA (index 2..3+LEN);
 *   - BMS 响应: 校验覆盖 STATUS + LEN + DATA (index 2..3+LEN).
 *   注意: SOF (0xDD) 与状态字节 (0xA5/0x5A/CMD) 以及 EOF (0x77) 不参与累加求和.
 *
 * @param p 待计算字节流首地址 / Pointer to start of byte sequence
 * @param n 参与校验的字节数 / Number of bytes
 * @return 16 位大端校验和数值 / 16-bit unsigned checksum value
 */
uint16_t proto_checksum(const uint8_t *p, size_t n) {
    unsigned int sum = 0;
    for (size_t i = 0; i < n; i++) sum += p[i];
    return (uint16_t)(~sum + 1);
}

/*
 * @brief 组装 7 字节读命令帧: DD A5 CMD 00 CS_H CS_L 77
 *        Assemble a 7-byte read command frame: DD A5 CMD 00 CS_H CS_L 77
 *
 * @param out 输出缓冲区 (容量至少 7 字节) / Output buffer (at least 7 bytes)
 * @param cap 缓冲区容量 / Capacity of output buffer
 * @param cmd 命令号 (0x03/0x04/0x05 等) / Command code
 * @return 实际组装字节数 (7), 容量不足返回 0 / Total packed bytes (7), or 0 on error
 */
size_t proto_pack_read(uint8_t *out, size_t cap, uint8_t cmd) {
    if (cap < 7) return 0;
    out[0] = BMS_SOF;
    out[1] = BMS_STA_READ;
    out[2] = cmd;
    out[3] = 0;
    uint16_t cs = proto_checksum(out + 2, 2);    /* 校验覆盖 CMD + LEN / Checksum over CMD + LEN */
    out[4] = (uint8_t)(cs >> 8);
    out[5] = (uint8_t)(cs & 0xFF);
    out[6] = BMS_EOF;
    return 7;
}

/*
 * @brief 组装写寄存器命令帧: DD 5A CMD LEN DATA[..] CS_H CS_L 77
 *        Assemble a write command frame: DD 5A CMD LEN DATA[..] CS_H CS_L 77
 *
 * @param out 输出缓冲区 / Output buffer
 * @param cap 缓冲区容量 / Buffer capacity (must be >= 7 + plen)
 * @param cmd 命令号 / Command code
 * @param payload 待写入的载荷字节数组 / Payload data bytes
 * @param plen 载荷字节长度 (0..255) / Payload length in bytes
 * @return 实际组装字节数 (7 + plen), 失败返回 0 / Total packed bytes (7 + plen), or 0 on error
 */
size_t proto_pack_write(uint8_t *out, size_t cap, uint8_t cmd,
                        const uint8_t *payload, size_t plen) {
    if (plen > 255) return 0;
    if (cap < 7 + plen) return 0;
    out[0] = BMS_SOF;
    out[1] = BMS_STA_WRITE;
    out[2] = cmd;
    out[3] = (uint8_t)plen;
    if (plen > 0) memcpy(out + 4, payload, plen);
    /* 校验和覆盖 [CMD + LEN + DATA] = out[2 .. 3+plen] / Checksum over [CMD + LEN + DATA] */
    uint16_t cs = proto_checksum(out + 2, 2 + plen);
    out[4 + plen] = (uint8_t)(cs >> 8);
    out[5 + plen] = (uint8_t)(cs & 0xFF);
    out[6 + plen] = BMS_EOF;
    return 7 + plen;
}

/*
 * @brief 校验并解析一个完整的 BMS 响应帧
 *        Validate and parse a complete BMS response frame
 *
 * 响应帧字节布局 / Response Frame Layout:
 *   [0]    SOF (0xDD)
 *   [1]    回显命令号 CMD (0x03/0x04/0x05) / Echoed CMD
 *   [2]    状态字节 STATUS (0x00 正常, 0x80 错误) / Status byte
 *   [3]    数据长度 LEN (N 字节) / Data length N
 *   [4..]  数据载荷 DATA (N 字节) / Data payload (N bytes)
 *   [4+N]  校验和高字节 CS_H / Checksum MSB
 *   [5+N]  校验和低字节 CS_L / Checksum LSB
 *   [6+N]  EOF (0x77)
 *
 * 校验步骤 / Validation Steps:
 *   1. 最小长度检查 (>= 7 字节) / Minimum length check
 *   2. SOF / EOF 帧定界符匹配 / Delimiter check (0xDD .. 0x77)
 *   3. 命令号与期望命令匹配 (cmd == expect_cmd) / Command echo match
 *   4. 帧总长严格匹配 7 + LEN / Exact frame length match
 *   5. 校验和验证: 覆盖字节 [2 .. 4+LEN-1] (STATUS + LEN + DATA) / Checksum verification
 *   6. 状态字节检查: 0x00 判定为成功, 0x80 或其他非零返回 BMS_ERR_STATUS / Status code check
 *
 * @return BMS_OK 表示校验成功, 否则返回具体错误枚举 / BMS_OK or error code
 */
bms_err_t proto_validate(const uint8_t *frame, size_t len,
                         uint8_t expect_cmd, bms_response_t *out) {
    if (len < 7) return BMS_ERR_BAD_FRAME;
    if (frame[0] != BMS_SOF) return BMS_ERR_BAD_FRAME;
    if (frame[len - 1] != BMS_EOF) return BMS_ERR_BAD_FRAME;

    /* 响应帧 byte[1] 是 cmd, byte[2] 是 status / byte[1] is echoed cmd, byte[2] is status */
    uint8_t cmd    = frame[1];
    uint8_t status = frame[2];
    uint8_t dlen   = frame[3];

    if (cmd != expect_cmd) return BMS_ERR_BAD_FRAME;

    /* 总长必须正好是 7 + dlen / Total frame length must strictly equal 7 + dlen */
    size_t expected = (size_t)dlen + 7;
    if (len != expected) return BMS_ERR_BAD_FRAME;

    /* 校验和覆盖字节 [2 .. 4 + dlen - 1], 长度 = dlen + 2 / Checksum covers STATUS + LEN + DATA */
    if (dlen > 253) return BMS_ERR_BAD_FRAME;  /* 防止溢出 / Overflow guard */
    uint16_t cs_calc = proto_checksum(frame + 2, (size_t)dlen + 2);
    uint8_t  cs_h    = frame[4 + dlen];
    uint8_t  cs_l    = frame[5 + dlen];
    if ((uint8_t)(cs_calc >> 8) != cs_h || (uint8_t)(cs_calc & 0xFF) != cs_l)
        return BMS_ERR_BAD_CHECKSUM;

    if (status == BMS_STATUS_ERR) return BMS_ERR_STATUS;
    if (status != BMS_STATUS_OK)  return BMS_ERR_STATUS;

    if (out) {
        out->cmd      = cmd;
        out->status   = status;
        out->data_len = dlen;
        out->data     = (dlen > 0) ? (uint8_t *)(frame + 4) : NULL;
    }
    return BMS_OK;
}
