# 首次上板：HSE与72 MHz系统时钟

日期：2026-10-17。当前基线9a6a533，main与origin/main一致，文档更新前工作区干净。当前.ioc与SystemClock_Config仍为HSI 8 MHz、无PLL。本步为待用户实施的教程，尚未修改时钟、生成代码或上板验证。

## 唯一目标与依据

使用板载8 MHz HSE晶振，经PLL×9得到72 MHz系统时钟；复测原有D3与串口节奏，确认运行时的时钟选择。此步骤不增加ADC采样、BMS业务、UART接收或FreeRTOS。

配套双USB原理图第4页标注Y1为8 MHz晶体并带负载电容，Y2为32.768 kHz低速晶体。晶体频率依据来自配套资料，实物PCB版本与完整丝印按前期记录继续核对；没有振荡波形测量。

本机RM0008英文Rev15第7.2节与7.3节说明HSE、PLL、系统时钟及寄存器；第3.3节、PDF第59/61页说明Flash等待周期。F103章节位于第7章，不照搬第8章F105/F107互联型PLL参数。官方DS5792 Rev13支持72 MHz系统、APB1最高36 MHz及ADC最高14 MHz。

## 1. 准备和RCC选择

1. 退出Keil调试，保存VS Code文件，CubeMX打开原有MCU/projects/bms_bringup/bms_bringup.ioc，不新建另一份工程。
2. 确认项目仍选CubeF1 1.8.7、MDK-ARM V5、保留用户代码与外设分文件生成；当前Git基线已经保存，无需重做前一提交。
3. System Core → RCC：High Speed Clock (HSE)选Crystal/Ceramic Resonator。板上晶体跨接OSC_IN/OSC_OUT并带负载电容，使用芯片的振荡电路；Bypass用于从OSC_IN注入外部有源时钟。
4. Low Speed Clock (LSE)保持未启用，本步没有RTC需求。其余RCC选项保持当前默认，不把CSS或MCO加入本次验收。
5. 启用HSE后，CubeMX会分配OSC_IN/OSC_OUT。本F103的相关命名以器件数据库为准，不照搬混版原理图中的其他系列旧符号字段，也不把晶振引脚手工配置成普通GPIO。
6. 保留Serial Wire、PB5/LED_STATUS、USART1的PA9/PA10以及115200/8N1、无流控配置。

接线沿用左侧USB TO UART供电与板载CH340C；调试器三线SWD，RST留空。HSE是板上已有器件，本步不需要外接晶振、杜邦线或额外电容。

## 2. Clock Configuration逐项设置

切到Clock Configuration，从输入到输出检查。页面不同版本的文字位置可能不同，目标数值与来源选择保持如下：

| 配置 | 选择 | 结果与原因 |
| --- | --- | --- |
| HSE Input frequency | 8 MHz | 与板载晶体标注一致 |
| PLL Source Mux | HSE | PLL使用外部高速晶体作为参考 |
| HSE到PLL的分频 | /1 | 进入PLL仍为8 MHz，避免误选/2 |
| PLL倍频 | ×9 | 8×9=72 MHz |
| System Clock Mux | PLLCLK | 系统实际改用PLL输出；只启用PLL而不选择它仍不能切换 |
| AHB Prescaler | /1 | HCLK=72 MHz，作为内核及AHB域时钟 |
| APB1 Prescaler | /2 | PCLK1=36 MHz，满足该总线限制 |
| APB2 Prescaler | /1 | PCLK2=72 MHz，USART1在该域 |
| ADC Prescaler，页面可配置时 | /6 | ADC时钟12 MHz，低于14 MHz上限；本步不启用ADC采样 |

核心关系：

```text
Y1 8 MHz → HSE /1 → PLL ×9 → SYSCLK 72 MHz
                              └ AHB /1 → HCLK 72 MHz
                                           ├ APB1 /2 → PCLK1 36 MHz
                                           └ APB2 /1 → PCLK2 72 MHz
```

不要只填右侧72 MHz而不检查HSE来源和分频。若自动求解，求解后仍按表逐项核对。若当前未启用ADC导致其节点不可编辑，本步先不加ADC外设；下次启用ADC时设置/6，核对实际生成的ADC时钟代码。ADC节点目标并不等于本步已经执行ADC初始化或采样。

APB1分频不为1时，相关定时器时钟可能为PCLK1的两倍，即72 MHz；PCLK1与定时器计数时钟不能混为一谈，后续TIM实验再核对。左侧USB转串口依然由板载CH340C承担，本步不启用MCU USB设备外设。

