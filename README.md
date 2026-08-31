# BMS 上位机查询工具 (Jiabaida Protocol V4)

**版本: v1.0.0** · 2026-07-16

BMS (Battery Management System) 上位机工具，通过 USB 转 RS485 适配器与嘉佰达软件板通信，按协议 V4 解析响应。三档使用方式：

- **手动菜单模式**（`./bms_query /dev/ttyXXX`）：1/2/3 单次查询
- **菜单 [4] 监控模式**（菜单里按 `4`）：交互式 setup，进长期监控
- **CLI 监控模式**（`./bms_query --monitor 03,04 ...`）：脚本友好，不进菜单

所有查询支持 BMS 进深睡后的**自动重试（4 次）**。

## 硬件

- **嘉佰达软件板**（RS485 接口，波特率 9600-8-N-1）
- **USB 转 RS485 适配器**（CH340 / FT232 等多数免设置；少数需手动 RTS 切换）
- 接线：A+ / A- 对 A+ / A-, B+ / B- 对 B+ / B-，注意 485 半双工收发方向

## 编译

```bash
make           # 编译，产物在工程根 bms_query
make test      # 跑三套离线自测
make clean     # 删 .o + 可执行 (留 logs/)
make distclean # 连 logs/ 一起清
make run PORT=/dev/ttyUSB0
```

**依赖**：

- Linux（kernel ≥ 3.7 提供 `TIOCSRS485` 等 ioctl）
- GCC 4.8+（支持 c11）
- `make`
- 内核头文件：`sudo apt install linux-headers-$(uname -r)`（多数发行版默认就有）
- 无第三方库（只用 libc + POSIX）

## 用法

### 1. 手动菜单模式

```bash
./bms_query /dev/ttyUSB0
# 或无参数: 弹出端口选择器, 1/2/3 选中或者直接输 /dev/ttyXXX
```

菜单：

```
====== BMS Query Tool (Jiabaida V4) ======
  [1] 基本信息       (0x03)
  [2] 单体电压       (0x04)
  [3] 硬件版本号     (0x05)
  [4] 进入监控模式   (logs/monitoring/, 按 q 停)
  [q] 退出
>>
```

### 2. 菜单 [4] - 交互式监控模式

按 `4`，按提示输入：

```
>> 4
进入监控模式. CSV 写到 logs/monitoring/
请输入要查询的命令 (例如 03, 03,04): 03,04
请输入轮询间隔秒数 (1..3600, 直接回车用默认 5): 5
[poll] 启动. 按 q 或 Ctrl+C 退出.
[17:30:05] V=48.74V I=+0.05A Ah=9500mAh T=[25.5,25.9,26.0] protection_status=0x0000 (无)
[17:30:07] cells=13 min=3748 max=3752 spread=4 avg=3749 (mV)
...
q
[poll] 停止. 共记录 N 条
```

### 3. CLI 监控模式（脚本友好）

```bash
./bms_query --monitor 03,04 --interval 5 /dev/ttyUSB0
./bms_query --monitor 03 --interval 1 /dev/ttyUSB0
./bms_query --help
```

- `--monitor <list>`：`03` / `04` / `03,04`，必填
- `--interval <N>`：秒数（1..3600，默认 5）
- `Ctrl+C` 退出，干净 fclose 所有监控 CSV

### 4. 调试模式（任意入口）

加 `--debug` 会在每次收发时打印原始字节流：

```bash
./bms_query --debug /dev/ttyUSB0
./bms_query --debug --monitor 03 /dev/ttyUSB0
```

日志会带 `[debug] TX: DD A5 03 00 FF FD 77` 之类。

## 自动重试（唤醒 BMS 深睡）

BMS 长时间没活动后会进低功耗，首字节唤醒有延迟。协议层把单帧交互改成 4 次尝试：

| 常量 | 值 | 含义 |
| --- | --- | --- |
| `BMS_RESPONSE_TIMEOUT_MS` | 1000 | 单帧等待 1 秒超时 |
| `BMS_MAX_RETRIES` | 3 | 失败后最多再重试 3 次 |
| `BMS_RETRY_DELAY_MS` | 200 | 重试间隔给 BMS 唤醒缓冲 |

只对 **TIMEOUT**（根本没收到 SOF）触发重试；**BAD_FRAME / BAD_CHECKSUM / ERR_STATUS** 立即返回（BMS 回了，错了就是错了）。

## 输出示例

### 菜单 1 输出（手动）

```
[14:32:01] 总电压 66.23V | 电流 -20.12A | 剩余 34930mAh / 40000mAh (87%)
            循环 2 次 | 生产 2016-03-08 | 串数 17 | NTC 4
            温度[°C] 24.7 / 25.1 / 24.6 / 24.7
            MOS 充=ON 放=ON | 均衡: 1, 5-8, 12 开 / 其余关 | 保护 0x018A: 单体过压, 单体欠压, 短路
            软件 V1.2
```

保护状态以逗号分隔中文名称输出，未触发时仅显示 `保护 0x0000`。

### 监控模式输出

```
[17:30:05] V=48.74V I=+0.05A Ah=9500mAh T=[25.5,25.9,26.0] protection_status=0x0000 (无)
[17:30:07] cells=13 min=3748 max=3752 spread=4 avg=3749 (mV)
[17:30:10] V=48.74V I=+0.05A Ah=9500mAh T=[25.5,25.9,26.0] protection_status=0x018A (单体欠压, 整组欠压, 放电低温, 充电过流)
[17:30:12] cells=13 min=3748 max=3752 spread=4 avg=3749 (mV)
```

