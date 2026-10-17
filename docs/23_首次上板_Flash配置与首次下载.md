# 首次上板：Flash配置与首次下载

日期：2026-10-17。本步唯一目标：将当前bms_bringup固件写入芯片内部Flash，并取得写入校验通过的日志。用户已提供算法配置截图，后续反馈编译下载完成；本次下载与Verify输出尚未归档。下文保留操作教程，下一步进行断点执行验证。

## 截图配置核对结果

当前工程目标为STM32F103ZE，使用STM32F10x_512.FLM。截图中的高密度512k片内Flash算法与当前工程相符。

| 截图项目 | 显示值 | 含义与本步处理 |
| --- | --- | --- |
| Algorithm | STM32F10x High-dens…，512k，On-chip Flash | 用于对应F1高密度器件的片内Flash擦写，保留 |
| Flash Start | 0x08000000 | 本工程内部Flash起始地址，保留 |
| Flash Size | 0x00080000 | 524288字节，即512 KiB，保留 |
| Flash Address Range | 0x08000000～0x0807FFFF | 起始地址加容量减1，与工程一致 |
| RAM for Algorithm Start | 0x20000000 | 从芯片SRAM起始位置放置下载工作区，保留 |
| RAM for Algorithm Size | 0x8000 | 32768字节，即32 KiB，位于64 KiB SRAM范围内，保留 |

当前.uvprojx声明SRAM范围为0x20000000～0x2000FFFF，Flash范围为0x08000000～0x0807FFFF，器件为STM32F103ZE。ZE容量参见[ST官方器件资料](https://www.st.com/en/microcontrollers-microprocessors/stm32f103ze.html)。

这只是当前工程与下载配置的核对，不是容量全片测试或芯片完整料号的实物确认。MCU清晰丝印和PCB版本仍待补充。

## 每个下载选项有什么作用

| 选项 | 当前设置 | 作用 |
| --- | --- | --- |
| Erase Sectors | 选中 | 擦除本次程序所需的Flash页/扇区，为写入做准备 |
| Program | 勾选 | 将固件内容写入Flash |
| Verify | 勾选 | 校验写入的内容是否与本次固件一致 |
| Reset and Run | 勾选 | 下载完成后请求目标复位并运行 |
| Erase Full Chip | 未选 | 本次不需要擦除整个Flash |
| Do not Erase | 未选 | 本次保留擦除步骤，不能假定旧Flash内容适合直接写入 |

RAM for Algorithm是给下载算法使用的临时工作区。Keil/ST-LINK会将擦写算法和缓冲数据放入目标RAM，再执行算法操作Flash。它不表示整个固件有32 KiB，也不把芯片的64 KiB SRAM改成32 KiB；下载后正常程序仍按自己的链接配置使用RAM。

下载配置和操作入口参见[Keil官方Flash下载教程](https://www.keil.com/support/man/docs/mcbtsx1001/mcbtsx1001_fp_download.htm)。该教程使用另一种芯片，仅参考通用选项意义与操作入口，不使用它的算法名称、RAM地址或容量。

## 第一步：保存下载与连接设置

1. 当前Flash Download页保持截图中的配置，不必Remove/Add算法，也不修改地址和容量。
2. 返回Debug页，确认Port为SW。
3. 若仍使用前一步三线接法、RST留空，将Connect设为Normal。上一张截图是under Reset，修改是否完成尚未得到用户确认。
4. 点击确定保存，退出Settings和Options for Target。

本机工程已经配置ST-LINK作为Flash下载驱动。如下载时出现驱动选择问题，再检查Options for Target的Utilities；此时不要求重新配置已有驱动。

## 第二步：编译当前固件

在Keil主界面按F7，确认当前项目构建成功，预期为0 Error(s)、0 Warning(s)。

F7负责把C代码、启动文件及依赖编译链接为固件。它不会因为构建成功就自动证明Flash写入或程序执行，当前步骤仍需单独下载。

如果构建失败，先保留完整错误，不继续下载旧产物。本步不需要新增或修改C/H文件。

## 第三步：下载并查看日志

保持开发板USB供电、ST-LINK连接及SWD接线，选择：

```text
Flash → Download（F8）
```

F8执行配置好的擦除、编程和校验。Keil可直接使用工程生成的AXF，无需另开软件手工选择HEX。F7/F8快捷键见[Keil官方快捷键表](https://www.keil.com/support/man/docs/uv4/uv4_dg_shortkeys.asp)。

输出窗口可能出现类似内容，下面是预期示例，不是本项目已经取得的实测日志：

```text
Erase Done.
Programming Done.
Verify OK.
...
```

不同驱动版本的措辞可能不同，需查看完整输出而非只匹配这三行。如果出现错误、警告或校验失败，保留原文再排查。

首次下载会覆盖对应Flash中的预装程序。本项目当前main()只做HAL、时钟和基础GPIO初始化，随后进入空循环，没有LED闪烁逻辑，也没有调用BMS控制器。因此D3/D4/D5可能停止原来的闪烁；以Flash校验结果验收本步，不用灯光变化替代日志。

## 本步验收与下一步

本步通过需要：当前工程构建成功，程序下载完成且校验通过。Reset and Run已勾选只是设置，不是已验证程序运行的证据。

下一小步才是进入Keil调试会话，命中main()附近可执行语句的断点，验证自己的固件执行。LED引脚配置在断点通过后独立推进。

| 项目 | 当前状态 |
| --- | --- |
| Keil识别调试器与目标SW-DP | 上一步截图已验证 |
| Flash算法、容量及地址设置 | 本次截图及工程文件已核对 |
| 下载算法RAM工作区 | 位于工程声明的SRAM范围内 |
| 当前固件首次编译与下载 | 用户已反馈完成 |
| 本次Flash校验输出 | Verify日志尚未提供或归档 |
| main()断点与本项目固件执行 | 后续步骤，未验证 |
| 自己编写的LED控制 | 尚未实现 |

用户后续反馈“编译下载已完成”，已按用户反馈记录，尚无此次Erase、Program、Verify的完整输出。若日志仍可查看，可后续一并保留，不必为继续本步教学重复下载。下一步见[main断点与单步验证](24_首次上板_main断点与单步验证.md)。

## 验收后的Git收尾

以下命令在下载校验通过、实际结果已补写后，由用户执行。Keil设置也由用户保存，助手不修改工程配置。

先查看差异：

```powershell
git status --short
git diff -- MCU/projects/bms_bringup/MDK-ARM/bms_bringup.uvoptx
git diff -- MCU/projects/bms_bringup/MDK-ARM/bms_bringup.uvprojx
```

若上一步的软件基线尚未提交，先按[统一控制周期教程](18_无板练习_统一控制周期.md)保存该版本，再进行本次收尾。检查实际工程改动确属本次设置后，暂存相应文件及文档：

```powershell
git add MCU/projects/bms_bringup/MDK-ARM/bms_bringup.uvoptx MCU/projects/bms_bringup/MDK-ARM/bms_bringup.uvprojx
git add README.md docs/02_开发记录.md docs/22_首次上板_SWD接线与Keil识别.md docs/23_首次上板_Flash配置与首次下载.md
git diff --cached --stat
git diff --cached --check
git diff --cached
git commit -m "chore: configure and verify first firmware download"
git push origin main
```

没有改动的工程文件不会被强行产生差异。下载产物继续由.gitignore排除，不要为了本步把AXF/HEX或本地PDF手册加入仓库。
