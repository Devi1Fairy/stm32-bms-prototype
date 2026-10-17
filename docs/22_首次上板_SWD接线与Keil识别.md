# 首次上板：SWD接线与Keil识别

日期：2026-10-17。依据用户提供的实物照片和本地双USB核心板规格书确定接线。本步唯一目标：让Keil通过ST-LINK识别目标的SWD调试接口。用户后续Keil截图已确认ST-LINK及目标SW-DP可识别；下文保留操作教程并补充实际记录，下载和断点仍未验证。

## 已从照片核对的信息

开发板正面有野火标志，两个Type-C接口分别标为左侧USB TO UART和右侧USB DEVICE。MCU完整丝印及PCB版本在当前照片中无法可靠辨认，后续补齐。

将开发板放成照片方向，两个USB接口朝上。它们之间的蓝色五针接口为J2，从左到右为：

```text
RST    TCK    GND    TMS    3V3
```

SWD模式下，TCK对应SWCLK（PA14），TMS对应SWDIO（PA13）。名称来自复用调试信号；当前工程保留PA13/PA14的Serial Wire接口。

用户的紫色Zave ST-LINK V2外壳标签给出以下功能表：

| 针脚编号 | 标签 |
| --- | --- |
| 1 | RST |
| 2 | SWCLK |
| 3 | SWIM |
| 4 | SWDIO |
| 5、6 | GND |
| 7、8 | 3.3V |
| 9、10 | 5.0V |

这是照片中的兼容调试器针脚表，不能直接替换成其他型号的排针顺序。插针端的观看方向与外壳图可能不同，接线按实物针脚编号定位；找不准编号时先补充插针端照片。

## 第一步：断电后接三根线

先拔掉开发板和ST-LINK的USB，使用三根母对母杜邦线连接：

| ST-LINK | 开发板J2 | 作用 |
| --- | --- | --- |
| 2号SWCLK | TCK | 调试器提供通信时钟 |
| 4号SWDIO | TMS | 双向传送调试命令和数据 |
| 6号GND，也可选5号 | GND | 提供共同的电压参考 |

SWD使用两根调试信号线，仍需要共地。即使两者稍后都接同一电脑，也把GND按表连接，形成明确的信号回路。

本轮RST留空，ST-LINK的3.3V/5V脚和板卡J2的3V3也留空。我们通过板卡USB供电，不把两个电源输出并接。这个方案针对照片中的调试器；带VTref输入的其他探针需按其手册连接参考电压。

保持现有BOOT跳帽，不接外部CH340、电位器、NTC或负载模块，不需要将核心板插入面包板。

## 为什么这一步不接3.3V

既定供电方案为：

- 开发板USB输入5V，经板载稳压器得到3.3V，为STM32供电。
- ST-LINK通过自己的USB供电。
- 两者之间连接SWCLK、SWDIO和GND，用于调试通信。

因此板卡已经有自己的3.3V电源，无需再由调试器提供电源。若调试器的3.3V脚是电源输出，再接到板卡3V3会把两个稳压输出直接并联。两路实际电压不可能完全一致，可能产生回灌；除非电路明确支持这种连接，否则不这样接。

必须区分两类接口：

| 接口功能 | 含义 | 板卡已通过USB供电时 |
| --- | --- | --- |
| 3.3V供电输出 | 从调试器向目标板送电 | 本方案不并接 |
| VTref / VAPP等目标电压参考输入 | 从目标板取得电压参考，用于调试器的目标接口 | 按具体调试器手册连接目标VDD |