## 3. 生成并检查关键配置

保留Keep User Code when re-generating，保存.ioc并生成。重新打开VS Code中的文件，核对用户区还在。

main.c的SystemClock_Config应包含以下关键行。它们是核对摘录，完整函数还包含结构体声明、OscillatorType、HAL_RCC_OscConfig及错误检查，不要用摘录整段覆盖函数：

```c
/* 以下为生成结果的关键配置摘录，用于核对，不替换完整函数。 */

/* 板载8MHz晶振进入PLL前不分频，PLL再乘9。 */
RCC_OscInitStruct.HSEState = RCC_HSE_ON;
RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL9;

/* 系统使用PLL；AHB为72MHz，APB1为36MHz，APB2为72MHz。 */
RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;
```

同时确认调用HAL_RCC_ClockConfig时使用FLASH_LATENCY_2。72 MHz下Flash读取需要2个等待周期；此前HSI 8 MHz使用FLASH_LATENCY_0，不能只改PLL而保留旧的Flash配置。让CubeMX与HAL完成正确的切换顺序。

HSE_VALUE在当前stm32f1xx_hal_conf.h和system_stm32f1xx.c中均为8000000U，与目标晶体一致。它用于软件计算频率，不能通过把它或SystemCoreClock变量直接改成72000000来代替硬件配置。

其他核对：

- main仍依次调用HAL_Init、SystemClock_Config、MX_GPIO_Init、MX_USART1_UART_Init。
- LED_PERIOD_MS=500u、UART_PERIOD_MS=200u及两个时间记录保留；用户区保留正常，不改周期常量。
- USART1仍配置115200、8N1；gpio.c/h、usart.c/h、用户BMS模块及Keil中的源文件引用保留。
- 主机tests目录不加入Keil，避免多个main。
- 生成代码回到两空格属于生成器格式，不是功能退化；手写代码继续四空格，不批量格式化库文件。

## 4. 为什么LED与串口不应该变快9倍

本机CubeF1 1.8.7的HAL_RCC_ClockConfig在切换后，按硬件寄存器计算并更新SystemCoreClock，随后调用HAL_InitTick重新配置毫秒时基。因此HAL_GetTick仍以毫秒为单位，LED和消息周期常量不变。

USART1初始化位于时钟配置之后。HAL_UART_Init使用新的PCLK2计算波特率分频，因此目标仍为115200；电脑串口助手无需改为原来的9倍。

这是本次要复测的行为，不凭代码生成成功宣布时基与波特率已通过。HAL_UART_Transmit仍为阻塞方式，增加CPU频率也不使串口字节以更高的目标波特率发送。

## 5. 编译、下载和运行时验收

### 构建与时钟变量

1. Keil F7构建，要求0错误0警告，再F8下载并查看校验结果。
2. 在main里的MX_GPIO_Init调用处F9设断点，进入目标调试并F5运行到这里。这个位置在SystemClock_Config之后、GPIO初始化之前。
3. Watch添加SystemCoreClock，应为72000000（若十六进制显示则为0x044AA200）。
4. 若仍停在HAL_Init入口，时钟配置尚未执行，看到8000000并不能判定配置失败；先运行到上述位置。

SystemCoreClock是由软件按硬件状态计算的变量。它支持配置检查，但单独显示72000000不证明实际外部晶体频率或测量精度。

### RCC寄存器状态

在Peripherals → System Viewer → RCC查看；具体菜单以本机Keil设备视图为准：

| 寄存器字段 | 期望 | 含义 |
| --- | --- | --- |
| RCC_CR.HSERDY | 1 | HSE已就绪 |
| RCC_CR.PLLRDY | 1 | PLL已锁定 |
| RCC_CFGR.PLLSRC | 1 / HSE | PLL参考来自HSE |
| RCC_CFGR.PLLXTPRE | 0 / HSE不分频 | HSE原频率进入PLL |
| RCC_CFGR.PLLMUL | ×9；原始编码0111 | 编码7表示乘9，不能把编码直接当倍频数 |
| RCC_CFGR.SWS | 10（二进制）/2 | 硬件报告当前SYSCLK实际来自PLL |
| RCC_CFGR.PPRE1 | /2 | APB1为36 MHz |
| RCC_CFGR.PPRE2 | /1 | APB2为72 MHz |

SW为软件请求，SWS为硬件实际状态，验收实际时钟来源要看SWS。若寄存器界面不可用，可提供Clock Configuration图及SystemCoreClock值后再指导查看，不为方便读寄存器新增业务代码。

