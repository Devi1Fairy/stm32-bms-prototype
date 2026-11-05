# 基于 STM32 / FreeRTOS 与 Qt 的电池监测及保护原型

基于 STM32F103ZET6、HAL 与 FreeRTOS，实现采样、故障保护、MOS 负载控制和通信上报。Windows Qt 上位机支持串口直连，也可通过 Linux 串口—TCP 网关完成远程监测与人工控制。

当前使用台式电源和电阻负载：电位器模拟单体电压，NTC 模块估算温度，INA226 实测母线电压与负载电流。项目定位为低压、小电流学习原型，尚未接入真实多串电池组。

- 本仓库：[stm32-bms-prototype](https://github.com/Devi1Fairy/stm32-bms-prototype)，包含 MCU 固件与 Qt 上位机。
- Linux 配套仓库：[netflow-analyzer](https://github.com/Devi1Fairy/netflow-analyzer)，包含串口—TCP 网关与网络流量分析工具，两者为独立功能。
- 更新日期：2026-11-05。

## 系统组成

~~~mermaid
flowchart LR
    POT[电位器 / NTC] -->|ADC| MCU[STM32F103ZET6 / FreeRTOS]
    INA[INA226 / R100] -->|I2C| MCU
    MCU -->|GPIO| MOS[MOS / 电阻负载]
    MCU <-->|UART 串口直连| QT[Windows Qt 上位机]
    MCU <-->|USB 串口| GW[鲁班猫 Linux 网关]
    GW <-->|TCP| QT
    MCU <-->|CAN 收发器| CAN[USB-CAN / PCAN-View]
~~~

串口直连与 Linux 网关模式按接线选择。采样、保护判断和 MOS 输出均由 STM32 本地执行；Qt 提交人工命令并观察后续报告，网络断开本身不会自动关闭负载。

## 已实现的功能

| 模块 | 当前实现 |
| --- | --- |
| 采样与器件驱动 | ADC 读取电位器与 NTC；通过 I²C 访问 INA226 寄存器，换算真实母线电压和电流；记录数值、来源、有效标志与采样时间，新转换才更新时间 |
| 保护与状态机 | 模拟单体和实测母线过欠压、过温、双向过流、采样无效与超时保护；自检、待机、运行、故障锁定四状态；故障锁存、恢复回差、连续稳定计时与手动清除 |
| FreeRTOS 与 IPC | 采集、保护、通信、心跳四任务；两个单项快照邮箱按值复制数据，普通命令使用 8 项队列；STOP 独立保留并优先处理，入队失败请求停止 |
| 任务健康与 IWDG | 四任务进度报到，依据健康快照条件喂狗；任务漏报或监督任务阻塞后通过独立看门狗复位；记录复位标志，复位后负载默认关闭 |
| 电量与能量统计 | 对有效电流、母线电压进行 64 位整数积分，分别累计流出/流入电量和能量；异常或超长间隔断链、不补算，报告有效覆盖时间与缺口 |
| MCU 通信 | USART1 接收字符命令并输出 BMS / STAT 文本；经典 CAN 使用 500 kbit/s 标准帧，实现启停、清故障、查询及状态/测量上报 |
| Qt 监测与记录 | C++17、Qt Widgets、QSerialPort、QTcpSocket；异步接收、有界按行组帧和字段校验；状态概览、四通道趋势、累计统计与 CSV 记录 |
| Qt 人工控制 | 串口及支持控制的 TCP 网关均可启停和清故障；网络请求按编号关联回执，收到提交回执后观察后续状态；处理上报过期、命令超时和重连清理，不自动重发旧命令 |

显示滤波与保护输入分开处理。自检通过目前仍使用演示常量，未实现完整硬件自检。

Qt 趋势保留最近 60 秒、最多 600 条记录，通道为模拟单体电压、NTC 估温、电流和实测母线电压。无效或过期采样断段；合法 0 V 可正常显示。CSV 保留 MCU 原始单位、有效性与年龄字段，并附带新鲜的累计统计和分段信息；PC 不重复积分。

## 验证状态

| 验证项 | 已有结果 |
| --- | --- |
| MCU 主机回归 | 2026-11-05 对当前实际源码运行回归入口，19 组全部通过，覆盖保护、状态机、恢复、协议、IPC、采集桥接、板级边界、累计统计、健康监测和看门狗逻辑 |
| Qt 软件检查 | 控制 v2 实际工程构建及 13 个模拟业务场景通过；母线扩展候选包含 28 个模拟业务场景，QtTest 总计 30 项通过；实现后用户反馈本步全部成功 |
| 台架采样与保护 | MOS 启停、47 Ω 负载过流、采样异常关断和恢复清除已获用户实测通过反馈；实测母线保护及 Qt 母线显示、趋势和 CSV 获整体通过反馈 |
| 看门狗恢复 | 用户确认任务漏报后复位、监督任务阻塞后复位、复位后 MOS 保持关闭三项实测通过 |
| 系统联调 | “STM32—Linux 网关—Qt”遥测、启动、停止和清除故障已获用户实物联调通过反馈 |

主机回归使用 HAL / 队列替身，不模拟电路、实际 RTOS 抢占或硬件故障。Qt 历史模拟测试产物保存在本地忽略目录，尚未接入仓库正式 CMake 测试目标。模拟检查与用户板测反馈分别记录在[测试记录](docs/测试记录.md)中。

当前没有计量标定精度、端到端最坏响应时间或长时间运行数据。netflow-analyzer 对网关真实 TCP 流量的旁路采集联动仍待验收。

## 代码结构

~~~text
MCU/projects/bms_bringup/
├─ Core/                   CubeMX 初始化、中断、HAL 回调转发与 RTOS 入口
├─ Bsp/                    ADC、MOS/LED、UART/CAN 传输及 IWDG 板级接口
├─ Devices/                INA226 寄存器访问、换算与器件诊断
├─ Components/
│  ├─ Bms/                 纯 C 保护、状态机、恢复、累计统计、健康与看门狗逻辑
│  ├─ Sensing/             NTC 估温与显示滤波
│  └─ Protocol/            CAN 编解码、UART 字符解析与上报有效性
├─ App/                    集中配置、采集、通信、IPC 与四任务编排
├─ Core/tests/             保护与控制器主机测试
├─ Tests/                  回归入口、模块测试及 HAL/队列替身
└─ MDK-ARM/                Keil 工程

host/bms_monitor/
├─ mainwindow.*            界面、连接会话及业务协调
├─ bms_log_parser.*        BMS/STAT 解析与有界按行组帧
├─ bms_gateway_protocol.*  网关能力、命令及回执协议
├─ bms_commands.*          命令许可与目标状态判断
├─ bms_csv_logger.*        CSV 记录
├─ bms_trend_widget.*      有限历史曲线
└─ CMakeLists.txt          Qt 构建入口

docs/                      开发步骤、开发记录、测试记录、规划与学习笔记
~~~

采集任务独占 ADC / I²C；保护任务独占控制器和正常运行时的 MOS 输出；通信任务读取报告副本并提交命令。硬件访问、纯 C 业务与任务编排分别组织，当前没有引入完整 OSAL 或通用消息总线。

## 台架与主要接线

首版负载电源为 5 V，逻辑供电为 3.3 V。正常负载使用 100 Ω / 5 W，过流演示使用 47 Ω / 10 W；INA226 分流电阻为 R100，按标称 0.100 Ω 换算。NTC 模块使用 3.3 V 供电，AO 接 ADC。

| STM32 引脚 | 用途 |
| --- | --- |
| PC0 / ADC1_IN10 | 电位器输入，映射第一节模拟单体电压 |
| PC1 / ADC1_IN11 | NTC 模块 AO |
| PB6 / PB7 | I2C1 SCL / SDA，100 kHz；当前 INA226 的 7 位地址为 0x44 |
| PA9 / PA10 | USART1 TX / RX，115200、8N1 |
| PB8 / PB9 | CAN RX / TX，外接 SN65HVD230；总线两端各接 120 Ω 终端 |
| PB10 | LOAD_EN，高电平使能 MOS |
| PB5 / PB0 / PB1 | 心跳 / 运行 / 故障 LED，低电平亮 |

功率电流路径：

~~~text
电源正极 → MOS DC+ / OUT+
         → INA226 IN+ → 板上 R100 → INA226 IN−
         → 负载电阻 → MOS OUT− → 板上 MOS 开关 → MOS DC− → 电源负极
~~~

INA226 VBS / VBUS 接自身 IN−；INA226 GND、STM32 GND 与电源负极建立共同参考。IN−是正极回路节点，OUT−是受控返回端，均不额外接地。高电平 MOS 控制“＋”接 PB10，信号 GND 接 STM32 GND，低电平控制“－”悬空；控制“＋”到信号 GND 保留 10 kΩ 下拉。

MOS 位于低端返回路径，关闭后负载电阻两端应接近 0 V，而 INA226 的母线测点仍可能约为 5 V。负载功率接线使用模块端子与合适导线，面包板用于信号验证。详细操作见[INA226 与负载接线](docs/开发步骤.md#ina226-current-diagnostic-20261027)。

## 构建与运行

### MCU 固件

已使用的环境：STM32CubeMX 6.18.0、STM32CubeF1 1.8.7、Keil 5、ARM Compiler 5.06 update 5、STM32F1xx_DFP 2.2.0；手写 C 使用 C99。

1. 打开 [bms_bringup.uvprojx](MCU/projects/bms_bringup/MDK-ARM/bms_bringup.uvprojx)，Rebuild 后通过 ST-LINK / SWD 下载。
2. 核对接线，将负载电源设为 5.0 V，从 0.10 A 限流开始，使用 100 Ω 负载；电位器调至模拟电压正常范围，NTC 保持正常。
3. 上电后默认关闭负载。若曾在负载电源关闭时启动 MCU，母线欠压可能已经锁存；供电恢复并连续稳定 2 秒后，发送 C 清除回待机，再发送 S 启动。
4. 依次验证停止、过流或采样异常关断、恢复后清除；结束时停止负载。分项步骤见[母线保护验证](docs/开发步骤.md#mcu-bus-voltage-20261104)及[看门狗验证](docs/开发步骤.md#mcu-iwdg-20261104)。

使用 CubeMX 重新生成前，保留自定义目录和 Keil 包含路径，核对 USER CODE 区；生成后检查 Git 差异并重新构建。

### Windows Qt 上位机

已使用 Qt 6.12.0、MinGW 13.1 与 C++17。工程依赖 Core、Widgets、SerialPort 和 Network，不依赖 Qt Charts。

1. 在 Qt Creator 打开 [CMakeLists.txt](host/bms_monitor/CMakeLists.txt)，选择匹配的 MinGW Kit，配置并构建；当前可执行目标名称仍为 qt_smoke。
2. 串口页选择实际 COM 口并连接，使用 115200、8N1、无流控。当前代码关闭 DTR / RTS，并保留关闭时不恢复旧参数的设置。
3. 网络页填写网关实际 IPv4 和端口，以 Linux 输出的 LISTEN 地址为准；收到有效 BMS 报告后才标记数据在线。
4. 查看概览、趋势与累计统计，选择路径开始 CSV 记录。通过启停与清故障按钮人工操作，观察命令反馈及后续设备状态。

### Linux 网关

网关源码与部署说明位于[配套 Linux 仓库](https://github.com/Devi1Fairy/netflow-analyzer)，本仓库包含其 Qt 客户端。已交接的启动格式为：

~~~text
bms-gateway SERIAL IPV4 PORT [SECONDS] [--control]
~~~

SERIAL、IPV4、PORT 均按实际设备填写。默认只读；显式启用 --control，且串口打开、配置成功后，网关发送控制能力行。当前每次运行只处理一个客户端会话，默认总时长预算 30 秒、可设 1～300 秒，等待客户端也计入预算；再次连接需要重新启动网关。

IP 和端口不写死，Qt 以当次 LISTEN 端点连接。当前协议用于可信实验局域网，未实现认证与 TLS。

## 控制与上报协议

### UART 与 CAN

UART：USART1，115200、8N1，普通文本发送大写字符。

| 字符 | 操作 |
| --- | --- |
| S / T | 启动 / 停止 |
| C | 满足恢复条件后清除故障，返回待机 |
| P / R | 本地调试：暂停 / 恢复采样更新，保护和通信继续运行 |
| D / A | 本地调试：切换通信 / 采集任务的 1000 ms 延迟，复位默认关闭 |

CAN：500 kbit/s、标准数据帧。

| ID | 长度 | 内容 |
| --- | --- | --- |
| 0x201 | 1 | 01 启动、02 停止、03 清故障、04 查询 |
| 0x301 | 8 | 状态、负载许可、清除许可、序号、活动故障位图 |
| 0x302 | 8 | 第一节模拟电压 mV、估算温度 0.1 ℃、电流 mA、有效位与来源 |

0x302 前三个量各占 2 字节，低字节先发，温度与电流为有符号 16 位。第 6 字节 bit0/1/2 表示电压/温度/电流新鲜有效，第 7 字节每两位依次表示来源。实测母线电压通过 UART / TCP 上报，母线故障进入 0x301 活动故障位图；当前未扩展 0x302 测量布局。

### BMS / STAT 文本

UART 输出 BMS 实时报告与 STAT 累计统计；网关按行转发正文，将行尾规范为单个 LF。以下仅为格式示例，不是实测记录：

~~~text
BMS state=1, faults=0x00000000, latched=0x00000000, load=0, clear=0, cell0_mv=3800, temp_est_dC=250, i_ma=0, iv=1, i_src=3, i_age=100, ina=1, bus_mv=5000, sample=10, ctrl=50, gap_max=20, bus_v=1, bus_age=100
STAT q_out_uah=833, q_in_uah=0, e_out_uwh=4166, e_in_uwh=0, covered_ms=60000, gaps=0, overflow=0
~~~

- state：0 自检、1 待机、2 运行、3 故障锁定；faults 为活动故障，latched 为历史锁存。
- cell0_mv 为模拟单体电压；temp_est_dC 为估算温度，单位 0.1 ℃；i_ma 为实测电流。
- bus_mv、bus_v、bus_age 分别表示实测母线 mV、有效标志和 MCU 报告的采样年龄 ms；该年龄不包含网络传输时间。
- q_out_uah / q_in_uah、e_out_uwh / e_in_uwh 分别为流出/流入的 µAh 与 µWh；covered_ms 为有效积分覆盖时间。累计值复位清零，不随 S / T / C 清零，尚未掉电保存。
- Qt 分别检查实时报告与累计统计的时效；CSV 保留母线原始值及质量字段，旧日志缺少完整母线字段组时不认定为有效母线测量。

### TCP 人工控制

| 方向 | 正文格式 |
| --- | --- |
| 网关 → Qt | GW 2 CONTROL |
| Qt → 网关 | CMD <编号> STOP / START / CLEAR |
| 网关 → Qt | ACK <编号> UART_ACCEPTED，或 ERR <编号> <原因> |

表中的 STOP / START / CLEAR 为三种可选动作；每条消息末尾为单个 LF，不附 NUL，控制请求不使用 CRLF。请求编号在同一连接内严格递增，范围 1～4294967295。

Qt 只有收到本连接精确的控制能力行后才允许 TCP 人工控制；只读或旧网关不授予控制能力。ACK 仅表示串口驱动接受了字节，执行效果继续依据后续 BMS 报告观察。命令超时不自动重发，断开后清除权限与等待状态，不跨连接重放请求；网关只映射 S / T / C，不开放本地调试字符。

## 参数与主机测试

主要演示参数集中在 [bms_config.c](MCU/projects/bms_bringup/App/Src/bms_config.c) 与 [bms_config.h](MCU/projects/bms_bringup/App/Inc/bms_config.h)：

| 项目 | 当前配置 |
| --- | --- |
| 采集 / 保护目标周期 | 100 ms / 20 ms |
| UART BMS / STAT / CAN 报告周期 | 200 ms / 1000 ms / 1000 ms |
| 模拟单体欠压 / 过压阈值 | 3000 mV / 4200 mV；恢复范围 3200～4100 mV |
| 实测母线欠压 / 过压阈值 | 4500 mV / 5500 mV；恢复范围 4700～5300 mV |
| 过温 / 温度恢复门限 | 60 ℃ / 55 ℃，基于未标定 NTC 估温 |
| 双向过流 / 恢复门限 | ±80 mA / ±60 mA |
| 采样最大年龄 / 恢复稳定时间 | 500 ms / 2000 ms |
| 任务健康超时 / 看门狗启动宽限 | 2000 ms / 3000 ms |
| IWDG | 64 分频、重载 2499；按 40 kHz LSI 推算标称约 4 秒 |

周期与看门狗推算值不是实测故障关断或复位时延；CAN 当前仍由通信任务轮询处理。

在仓库根目录执行 MCU 主机回归：

~~~powershell
python MCU/projects/bms_bringup/Tests/run_host_tests.py --compiler E:/mingw/bin/gcc.exe
~~~

将编译器参数替换为自己的 GCC 路径。当前入口包含 19 组，使用 C11 与 -Wall / -Wextra / -Werror / -pedantic 编译，产物位于被 Git 忽略的 build/host。Core/tests 与 Tests/host 的测试程序各自包含 main，不加入 Keil 固件目标。

## 当前范围与后续方向

- 第一节单体电压由电位器映射到 2800～4400 mV，第 2、3 节固定模拟 3800 mV；尚未完成真实三串采样。
- NTC 暂定 10 kΩ / B3950 模型，温度、电压和电流均未完成计量标定。
- 台架验证以电源加电阻负载为主；流入方向具备软件统计与保护逻辑，未搭建真实电池充电回路，累计量不等同于 SOC。
- 任务健康诊断保存在 RAM，复位标志可输出；故障日志、累计量掉电保存与历史数据库尚未实现。
- 后续可推进测量标定、自制 AFE 子板与 PCB、真实多串采样，再按需求扩展 SOC、均衡、Bootloader 或 SPI 故障存储。

## 文档导航

| 文档 | 内容 |
| --- | --- |
| [项目规划](docs/项目规划.md) | 需求、范围、预算、路线与投递准备 |
| [开发步骤](docs/开发步骤.md) | 接线、代码讲解、复现与验证步骤 |
| [开发记录](docs/开发记录.md) | 里程碑、实际问题与解决过程 |
| [测试记录](docs/测试记录.md) | 软件检查、用户板测反馈与验证边界 |
| [学习笔记与参考资料](docs/学习笔记与参考资料.md) | 外设原理、协议、Qt/C++ 与手册用途 |

历史教程保留当时的实现和验证状态，当前功能以本页、实际源码与最新记录为准。参考手册保存在被 Git 忽略的手册目录，构建产物不上传仓库。
