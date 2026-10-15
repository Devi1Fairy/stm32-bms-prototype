# STM32 电池监测与保护控制原型

更新时间：2026-10-15。项目状态：第一批硬件已下单，尚无到货验证记录；最小 HAL 工程首次编译通过，0 错误、0 警告，已生成 AXF/HEX；外设及业务功能未上板验证。

求职方向：MCU 开发、BMS、车载控制。已有嵌入式 Linux 网络流量分析项目，本项目补充外设驱动、实时任务、故障处理、状态机及 CAN 通信能力。

公开仓库：[Devi1Fairy/stm32-bms-prototype](https://github.com/Devi1Fairy/stm32-bms-prototype)。日常通过 Git 提交与推送保存每个可验证步骤的代码和记录。

项目名称暂定：**基于 STM32F103ZET6 与 FreeRTOS 的电池监测与保护控制原型**。

## 工作方式与边界

- 每次推进一个可验证的小步骤。每个配置步骤都解释做什么、为什么这样选、影响什么、如何验证，再结合实际接线和代码推进。
- 先裸机验证，再整合 FreeRTOS；采样接口先支持模拟输入，后续接入 BQ76920。
- 软件环境已选定：传统 STM32CubeMX 6.x＋Keil MDK；CubeMX 生成 HAL 工程，Keil 负责编辑、编译和调试。
- 小型核心板＋独立 ST-LINK＋外围模块。面包板只承载信号和初期验证连接；负载电流走模块、端子和合适的导线。
- 总预算 1000 元，分阶段采购。现有工具及具体模块版本待确认。
- 第一版为低压、小电流学习原型。SOC、均衡、Bootloader、复杂上位机、充电控制和车规验证不在第一版范围内。
- 只有经过验收、有日志或测量证据的功能才能标为已完成；计划功能不能写成已实现。

## 项目记录

- [第一版需求、硬件清单与选购注意事项](docs/01_需求与硬件规划.md)
- [开发记录与问题记录](docs/02_开发记录.md)
- [软件环境与首次编译](docs/03_软件环境与首次编译.md)
- [最小工程逐项配置解释](docs/05_最小工程逐项配置解释.md)
- [硬件购买链接与待确认项](docs/04_硬件购买链接.md)
- [Git 与 GitHub 同步](docs/06_Git与GitHub同步.md)
- [无板练习：采样数据类型](docs/07_无板练习_采样数据类型.md)

## 构建最小工程

当前工具配置：STM32CubeMX 6.18.0、STM32CubeF1 1.8.7、Keil MDK 5.24、Arm Compiler 5.06 update 5、STM32F1xx_DFP 2.2.0。

1. 使用 Keil 打开 [bms_bringup.uvprojx](MCU/projects/bms_bringup/MDK-ARM/bms_bringup.uvprojx)，确认使用已安装的 Compiler 5.06 update 5 和目标 STM32F103ZE。
2. 按 F7 构建。基线结果为 0 错误、0 警告；产物在 MDK-ARM/bms_bringup 下。
3. 如需修改图形配置，用 CubeMX 打开 [bms_bringup.ioc](MCU/projects/bms_bringup/bms_bringup.ioc)，使用 CubeF1 1.8.7 重新生成，再回到 Keil 构建。

当前使用内部 HSI 8 MHz，保留 SWD；编译无需连接开发板。源码、配置和必要依赖随仓库保存，构建输出与个人界面布局由 .gitignore 排除。当前未使用的 CMSIS DSP 源码与预编译数学库也已排除，后续加入该依赖时再调整规则。

## 当前推进点

用户已完成 bms_bringup 工程生成和首次编译，本机 .ioc、.uvprojx、构建日志、MAP、AXF/HEX 均已找到。实际工具配置为 CubeMX 6.18.0（.ioc 记录）、CubeF1 1.8.7、Arm Compiler 5.06 update 5、STM32F1xx_DFP 2.2.0；目标为 F103ZE，使用 HSI 8 MHz 和 SWD。实际工程位于 MCU/projects/bms_bringup。

开发板尚未到货。此前配置原理和 main.c 初始化流程已提供讲解，用户理解验收尚未单独记录。当前唯一练习目标：创建 bms_types.h，定义每路测量的数值、毫秒时间、有效性和来源，并在 main.c 的 USER CODE Includes 区域包含后编译。练习说明已准备，实际头文件和固件修改由用户操作，尚未完成。本轮不涉及接线、真实采样、保护逻辑或 FreeRTOS 整合。

无硬件阶段已完成验收：**STM32F103ZE 最小 HAL 工程生成、Keil 编译通过、AXF/HEX 生成、工具版本与构建结果记录。** 构建通过不代表下载、运行和采样保护功能通过。

第一条上板验收目标：**使用 SWD 连接芯片，成功下载程序，命中断点，并让板载 LED 按已知周期闪烁。** 具体供电、接线和 LED 引脚必须以实际板卡原理图为准。
