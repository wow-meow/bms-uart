#ifndef BMS_PROTOCOL_H
#define BMS_PROTOCOL_H

#include <stdint.h>
#include <stddef.h>

/* 一条响应帧的解包结果 */
typedef struct {
    uint8_t  cmd;        /* 0x03 / 0x04 / 0x05 */
    uint8_t  status;     /* 0 = 正确, 0x80 = 错误 */
    uint16_t data_len;   /* data 段长度, 不含 cmd/status/len 字段 */
    uint8_t *data;       /* 指向 data_len 字节载荷 (外部分配/owned) */
} bms_response_t;

typedef enum {
    BMS_OK = 0,
    BMS_ERR_TIMEOUT,
    BMS_ERR_BAD_FRAME,
    BMS_ERR_BAD_CHECKSUM,
    BMS_ERR_STATUS,
} bms_err_t;

/* 帧格式常量, 给各模块共用 */
#define BMS_SOF        0xDD
#define BMS_EOF        0x77
#define BMS_STA_READ   0xA5
#define BMS_STA_WRITE  0x5A
#define BMS_STATUS_OK  0x00
#define BMS_STATUS_ERR 0x80

/* 打包/解析/校验, 全部无 I/O 的纯函数 */
size_t   proto_pack_read(uint8_t *out, size_t cap, uint8_t cmd);
size_t   proto_pack_write(uint8_t *out, size_t cap, uint8_t cmd,
                          const uint8_t *payload, size_t plen);
uint16_t proto_checksum(const uint8_t *p, size_t n);
bms_err_t proto_validate(const uint8_t *frame, size_t len,
                         uint8_t expect_cmd, bms_response_t *out);

#endif /* BMS_PROTOCOL_H */
