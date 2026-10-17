# 首次上板：USART1固定文本发送

日期：2026-10-17。本步唯一目标：电脑通过板载USB转串口每约1 s收到一行BMS UART1 OK，同时D3继续亮灭。本文为待用户实施的教程；UART配置、代码和电脑接收均尚未验收。

## 信号连接与硬件依据

依据双USB核心板原理图第5页J6、第7页CH340C，以及规格书V1.0.1第2.4.12～2.4.13节：

| MCU端 | 方向 | 板载CH340C端 | 跳帽 |
| --- | --- | --- | --- |
| PA9 / USART1_TX | MCU发送到电脑 | RXD | PA9↔RXD |
| PA10 / USART1_RX | 电脑发送到MCU | TXD | PA10↔TXD |

TX为发送端，RX为接收端，数据从一方TX到另一方RX。默认跳帽已经完成板载连接，先按实物丝印确认，不额外接杜邦线或外置CH340模块。

左侧USB TO UART口用于板卡供电和CH340C连接电脑；右侧USB DEVICE本步不使用。下载继续用ST-LINK的SWDIO、SWCLK、GND三线，RST留空，Normal连接。当前芯片自己的USB外设没有配置，电脑端口来自CH340C。

板上DTR/RTS连接ISP一键下载电路，可影响BOOT0和NRST。串口助手中关闭DTR/RTS控制、选择无流控；本步只读文本，不使用ISP下载按钮，BOOT跳帽保持现状。

## 重新生成前保存当前基线

最新只读Git检查仍为c7feb86，有控制器、LED、工程和文档待提交。用户可先保存这个已能运行的基线，避免与UART新改动混合；如果已完成前述分阶段提交，跳过此段。

```powershell
git status --short
git add MCU/projects/bms_bringup/Core
git add MCU/projects/bms_bringup/bms_bringup.ioc MCU/projects/bms_bringup/.mxproject
git add MCU/projects/bms_bringup/MDK-ARM/bms_bringup.uvprojx MCU/projects/bms_bringup/MDK-ARM/bms_bringup.uvoptx
git add README.md docs
git diff --cached --stat
git diff --cached --check
git diff --cached
git commit -m "feat: add controller module and onboard LED blink"
git push origin main
```

该提交只描述已实现的控制模块与LED，不声称UART已完成。GPIO代码需要包含gpio.c/gpio.h，编译产物、本地手册及空EventRecorderStub.scvd不纳入上述暂存范围。助手不执行Git。

## 第一步：配置USART1

退出Keil调试并保存文件，CubeMX打开现有bms_bringup.ioc。进入Connectivity → USART1：

| 配置 | 选择 | 原因 |
| --- | --- | --- |
| Mode | Asynchronous | UART异步通信，通过波特率约定时序，不需要额外时钟线 |
| Baud Rate | 115200 | 每秒传输位数，电脑端必须一致 |
| Word Length | 8 Bits | 每个数据单元8位 |
| Parity | None | 本步不加奇偶校验位 |
| Stop Bits | 1 | 每个数据单元以1个停止位结束 |
| Data Direction | Receive and Transmit / Tx and Rx | 初始化收发功能，本步代码只测试发送 |
| Hardware Flow Control | None / Disabled | 不使用CTS/RTS握手 |
| Over Sampling，若显示 | 16 Samples | 本F103配置保持16倍采样 |

8数据位、无校验、1停止位简称8N1。波特率描述位速率，一字节在8N1中还包含起始位与停止位，因此不等于每秒115200字节。

确认引脚为PA9/USART1_TX和PA10/USART1_RX，和板上跳帽一致；不重映射到其他引脚。保留PB5的LED_STATUS、HSI 8 MHz和Serial Wire。

本步不启用USART1全局中断，不配置DMA；HAL_UART_Transmit是轮询发送，等待硬件标志，不依赖USART1中断来完成发送。SysTick仍保留，为HAL超时与LED延时计时。

确认Keep User Code when re-generating后生成。当前项目按外设分文件生成，预计新增Core/Src/usart.c、Core/Inc/usart.h。

## 第二步：核对生成结果

main.c应自动包含usart.h，并在进入while之前调用MX_USART1_UART_Init()。现有HAL_Init、SystemClock_Config及MX_GPIO_Init保留。