每行一条命令，触发保护时在括号内列出具体保护原因。

## CSV 输出

### 手动模式 - `logs/` 目录

文件名全英文无缩写，**每次查询追加一行**：

```
battery_basic_info.csv                       <- 菜单 1 (19 列)
battery_cell_voltages.csv                     <- 菜单 2 (50 列, 预留 48 串)
battery_hardware_version.csv                  <- 菜单 3 (2 列)
```

`balance_*` / `protection_bits` 列保留原始 16-bit hex，方便 pandas/Excel 后处理。

### 监控模式 - `logs/monitoring/` 子目录

**每次启动监控 (CLI `--monitor` 或菜单 4) 创建一份独立 CSV**，文件名带启动时间秒：

```
battery_basic_info_20260716_173000.csv           <- 监控基本摘要 (12 列)
battery_cell_voltages_20260716_173000.csv       <- 监控串电压统计 (6 列)
```

不在文件后追加，每次监控都是新文件。这样一次测试 run 一份记录，便于事后分离每个实验的数据。

**`battery_basic_info_*.csv`** (12 列摘要)：

```
timestamp,total_voltage_v,current_a,remaining_capacity_mah,production_date,
ntc_count,ntc1_c,ntc2_c,ntc3_c,ntc4_c,
protection_status,triggered_protection
```

- `protection_status`：原始 16-bit hex
- `triggered_protection`：逗号分隔中文名，**未触发时为空字符串**（pandas/Excel 视为 null）

**`battery_cell_voltages_*.csv`** (6 列统计)：

```
timestamp,cell_count,cell_min_mv,cell_max_mv,cell_spread_mv,cell_avg_mv
```


## 项目结构

```
bms_info/
├── README.md                       # 本文件
├── Makefile
├── bms_query                       # 可执行 (make 输出到根)
├── bin/                            # .o + 测试二进制
├── logs/                           # 手动模式 CSV (追加, 同名长期复用)
├── logs/monitoring/                # 监控模式 CSV (每次监控独立带时间戳文件)
├── doc/
│   ├── 485UART通用协议 V4.pdf      # 协议文档
│   └── IMPLEMENTATION_PLAN.md      # 计划快照 (实施时各阶段)
├── inc/                            # 头文件
│   ├── balance_fmt.h               # 均衡人话化
│   ├── bms.h                       # BMS 解码结构 + 接口
│   ├── csvlog.h                    # 手动 + 监控两套 CSV 写
│   ├── monitor.h                   # 监控模式接口
│   ├── prot_fmt.h                  # 保护名列表 + 状态打印
│   ├── protocol.h                  # 协议编解码常量 + 校验
│   ├── serial.h                    # 串口封装
│   └── ui.h                        # 手动菜单接口
├── src/                            # 源文件 (.c)
└── tests/                          # 离线自测
```

## 协议要点（V4）

帧格式：

```
0xDD | 0xA5(读)/0x5A(写) | CMD | LEN | DATA[..] | CS_H | CS_L | 0x77
```

校验：`CS = ~(sum of byte[2 .. end_of_data]) + 1`，大端 2 字节。

| 命令 | 含义 | 数据长度 |
| --- | --- | --- |
| `0x03` | 基本信息及状态 | 23 + 2N（N=NTC 个数） |
| `0x04` | 单体电压 | 2 × cell_count（字节大端，单位 mV） |
| `0x05` | 硬件版本号 | ≤31 ASCII 字节 |
| `0xE1` | 控制 MOS | （本期未实现） |

注意：

- 响应帧**无状态字节**：SOF 后面直接是 CMD
- 总电压单位 10mV，单体电压单位 mV（**两命令单位不同**）
- 电流最高位 = 1 表示放电
- 温度编码：`raw = 2731 + 实际°C × 10`

完整字段定义见 [`doc/485UART通用协议 V4.pdf`](doc/485UART通用协议%20V4.pdf)。


## 更新日志 (Changelog)

| 版本 | 日期 | 类型 | 内容 |
| --- | --- | --- | --- |
| V1.0.0 | 2026-07-16 | feat | 1. 实现三条读命令（0x03 / 0x04 / 0x05）<br>2. 通过 9600-8-N-1 串口与 RS485 自动收发方向建立通信（内核 `TIOCSRS485` + 手动 `TIOCMBIS` 兜底）<br>3. 协议层重试机制：唤醒重试 4 次（单次超时 1 秒 + 重试间隔 200ms）<br>4. 均衡状态格式化输出（连续串号折合区间表示，如 `1-3, 5, 7-9`）<br>5. 保护状态以逗号分隔中文名称输出（未触发时为空格，触发时列出具体保护原因）<br>6. 双日志路径：手动查询 `logs/` + 监控日志 `logs/monitoring/`，存储路径分离<br>7. 三入口模式：手动菜单 1/2/3、菜单 4 交互式监控、`--monitor` CLI 参数<br>8. 离线自测：协议层 5/5（PDF 例帧验证）、均衡格式化 14 项、保护格式化 21 项 |

## License

This project is licensed under the [MIT License](LICENSE).

