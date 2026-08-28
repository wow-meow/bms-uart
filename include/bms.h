/*
 * bms.h — BMS 业务层公开接口: 读命令封装 + 三条命令的解码结果结构体
 *         BMS Business Layer Public Interface: Read command wrappers + decoded response structures.
 *
 * 实现在 / Implementation in: src/bms.c
 * 协议常量及错误码在 / Protocol constants & error codes in: include/protocol.h
 */

#ifndef BMS_H
#define BMS_H

#include <stdint.h>
#include "protocol.h"   /* bms_err_t */

/* 调试模式: 1=打印原始收发字节, 0=静默. 在 main.c 里定义并可由 --debug 设置.
 * Debug mode: 1 = print raw TX/RX hex bytes, 0 = silent. Defined in main.c and set via --debug flag. */
extern int bms_debug;

/*
 * 基本信息 (0x03 命令) 解码结果
 * Decoded result structure for Basic Information & Status (Command 0x03)
 */
typedef struct {
    double   total_voltage_v;        /* 总电压 (V) / Total pack voltage (V) */
    double   current_a;              /* 电流 (A), 正=充电, 负=放电 / Current (A), positive=charging, negative=discharging */
    uint32_t remaining_capacity_mah; /* 剩余容量 (mAh) / Residual capacity (mAh) */
    uint32_t nominal_capacity_mah;   /* 标称容量 (mAh) / Nominal/Design capacity (mAh) */
    uint16_t cycle_count;            /* 循环充放电次数 / Charge/discharge cycle count */
    char     prod_date[16];          /* 生产日期 (YYYY-MM-DD) / Production date string ("YYYY-MM-DD") */
    uint16_t balance_low;            /* 均衡状态低字: bit0..15 对应 1..16 串 / Lower balance bits: bit0..15 -> cells 1..16 */
    uint16_t balance_high;           /* 均衡状态高字: bit0..15 对应 17..32 串 / Higher balance bits: bit0..15 -> cells 17..32 */
    uint16_t protection_bits;        /* 保护状态位图 (见 PDF 第 2 页) / Protection status bitmap (per Protocol V4 Doc p.2) */
    uint8_t  sw_version_major;       /* 软件版本主版本号 / Software version major number */
    uint8_t  sw_version_minor;       /* 软件版本次版本号 / Software version minor number */
    uint8_t  rsoc_pct;               /* 剩余容量百分比 (0-100%) / Relative State of Charge (RSOC %) */
    uint8_t  fet_state;              /* MOS 开关状态: bit0=充(CHG), bit1=放(DIS), 1=开(ON) / FET state: bit0=CHG, bit1=DIS, 1=ON */
    uint8_t  cell_count;             /* 电池串数 / Number of battery cells in series */
    uint8_t  ntc_count;              /* NTC 温度传感器个数 / Number of NTC temperature sensors */
    double   ntc_temp_c[16];         /* 各 NTC 探头温度 (°C), 前 ntc_count 个有效 / NTC temperatures (°C), valid for first ntc_count items */
} bms_basic_info_t;

/*
 * 单体电压 (0x04 命令) 解码结果
 * Decoded result structure for Cell Voltages (Command 0x04)
 */
typedef struct {
    uint16_t cell_count;             /* 实际读取到的电池串数 / Actual number of cell voltages read */
    uint16_t cell_mv[48];            /* 各串单体电压 (单位 mV, 最多支持 48 串) / Individual cell voltage in mV (up to 48 cells) */
} bms_cell_voltages_t;

/*
 * 硬件版本号 (0x05 命令) 解码结果
 * Decoded result structure for Hardware Version (Command 0x05)
 */
typedef struct {
    char hw_version[32];             /* 硬件型号 ASCII 字符串, 最长 31 字符 + '\0' / Hardware model ASCII string (max 31 chars + '\0') */
} bms_hw_version_t;

/*
 * 三条 BMS 读命令封装: 组读命令 -> 发送并等待整帧 -> 校验 -> 解码填充结构体.
 * 成功返回 BMS_OK, 任何步骤失败返回 BMS_ERR_*.
 *
 * Three BMS read command wrappers: pack read cmd -> send & wait frame -> validate -> decode into struct.
 * Returns BMS_OK on success, or BMS_ERR_* on any failure.
 */

/**
 * @brief 读取基本信息及状态 (命令 0x03) / Read BMS basic info and status (Command 0x03)
 * @param fd 串口文件描述符 / Serial port file descriptor
 * @param out 解码输出结构体指针 / Pointer to output basic info struct
 * @return bms_err_t (BMS_OK / BMS_ERR_*)
 */
bms_err_t bms_read_basic(int fd, bms_basic_info_t *out);

/**
 * @brief 读取所有单体电池电压 (命令 0x04) / Read individual cell voltages (Command 0x04)
 * @param fd 串口文件描述符 / Serial port file descriptor
 * @param out 解码输出结构体指针 / Pointer to output cell voltages struct
 * @return bms_err_t (BMS_OK / BMS_ERR_*)
 */
bms_err_t bms_read_cells(int fd, bms_cell_voltages_t *out);

/**
 * @brief 读取 BMS 硬件版本名称 (命令 0x05) / Read BMS hardware version string (Command 0x05)
 * @param fd 串口文件描述符 / Serial port file descriptor
 * @param out 解码输出结构体指针 / Pointer to output hardware version struct
 * @return bms_err_t (BMS_OK / BMS_ERR_*)
 */
bms_err_t bms_read_hwver(int fd, bms_hw_version_t *out);

#endif /* BMS_H */