usart.c/h中应有：

- UART_HandleTypeDef huart1及其extern声明：这是串口1的HAL句柄，保存所用外设与配置状态。
- Instance为USART1，参数为115200、8N1、Tx/Rx、无流控、16倍采样。
- 初始化USART1时钟及PA9复用推挽输出、PA10接收输入。
- HAL_UART_Init失败时调用Error_Handler。

stm32f1xx_hal_conf.h应由生成器启用HAL_UART_MODULE_ENABLED；Keil工程应加入usart.c和HAL UART驱动。以前的BMS和GPIO用户文件仍应保留，电脑测试文件不加入MCU工程。不要在生成前手工启用一部分HAL宏来绕过完整外设初始化。

## 第三步：编写固定消息发送

在main.c的PV用户区放入：

```c
/* USER CODE BEGIN PV */

/* 固定发送内容：\r\n让电脑收到后换到下一行。 */
static const uint8_t uart_message[] = "BMS UART1 OK\r\n";

/* USER CODE END PV */
```

static限制该变量的文件可见范围，const表示程序不修改消息内容，uint8_t数组存放字节。当前本地HAL_UART_Transmit的pData参数为const uint8_t*，可以直接传数组，不需要强制转换。

把WHILE用户区改为：

```c
/* USER CODE BEGIN WHILE */
while (1)
{
  /* USER CODE END WHILE */

  /* USER CODE BEGIN 3 */

  /*
   * 通过串口1发送一行消息。
   * sizeof减1：不发送字符串末尾的\0。
   * 100u：最多等待约100毫秒，单位不是字节数。
   */
  if (HAL_UART_Transmit(&huart1,
                        uart_message,
                        (uint16_t)(sizeof(uart_message) - 1u),
                        100u) != HAL_OK)
  {
    /* 本次基础实验发送失败时停在错误处理，便于用调试器定位。 */
    Error_Handler();
  }

  /* D3低电平亮，保持约500毫秒。 */
  HAL_GPIO_WritePin(LED_STATUS_GPIO_Port, LED_STATUS_Pin, GPIO_PIN_RESET);
  HAL_Delay(500u);

  /* D3高电平灭，保持约500毫秒。 */
  HAL_GPIO_WritePin(LED_STATUS_GPIO_Port, LED_STATUS_Pin, GPIO_PIN_SET);
  HAL_Delay(500u);
}
/* USER CODE END 3 */
```

保留程序其他位置，尤其不要重复手写MX_USART1_UART_Init或GPIO初始化。

参数解释：

| 参数 | 含义 |
| --- | --- |
| &huart1 | 使用串口1的句柄 |
| uart_message | 发送数据的起始位置 |
| sizeof(uart_message)-1u | 发送14字节，排除末尾NUL；这是当前作用域中的真实数组大小 |
| 100u | 本次发送的超时等待上限，毫秒 |

字符串的\r\n为回车换行；末尾\0属于C字符串结束标记，本步不发送。两次约500 ms延时加上发送开销，形成约1 s的消息间隔。

HAL_OK表示MCU发送过程完成，不证明电脑已收到；即使串口助手尚未打开，MCU也能完成UART发送。本步还要以电脑收到文本验收。

发送失败进入现有Error_Handler是本次基础调试策略；后续接入BMS控制和RTOS时，日志错误处理须与保护控制的运行要求一起设计。这里未实现接收回调、命令解析、printf重定向或统一日志模块。

## 第四步：确认电脑COM端口

