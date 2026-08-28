/*
 * serial.h — 串口收发层接口 / Serial Port Communication Layer Interface
 * (实现 / Implementation: src/serial.c)
 *
 * open/config 成对使用; serial_read_frame 按 SOF..EOF 收取整帧.
 * Open/config paired usage; serial_read_frame captures complete frames delimited by SOF..EOF.
 */

#ifndef BMS_SERIAL_H
#define BMS_SERIAL_H

#include <stdint.h>
#include <stddef.h>
#include <termios.h>   /* for speed_t (B9600 ...) */

/**
 * @brief 打开串口设备文件 (以非阻塞模式打开)
 *        Open serial port device in non-blocking mode (e.g. /dev/ttyUSB0)
 * @param path 串口设备路径 / Serial device path
 * @return 成功返回文件描述符 fd, 失败返回 -1 / File descriptor fd on success, -1 on failure
 */
int  serial_open(const char *path);

/**
 * @brief 将串口配置为 8-N-1 raw 模式并配置 RS485 收发控制
 *        Configure serial port to 8-N-1 raw mode and set up RS485 direction control
 * @param fd 串口文件描述符 / File descriptor
 * @param baud 波特率常量 (如 B9600) / Baud rate constant (e.g. B9600)
 * @return 0 成功, -1 失败 / 0 on success, -1 on failure
 */
int  serial_config(int fd, speed_t baud);

/**
 * @brief 阻塞写入 n 字节数据 (处理 EINTR 和短写, 并控制 RS485 发送方向引脚)
 *        Blocking write of n bytes (handles EINTR and short writes, toggling RS485 RTS lines)
 * @param fd 串口文件描述符 / File descriptor
 * @param p 待发送字节缓冲区 / Pointer to buffer to send
 * @param n 发送字节数 / Number of bytes to send
 * @return 0 成功, -1 失败 / 0 on success, -1 on failure
 */
int  serial_write(int fd, const uint8_t *p, size_t n);

/**
 * @brief 从串口读取一个完整帧 (从 SOF 0xDD 开始至 EOF 0x77 结束)
 *        Read a single complete frame from serial (delimited from SOF 0xDD to EOF 0x77)
 *
 * - 自动丢弃 SOF 前的噪声字节 / Drops noise bytes before SOF
 * - 从收到 SOF 开始计算超时 timeout_ms / Timeout begins when SOF is detected
 * - 帧总长度受 cap 限制 / Max frame length bounded by buffer capacity cap
 *
 * @param fd 串口文件描述符 / File descriptor
 * @param out 接收缓冲区 / Output buffer
 * @param cap 接收缓冲区容量 / Output buffer capacity
 * @param timeout_ms 接收超时时长 (毫秒) / Timeout in milliseconds
 * @param out_len 输出实际接收到的帧字节数 / Pointer to store received frame length
 * @return 0 成功, -1 超时或错误 / 0 on success, -1 on timeout or error
 */
int  serial_read_frame(int fd, uint8_t *out, size_t cap,
                       int timeout_ms, size_t *out_len);

/**
 * @brief 关闭串口设备
 *        Close serial port file descriptor
 * @param fd 串口文件描述符 / File descriptor
 */
void serial_close(int fd);

#endif /* BMS_SERIAL_H */
