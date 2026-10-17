# 开发工具：IntelliSense、缩进与时钟选择

日期：2026-10-17。前一步毫秒计时实验已由用户反馈成功，并保存于23935d3。更新：根目录IntelliSense配置、四空格settings.json及本机配置忽略规则已由用户写入，保存于9a6a533；提示消失尚未单独反馈。以下首次排查事实和操作保留供复现，不必重复覆盖已维护的根配置。HSE切换另见30号教程，目前仍为HSI。

## 已核对的事实

- Keil最新已有构建日志2026-10-17 15:13:57：0错误、0警告。
- 用户截图为Microsoft C/C++扩展C/C++(20)诊断，提示GPIOB、RCC_CR_HSION、RCC_CFGR_SW_HSI、RCC_CFGR_HPRE_DIV1、RCC_CFGR_PPRE1_DIV1未定义。
- 五个宏均存在于项目Drivers/CMSIS/Device/ST/STM32F1xx/Include/stm32f103xe.h；Keil编译宏包含USE_HAL_DRIVER、STM32F103xE。无需在main.c重新定义寄存器或增加假的空宏。
- 初次排查时唯一现有c_cpp_properties.json位于MCU/projects/bms_bringup/MDK-ARM/.vscode。其defines已经包含正确芯片宏，includePath已经包含Core、HAL、CMSIS及ARMCC头文件。
- 初次排查时根目录尚无.vscode/c_cpp_properties.json，优先怀疑嵌套配置没有被仓库根目录窗口加载；现根配置已写入，但尚无当前main.c的Log Diagnostics或明确的提示消失反馈，不将推断写成已经复测确认的根因。
- VS Code历史工作区同时出现仓库根目录与MDK-ARM目录，历史记录不能证明当前生效配置；下面统一以仓库根目录为工作区，避免路径歧义。
- main.c中的Tab字符计数为0。当前看到的不同宽度是普通空格缩进差异；用户设置已有editor.detectIndentation=false。
- CubeMX和main.c仍选择HSI 8 MHz；HSE_VALUE=8000000U只是外部晶振频率常量，不代表已经启用HSE。

## 1. 让IntelliSense读取正确配置

Keil与C/C++扩展分别处理构建和编辑器分析。Keil读取.uvprojx里的芯片宏、头文件路径和ARMCC配置，C/C++扩展需要自己的工作区配置。两边不一致时，可以出现编译成功但编辑器报未定义。

### 操作

1. 确认VS Code通过“文件 → 打开文件夹”打开D:\嵌入式学习\电池管理系统。
2. 在PowerShell执行下面命令，复制已经由Keil Assistant生成的配置到当前工作区根目录：

```powershell
Set-Location -LiteralPath "D:\嵌入式学习\电池管理系统"

New-Item -ItemType Directory -Path ".vscode" -Force | Out-Null

Copy-Item -LiteralPath "MCU/projects/bms_bringup/MDK-ARM/.vscode/c_cpp_properties.json" -Destination ".vscode/c_cpp_properties.json"
```

这复用已有的ARMCC编辑器兼容定义及头文件路径，不改变Keil编译配置。当前检查目标根配置不存在；未来若已经维护过根配置，先比较再同步，避免覆盖自己的改动。

3. Ctrl+Shift+P运行C/C++: Select a Configuration...，选bms_bringup。
4. 运行C/C++: Reset IntelliSense Database，随后Developer: Reload Window，等待重新分析。
5. 打开main.c观察提示；GPIOB应能识别到对应器件宏。按F12可辅助验证头文件导航，但能跳转本身不代替当前翻译单元的完整分析。

根配置文件已经有USE_HAL_DRIVER、STM32F103xE，不需要重复添加。不要为消除提示把这些项目宏或兼容空定义散放进main.c；配置中的兼容宏只供编辑器分析使用。

### 如果提示仍存在

保持main.c为当前文件，运行C/C++: Log Diagnostics，从C/C++输出窗口查看：

- Current Configuration是否为bms_bringup。
- 当前翻译单元的Defines中是否含USE_HAL_DRIVER、STM32F103xE。
- Includes是否含Core/Inc、HAL Inc、CMSIS器件目录、CMSIS/Include及本机ARMCC标准头目录。
- 是否存在其他configurationProvider、compile_commands或错误的桌面编译器配置覆盖。

不要只关闭errorSquiggles；需要获得有效配置再判断其余提示。源代码编译结论以Keil为准，但编辑器分析也应尽量与真实工程一致。复制配置是否解决这五条诊断，等待用户反馈。

## 2. 四空格缩进与格式化

Tab键是输入动作，Tab字符与普通空格是不同字符。editor.insertSpaces=true时，按Tab键插入空格；tabSize=4决定插入宽度。已有两空格不会因为改设置自动变成四空格。

CubeMX生成器采用自己的格式；VS Code设置不会改变CubeMX的模板。约定：以后手写C代码、用户区和新增模块采用四空格，自动生成部分保留生成器格式。缩进宽度不改变这些C语句由大括号确定的层级。

在仓库根目录新建.vscode/settings.json，内容如下；如果文件已有内容，则合并设置：

```json
{
    "editor.insertSpaces": true,
    "editor.tabSize": 4,
    "editor.detectIndentation": false,
    "[c]": {
        "editor.defaultFormatter": "ms-vscode.cpptools",
        "editor.formatOnSave": false
    },
    "C_Cpp.clang_format_style": "{ BasedOnStyle: LLVM, IndentWidth: 4, UseTab: Never, BreakBeforeBraces: Allman }"
}
```

各项作用：