1. 开发板左侧USB TO UART口接电脑，板卡保持供电。
2. 打开设备管理器的端口（COM和LPT），找到新增USB-SERIAL CH340及COM号。
3. 以实际新增端口为准。只读检查曾见历史CH340 COM8记录，但状态为Unknown，PresentOnly没有列出在线串口，不能直接指定COM8。
4. 若确实没有设备识别，再检查USB数据线、接口和驱动。需要驱动时使用[WCH官方CH341SER](https://www.wch.cn/downloads/ch341ser_exe.html)，支持CH340C。本轮不要求预先重装。

## 第五步：用串口助手接收

已有串口助手可沿用；没有的话可从[UartAssist作者页面](https://www.cmsoft.cn/home/resource/101.html)获取。用户自行安装或启动，助手没有下载、运行该软件。其[官方使用说明](https://www.cmsoft.cn/resource/321.html)可辅助查找设置。

| 电脑端选项 | 设置 |
| --- | --- |
| COM端口 | 实际CH340对应端口 |
| 波特率 | 115200 |
| 数据位 | 8 |
| 校验 | None |
| 停止位 | 1 |
| 流控 | None |
| 接收显示 | 文本 / ASCII，关闭HEX显示 |
| DTR、RTS | 关闭控制，保持未断言，不启用自动切换 |
| 自动发送或ISP下载 | 本步不启用 |

关闭占用该COM端口的其他窗口。Keil按F7构建、F8下载，检查编程与Verify；串口助手打开COM后观察：

```text
BMS UART1 OK
BMS UART1 OK
BMS UART1 OK
```

以上是预期结果，不是本项目已收到的日志。消息约每秒一行，D3继续闪烁。

## 验收与常见排查入口

本步通过需要：本次构建成功，下载校验通过，电脑连续收到至少10行完整文本，同时D3持续闪烁。

| 现象 | 先检查 |
| --- | --- |
| 找不到CH340 COM | 是否接左侧USB、是否数据线、设备管理器及驱动 |
| COM打开失败 | 端口是否在线、是否被其他工具占用 |
| D3闪但没有文本 | COM号、PA9↔RXD跳帽、USART1初始化与新固件下载 |
| 收到乱码 | 两端115200/8N1、文本显示、实际时钟和供电 |
| 打开串口后D3停止或重启 | DTR/RTS设置、自动下载电路及仍连接的调试器RST |
| D3停止且调试器停在Error_Handler | 检查HAL_UART_Transmit返回状态及初始化，保留暂停位置 |

这些是排查方向，不是已经发生的问题。没有反馈时不预填“驱动问题已解决”或“UART验证成功”。

| 验收项 | 当前状态 |
| --- | --- |
| 板载桥接器及PA9/PA10连接依据 | 已核对 |
| 电脑在线COM端口 | 待连接后确认 |
| USART1配置与代码 | 教程已给出，尚未实现 |
| 本次编译、下载和校验 | 待用户操作 |
| 电脑收到10行文本，D3继续闪烁 | 未验证 |
| 接收命令、格式化日志、DMA/RTOS | 后续阶段 |

完成后反馈F7/F8结果、实际COM号，以及连续接收文本的截图或复制出的内容。

## 验收后的Git收尾

用户确认通过、助手补写实测记录后，在已保存上一基线的前提下执行：

```powershell
git status --short
git diff -- MCU/projects/bms_bringup/bms_bringup.ioc MCU/projects/bms_bringup/Core
git add MCU/projects/bms_bringup/bms_bringup.ioc MCU/projects/bms_bringup/.mxproject
git add MCU/projects/bms_bringup/Core/Src/main.c
git add MCU/projects/bms_bringup/Core/Src/usart.c MCU/projects/bms_bringup/Core/Inc/usart.h
git add MCU/projects/bms_bringup/Core/Inc/stm32f1xx_hal_conf.h
git add MCU/projects/bms_bringup/MDK-ARM/bms_bringup.uvprojx MCU/projects/bms_bringup/MDK-ARM/bms_bringup.uvoptx
git add README.md docs
git diff --cached --stat
git diff --cached --check
git diff --cached
git commit -m "feat: send periodic bring-up message through USART1"
git push origin main
```

查看实际diff；若CubeMX还改了其他必要源码或新增HAL依赖，核对后再暂存对应文件。生成产物和本地PDF继续忽略。

## 资料与本次范围

- 双USB原理图第5/7页与规格书2.4.12～2.4.13，负责板卡接线。
- 本机CubeF1 1.8.7的HAL_UART_Transmit声明及实现，负责API参数、轮询和返回状态。
- RM0008的USART章节及[ST F1 HAL说明UM1850](https://www.st.com/resource/zh/user_manual/dm00154093.pdf)，负责异步通信与初始化概念。
- WCH与UartAssist官方页面只作驱动和工具入口，不表示已经安装或验证。

助手只维护Markdown、只读查看文件与设备信息；未修改代码、.ioc或Keil配置，没有代为编译、下载安装、占用COM端口或操作Git。