### 原功能与独立运行

- F5继续执行，移除本次断点后观察约10 s。
- D3亮灭各约0.5 s、完整周期约1 s。
- 串口仍为115200/8N1，完整消息仍约每200 ms一次，无乱码。
- 退出调试，断开调试器连线，只保留板卡USB供电，按RESET复测相同功能。RST仍不连失电调试器，避免重现此前已定位的连接影响。

反馈构建/下载结果、SystemCoreClock、SWS及LED/串口现象。有示波器等仪器时可另做外部时钟测量，本步不把目测与变量值当成精确频率实测。

### Keil的调试时间显示

当前.uvoptx仍有-TC10000000，之前States/Sec显示曾对应10 MHz基准。若使用这些时间显示，在Debug → Settings → Trace页核对Core Clock为72 MHz；只校正调试显示/跟踪解释，不改变MCU真实时钟，也不要求启用SWO。

SWD Clock是调试通信频率，与CPU 72 MHz不同，保持此前稳定设置即可。不要把SWD Clock设成72 MHz来表示系统时钟。

## 6. 排查入口

| 现象 | 首先查看 |
| --- | --- |
| 停在Error_Handler且还没到GPIO初始化 | HAL_RCC_OscConfig或HAL_RCC_ClockConfig的返回结果、HSE模式与晶体频率、PLL源和倍频 |
| HSE未就绪 | 是否选晶体模式、配套板卡与实物晶体情况；先取证，不盲目增加启动超时 |
| SystemCoreClock为8 MHz | 断点是否在时钟配置之前、新固件是否下载、System Clock Mux是否选PLL |
| LED快约9倍或慢约9倍 | 时钟变量、SysTick重新配置、是否误手改时基或只改频率常量 |
| LED正常但串口乱码 | USART1是否在时钟配置后初始化、两端波特率、PCLK2与HSE_VALUE |
| USB转串口无端口 | 左侧USB、CH340C与数据线；不要把它等同于MCU的USB时钟问题 |

这些为尚未发生的排查方向，不填写为实际问题。本步失败时保留断点和返回结果，再决定修复；Git已保存的HSI基线可用于对照。

## 状态与Git收尾

| 项目 | 当前状态 |
| --- | --- |
| 前一步编辑器配置 | 根配置、settings与忽略规则已写入并保存于9a6a533；诊断消失未单独提供反馈 |
| 当前实际时钟 | HSI 8 MHz、无PLL |
| 本步HSE/PLL配置 | 教程已提供，待用户实施 |
| 本步构建、下载、运行时RCC检查 | 未验证 |
| HSE精确频率、真实采样、CAN、FreeRTOS | 未验证或后续阶段 |

本步验收后补写实测记录，用户执行：

```powershell
Set-Location -LiteralPath "D:\嵌入式学习\电池管理系统"

git status --short
git add MCU/projects/bms_bringup/bms_bringup.ioc MCU/projects/bms_bringup/.mxproject
git add MCU/projects/bms_bringup/Core
git add MCU/projects/bms_bringup/MDK-ARM/bms_bringup.uvprojx MCU/projects/bms_bringup/MDK-ARM/bms_bringup.uvoptx
git add README.md docs

git diff --cached --stat
git diff --cached --check
git diff --cached

git commit -m "feat: configure HSE and PLL for 72 MHz system clock"
git push origin main
git status --short
```

阶段内不混入其他功能，按实际diff检查生成代码和工程变化；编译输出与本地手册仍忽略。助手未代为执行Git。

## 资料

- 本地RM0008英文Rev15：第3.3节Flash、第7.2节时钟与第7.3节RCC寄存器，重点PDF第59/61、95～96、99～103页。
- 本机CubeF1 1.8.7的HAL RCC及UART实现：频率计算、Flash配置、毫秒时基与波特率更新。
- 配套双USB核心板原理图第4页：Y1及其8 MHz标注。上一轮已按PDF流程渲染核对，本轮没有修改或重新下载该资料。
- [ST官方F103xC/xD/xE数据手册DS5792 Rev13](https://www.st.com/resource/en/datasheet/stm32f103ze.pdf)。本轮重新核对；官方RM0008在线PDF超过工具长度限制，具体段落依据本机已整理的Rev15，没有声称读取了在线最新全文。

助手仅维护Markdown，未修改C/H、.ioc、Keil或VS Code配置，没有代为生成、构建、下载、调试、测量或Git。

