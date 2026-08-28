/*
 * protocol.h — 帧协议层的类型/常量/纯函数接口 / Frame Protocol Layer Types, Constants & Pure Functions
 * (实现 / Implementation: src/protocol.c)
 *
 * SOF/EOF/状态字节等帧常量在这里集中定义, 供 serial.c / bms.c 共用.
 * Central definitions for SOF/EOF/Status framing constants, shared across serial.c and bms.c.
 */

#ifndef BMS_PROTOCOL_H
#define BMS_PROTOCOL_H

#include <stdint.h>
#include <stddef.h>

/*
 * 一条响应帧的解包结果结构体
 * Unpacked structure for a received response frame
 */
typedef struct {
    uint8_t  cmd;        /* 命令号 (0x03 / 0x04 / 0x05) / Command code (0x03/0x04/0x05) */
    uint8_t  status;     /* 状态码: 0x00=成功, 0x80=错误 / Status code: 0x00=OK, 0x80=ERROR */
    uint16_t data_len;   /* 数据载荷长度 (字节数, 不含 cmd/status/len) / Data payload length (excluding cmd/status/len) */
    uint8_t *data;       /* 指向载荷数据的指针 (指向帧内缓冲区) / Pointer to payload bytes (points inside frame buffer) */
} bms_response_t;

/*
 * BMS 通信错误码枚举
 * BMS Communication Error Codes
 */
typedef enum {
    BMS_OK = 0,             /* 成功 / Success */
    BMS_ERR_TIMEOUT,        /* 读超时 (无响应) / Timeout waiting for response */
    BMS_ERR_BAD_FRAME,      /* 帧格式错误 (SOF/EOF/长度不匹配) / Malformed frame (invalid SOF/EOF or mismatched length) */
    BMS_ERR_BAD_CHECKSUM,   /* 校验和不匹配 / Checksum mismatch */
    BMS_ERR_STATUS,         /* BMS 返回非 0 错误状态 (如 0x80) / Non-zero error status returned by BMS (e.g., 0x80) */
} bms_err_t;

/*
 * 帧格式常量 (给各模块共用)
 * Protocol framing constants (shared across modules)
 */
#define BMS_SOF        0xDD   /* 帧起始字节 (Start of Frame) / Start-of-frame delimiter */
#define BMS_EOF        0x77   /* 帧结束字节 (End of Frame) / End-of-frame delimiter */
#define BMS_STA_READ   0xA5   /* 读命令状态控制字节 / Read command status/control byte */
#define BMS_STA_WRITE  0x5A   /* 写命令状态控制字节 / Write command status/control byte */
#define BMS_STATUS_OK  0x00   /* 响应状态: 正常 / Response status: OK */
#define BMS_STATUS_ERR 0x80   /* 响应状态: 错误 / Response status: Error */

/*
 * 打包/解析/校验, 全部无 I/O 的纯函数
 * Packet assembly, parsing, and checksum calculation (pure functions without I/O)
 */

/**
 * @brief 打包读寄存器命令帧 (7 字节: DD A5 CMD 00 CS_H CS_L 77)
 *        Assemble a 7-byte read command frame: DD A5 CMD 00 CS_H CS_L 77
 * @param out 目标输出缓冲区 / Output buffer
 * @param cap 缓冲区容量 / Output buffer capacity
 * @param cmd 命令号 (0x03/0x04/0x05 等) / Command code (0x03/0x04/0x05, etc.)
 * @return 写入的总字节数 (固定为 7, 容量不足返回 0) / Total bytes written (7, or 0 if capacity insufficient)
 */
size_t   proto_pack_read(uint8_t *out, size_t cap, uint8_t cmd);

/**
 * @brief 打包写寄存器命令帧 (7+plen 字节: DD 5A CMD LEN DATA CS_H CS_L 77)
 *        Assemble a write command frame: DD 5A CMD LEN DATA CS_H CS_L 77
 * @param out 目标输出缓冲区 / Output buffer
 * @param cap 缓冲区容量 / Output buffer capacity
 * @param cmd 命令号 / Command code
 * @param payload 写入的数据载荷 / Payload data bytes
 * @param plen 载荷字节长度 / Payload length in bytes
 * @return 写入的总字节数 (7 + plen, 失败返回 0) / Total bytes written (7 + plen, or 0 on failure)
 */
size_t   proto_pack_write(uint8_t *out, size_t cap, uint8_t cmd,
                          const uint8_t *payload, size_t plen);

/**
 * @brief 计算协议校验和: ~(sum) + 1 (二的补码, 16位大端存储)
 *        Calculate protocol checksum: ~(sum of bytes) + 1 (Two's complement)
 * @param p 待校验字节流起始指针 / Pointer to byte array to checksum
 * @param n 字节数 / Number of bytes
 * @return 16 位校验和 / 16-bit unsigned checksum
 */
uint16_t proto_checksum(const uint8_t *p, size_t n);

/**
 * @brief 校验并解包一个完整的 BMS 响应帧
 *        Validate and unpack a complete BMS response frame
 * @param frame 完整响应帧字节数组 / Full response frame buffer
 * @param len 帧总字节数 / Total length of frame in bytes
 * @param expect_cmd 期望匹配的命令号 / Expected command code
 * @param out 输出解包结构体指针 / Pointer to output response struct
 * @return bms_err_t 校验结果 (BMS_OK / BMS_ERR_*) / Validation result code
 */
bms_err_t proto_validate(const uint8_t *frame, size_t len,
                         uint8_t expect_cmd, bms_response_t *out);

#endif /* BMS_PROTOCOL_H */