| 设置 | 作用 |
| --- | --- |
| editor.insertSpaces | 用普通空格进行缩进 |
| editor.tabSize | 每级四空格 |
| editor.detectIndentation=false | 不让旧文件的两空格布局覆盖所选宽度 |
| editor.defaultFormatter | C文件使用Microsoft C/C++扩展格式化 |
| editor.formatOnSave=false | 保存时不自动重排整份main.c |
| C_Cpp.clang_format_style | 手动格式化采用四空格、禁止Tab字符及另起一行的大括号 |

当前main.c里Error_Handler分支等用户代码有缩进未对齐，逻辑仍由大括号决定。需要整理时只选用户代码块，按Ctrl+K再Ctrl+F执行Format Selection；不要批量格式化整个HAL/CMSIS库。自动生成部分整体改成四空格后，再生成还会回到生成器的格式，造成反复的无关diff。

后续助手直接提供的C示例使用四空格。已有历史教程与生成代码不追溯批量改写。

### 配置文件与Git

复制的c_cpp_properties.json含本机D盘/E盘绝对路径。在.gitignore末尾由用户追加：

```gitignore
/.vscode/c_cpp_properties.json
```

保留现有MDK-ARM/.vscode忽略规则。通用的根.vscode/settings.json可以提交，便于保留项目缩进约定；本机IntelliSense配置通过本教程复现，不提交绝对路径副本。

## 3. 开发板的外部晶振是否要启用

配套双USB核心板原理图第4页已抽取并渲染检查：

| 器件 | 标注频率与连接 | 用途 |
| --- | --- | --- |
| Y1 | 8 MHz，OSC_IN/OSC_OUT之间，带负载电容 | HSE外部高速晶振，可作为系统/PLL参考 |
| Y2 | 32.768 kHz，OSC32_IN/OSC32_OUT之间，带负载电容 | LSE外部低速晶振，通常用于RTC |

原理图页内存在不同系列的旧符号字段，板卡版本和实物完整料号仍按前期记录继续核对；以上是配套资料对晶振频率与接法的标注，不表示已经测量实物振荡波形或确认HSE启动。

当前HSI 8 MHz足以继续已通过的LED、串口和毫秒计时实验。板上有晶振并不意味着当前程序必须立即切过去，也不能从板上存在晶振推断固件已经在使用它。

建议把HSE切换作为下一独立实验，尤其在CAN联调前使用更合适的时钟精度基线。72 MHz是本器件可选的最高系统频率，不是所有BMS应用必须使用的频率。计划目标如下：

| 项目 | 后续目标 |
| --- | --- |
| RCC / HSE | Crystal/Ceramic Resonator，板上晶体与负载电容接法；不是Bypass外部有源时钟输入 |
| HSE输入频率 | 8 MHz，按实物与配套资料再核对 |
| PLL | HSE × 9 |
| SYSCLK / HCLK | 72 MHz |
| APB1 / PCLK1 | HCLK / 2 = 36 MHz |
| APB2 / PCLK2 | HCLK / 1 = 72 MHz |
| ADC时钟，启用ADC时 | PCLK2 / 6 = 12 MHz，满足14 MHz上限 |
| LSE | 暂不启用；需要RTC时再配置 |

该表是计划，不是已完成配置。本轮不修改.ioc、不重新生成，也不手工把SystemCoreClock变量改成72000000来代替真实时钟设置。后续由CubeMX生成振荡器、PLL、总线分频及Flash等待周期配置，再核对HAL时基、串口波特率及原有LED/消息节奏；修改时钟后重新计算相关外设参数。

## 本轮验收与Git

本轮先做编辑器配置：

1. main.c的五条未定义提示消失；若仍存在，提供Log Diagnostics中的当前配置、Defines、Includes。
2. 输入新C代码时采用四空格；只格式化用户代码选区，保留逻辑与消息内容。
3. CubeMX与main.c仍为HSI，当前不把时钟切换混入本轮。
4. 若用户改了C代码空白，Keil构建应保持0错误0警告。

编辑器配置提交已完成（9a6a533），以下首次收尾命令保留供复现，无需重复提交相同配置。后续本轮状态更新可与30号时钟实验一起收尾：

```powershell
Set-Location -LiteralPath "D:\嵌入式学习\电池管理系统"

git status --short
git add .gitignore .vscode/settings.json README.md docs
git add MCU/projects/bms_bringup/Core/Src/main.c
git diff --cached --stat
git diff --cached --check
git diff --cached

git commit -m "chore: set editor indentation and document bring-up results"
git push origin main
git status --short
```

c_cpp_properties.json应被新忽略规则排除，不加入上述暂存。Git由用户操作，助手没有执行任何写操作。

## 依据

- 本机Microsoft C/C++扩展1.34.4的配置及命令声明，现有Keil Assistant生成文件、.uvprojx及芯片头文件。
- [Microsoft IntelliSense配置说明](https://code.visualstudio.com/docs/cpp/configure-intellisense)，说明工作区配置、头文件路径与宏。
- [Microsoft缩进说明](https://code.visualstudio.com/docs/editing/codebasics#_indentation)，说明插入空格、宽度与自动检测。
- [Microsoft C/C++格式化说明](https://code.visualstudio.com/docs/cpp/cpp-ide#_code-formatting)，说明Format Selection及clang-format设置。
- 本地双USB核心板原理图第4页，负责Y1/Y2频率及连接依据。
- [ST STM32F103xC/xD/xE数据手册DS5792 Rev13](https://www.st.com/resource/en/datasheet/stm32f103ze.pdf)，核对72 MHz系统频率、APB1 36 MHz及ADC 14 MHz约束；本地较旧版手册没有被替换。

状态：前一步基本验收已通过；根配置、缩进设置及忽略规则已由用户实施并提交，编辑器诊断消失待明确反馈；HSE/PLL切换按30号教程单独实施。助手仅维护Markdown，未修改源码、.ioc、VS Code配置、.gitignore或Keil配置。

