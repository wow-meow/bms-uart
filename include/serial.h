#ifndef BMS_SERIAL_H
#define BMS_SERIAL_H

#include <stdint.h>
#include <stddef.h>
#include <termios.h>   /* for speed_t (B9600 ...) */

/* 打开串口设备; 失败返回 -1. path 形如 /dev/ttyUSB0 */
int  serial_open(const char *path);

/* 把 fd 配置成 8-N-1 raw 模式; 波特率通过 speed_t (B9600 等) 指定. */
int  serial_config(int fd, speed_t baud);

/* 阻塞写 n 字节; 短写就继续写直到写完或出错. 返回 0 ok, -1 err. */
int  serial_write(int fd, const uint8_t *p, size_t n);

/*
 * 从串口读一帧 (从 SOF 开始到 EOF 结束的整段字节).
 * - 超时 timeout_ms 毫秒
 * - out 容量 cap 字节
 * - 成功把帧 copy 进 out 并返回总字节数; 写入 *out_len.
 * - 超时/失败返回 -1, *out_len=0.
 */
int  serial_read_frame(int fd, uint8_t *out, size_t cap,
                       int timeout_ms, size_t *out_len);

/* 关闭串口 (tcsetattr 还原 + close). */
void serial_close(int fd);

#endif /* BMS_SERIAL_H */
