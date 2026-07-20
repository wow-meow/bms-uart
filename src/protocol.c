#include "protocol.h"
#include <string.h>

/*
 * 校验算法: 把 n 字节内容相加, 再按位取反 + 1 (即二的补码).
 *
 * PDF 帧结构表 (第 1 页) 说:
 *   cs = ~(sum of [DATA + LEN + CMD]) + 1,  高字节在前
 *
 * 例:
 *   主机发送 "DD A5 03 00 FF FD 77"
 *     sum = bytes from index 2 to 3 = CMD(03) + LEN(00) = 0x03
 *     cs  = ~0x03 + 1 = 0xFFFD  ->  发送 [0xFF, 0xFD]
 *
 *   BMS 响应 "DD 03 00 1B ...27字节... FB FF 77"
 *     sum = bytes from index 2 to (4+27-1) = STATUS(00) + LEN(1B) + DATA
 *     cs  = ~sum + 1 = 0xFBFF
 *
 * 注意: SOF (0xDD) 和状态位 (0xA5/0x5A) 不参与校验.
 */
uint16_t proto_checksum(const uint8_t *p, size_t n) {
    unsigned int sum = 0;
    for (size_t i = 0; i < n; i++) sum += p[i];
    return (uint16_t)(~sum + 1);
}

/*
 * 打包读命令帧: DD A5 CMD LEN(=0) CS_H CS_L 77, 7 字节.
 * 校验覆盖 out[2..3] (CMD + LEN).
 */
size_t proto_pack_read(uint8_t *out, size_t cap, uint8_t cmd) {
    if (cap < 7) return 0;
    out[0] = BMS_SOF;
    out[1] = BMS_STA_READ;
    out[2] = cmd;
    out[3] = 0;
    uint16_t cs = proto_checksum(out + 2, 2);    /* CMD + LEN */
    out[4] = (uint8_t)(cs >> 8);
    out[5] = (uint8_t)(cs & 0xFF);
    out[6] = BMS_EOF;
    return 7;
}

/*
 * 打包写命令帧: DD 5A CMD LEN DATA[..] CS_H CS_L 77.
 * 校验覆盖 out[2..3+plen] (CMD + LEN + DATA).
 * 本期不调用, 留口子. cap 至少 7 + plen 字节.
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
    /* 校验覆盖 [CMD + LEN + DATA] = out[2 .. 3+plen] */
    uint16_t cs = proto_checksum(out + 2, 2 + plen);
    out[4 + plen] = (uint8_t)(cs >> 8);
    out[5 + plen] = (uint8_t)(cs & 0xFF);
    out[6 + plen] = BMS_EOF;
    return 7 + plen;
}

/*
 * 校验一个完整的响应帧.
 * 响应帧格式: DD CMD STATUS LEN DATA[..] CS_H CS_L EOF
 *              0   1    2     3   4..     ...    end
 *
 * 校验覆盖 bytes [2 .. 4+data_len-1], 即 STATUS+LEN+DATA.
 *
 * 成功返回 BMS_OK; out 里填 cmd/status/data_len/data (data 直接指向 frame + 4).
 */
bms_err_t proto_validate(const uint8_t *frame, size_t len,
                         uint8_t expect_cmd, bms_response_t *out) {
    if (len < 7) return BMS_ERR_BAD_FRAME;
    if (frame[0] != BMS_SOF) return BMS_ERR_BAD_FRAME;
    if (frame[len - 1] != BMS_EOF) return BMS_ERR_BAD_FRAME;

    /* 响应帧 byte[1] 是 cmd, byte[2] 是 status */
    uint8_t cmd    = frame[1];
    uint8_t status = frame[2];
    uint8_t dlen   = frame[3];

    if (cmd != expect_cmd) return BMS_ERR_BAD_FRAME;

    size_t expected = (size_t)dlen + 7;
    if (len != expected) return BMS_ERR_BAD_FRAME;

    /* 校验和覆盖字节 [2 .. 4 + dlen - 1], 长度 = dlen + 2 */
    if (dlen > 253) return BMS_ERR_BAD_FRAME;  /* 防止 2+dlen 溢出, 实际不会 */
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