不能仅凭“3.3V”标签断定兼容调试器内部电路，也不能把当前三线方案推广到所有探针。带目标电压参考输入的型号应按其手册连接，参见[ST官方UM1075第4.2节](https://www.st.com/resource/en/user_manual/um1075-stsw-link007.pdf)。本项目兼容调试器的内部原理图及电源输出能力尚未核实，当前也不计划用它给整板供电。

## 第二步：分别连接USB

1. 开发板通过左侧标有USB TO UART的Type-C口连接电脑，保持板卡电源开启。
2. ST-LINK通过自己的USB接同一电脑。
3. 检查D1是否亮起，并确认三根线没有错位或松脱。

左侧USB同时承担板卡供电及板载USB转串口功能；这一步使用它供电，不依赖COM口是否出现。右侧USB DEVICE本轮留空。

## 第三步：在Keil检查连接

打开项目的MDK-ARM/bms_bringup.uvprojx。此时先不点Download，也不进入调试会话：工程可能在进入调试时自动下载程序。

操作路径：

```text
Options for Target（Alt+F7）
  → Debug
  → Use: ST-Link Debugger
  → Settings
  → Debug
```

逐项检查：

| 设置或显示区域 | 本步操作 | 原因或意义 |
| --- | --- | --- |
| Use | ST-Link Debugger | 使用外接调试器连接实物 |
| Port | SW，部分版本显示SWD | 对应已接好的SWCLK与SWDIO |
| Max Clock，若有此项 | 先选不高于1 MHz的档位 | 为初次杜邦线连接选择较低通信速率；不是MCU运行时钟 |
| Connect | 若RST留空，选择Normal | under Reset需要通过目标NRST实现硬件复位 |
| Adapter / Serial Number / FW | 查看是否出现调试器信息 | 验证电脑到ST-LINK这一段 |
| SW Device | 查看是否出现ARM CoreSight SW-DP及有效IDCODE | 验证ST-LINK到目标调试接口这一段 |

该设置入口与SW Device判据见[Arm/Keil官方教程第26节](https://www.keil.com/appnotes/files/apnt_230_v1.35.pdf)。教程使用另一款开发板，只参考Keil操作及识别判据，不套用其中的芯片ID、Flash算法或板卡接线。

若列表需要刷新，可退出Settings后重新打开。当前截图为under Reset；若仍只连接三根线、RST留空，将Connect改为Normal，再确认目标仍可识别。under Reset用于在硬件复位期间连接，需调试器实际控制目标NRST，参见[ST官方AN4989的连接与复位说明](https://www.st.com/resource/en/application_note/an4989-introduction-to-debug-toolbox-for-stm32-mcus-stmicroelectronics.pdf)。Reset的Autodetect本轮先保留；若后续报硬件复位错误，再依据实际接线检查。Trace与Flash算法另行核对。

## 如何判定结果

本步通过需要同时满足：

- Keil识别到ST-LINK，例如有序列号或固件版本信息。
- SW Device区域有目标SW-DP及有效IDCODE，没有目标连接错误。

只看到调试器序列号，仍不能说明已连上STM32。有效SW-DP表示调试接口可访问，也不等于已经验证STM32F103ZET6的完整型号或本项目固件运行。

| 实际现象 | 后续排查方向 |
| --- | --- |
| 无调试器信息，或No ST-LINK detected | 调试器USB连接、Windows设备识别和驱动 |
| 有调试器信息，但No Cortex-M SW Device Found等错误 | 板卡供电、SWD接线、针脚方向及SW端口设置 |
| 调试器与SW-DP都能识别 | 保存设置并反馈截图，下一步检查下载配置和main()断点 |

以上仅是排查方向，不是已经定位的根因。若Windows确实未识别调试器，再使用[ST官方STSW-LINK009驱动](https://www.st.com/en/development-tools/stsw-link009.html)；未出现驱动问题时不要求先重装。

## 反馈与记录

完成后提供Settings中的Debug页面截图，保留调试器信息、Port及SW Device区域。若报错，保留完整报错文字。实际接线与芯片完整丝印也可一并补充。

| 验收项 | 当前状态 |
| --- | --- |
| 开发板SWD标签及ST-LINK外壳针脚表 | 已从照片核对 |
| SWD实际接线 | 已能访问目标；具体线数、RST及电源脚实接未另行反馈 |
| Keil识别ST-LINK | 截图已验证ST-LINK/V2，有序列号，FW V2J37S7、HW V2 |
| Keil识别目标SW-DP | 截图已验证ARM CoreSight SW-DP，IDCODE 0x1BA01477 |
| 本项目固件下载、main()断点 | 后续步骤，未验证 |

## 实际截图记录

用户截图显示Port为SW，Clock的Req和Selected均为4 MHz，Connect为under Reset，Reset为Autodetect，Reset after Connect已勾选。右侧目标SW-DP信息有效，本次目标识别通过。

这只证明当前能够访问调试接口，不证明4 MHz下已通过下载或长时间调试。截图未显示实际导线数量，也未证明RST连接及硬件复位可用；按上文匹配连接模式后再进行下载验证。无需仅为重复识别而重新接电源线或重装驱动。

## 本步Git记录

Git由用户执行。若之前统一控制周期版本仍未提交，先按[18号教程](18_无板练习_统一控制周期.md)保存该软件基线，再提交这次记录。本次可单独暂存文档：

```powershell
git add README.md docs/02_开发记录.md docs/21_首次上板_型号核对与USB供电.md docs/22_首次上板_SWD接线与Keil识别.md
git diff --cached --stat
git diff --cached --check
git diff --cached
git commit -m "docs: record first SWD target connection"
git push origin main
```

若调整Keil并保存后产生工程配置差异，先检查git diff确认改动，再由用户决定是否一并暂存。助手未执行任何Git写操作。

助手只维护文档，用户操作接线和Keil。本轮不修改代码；Git由用户在验收后执行。

下一小步见[Flash配置与首次下载](23_首次上板_Flash配置与首次下载.md)：算法截图已经核对匹配，实际下载与校验仍待用户执行。
