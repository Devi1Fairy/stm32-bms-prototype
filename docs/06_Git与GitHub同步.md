# Git 与 GitHub 同步

日期：2026-10-15。用户已明确选择创建公开仓库。[Devi1Fairy/stm32-bms-prototype](https://github.com/Devi1Fairy/stm32-bms-prototype) 已创建，本机已初始化 main 分支并关联 origin。以下记录首次上传步骤和日常同步方法，首次推送的完成结果见开发记录。

## 1. 为什么用 Git 同步

Git 在本机保存提交历史，可以比较修改和恢复之前的版本。GitHub 托管远端 Git 仓库，方便备份、展示与协作。

基本流程为：修改工作区文件 → git add 暂存本次内容 → git commit 保存本地版本 → git push 发送到 GitHub。保存文件不会自动上传；commit 也只发生在本机，push 才更新远端。

本项目在 D:\嵌入式学习\电池管理系统 根目录管理一个仓库，将文档、CubeMX 配置和 MCU 工程一起记录，而非仅管理 main.c。

## 2. 哪些文件提交

| 内容 | 处理与原因 |
| --- | --- |
| README.md、docs、项目截图 | 提交，保存需求、进展与实际证据 |
| .gitignore、.gitattributes | 提交，共享忽略规则和文本换行规则 |
| .ioc | 提交，可复现 CubeMX 配置 |
| Core、必要 Drivers | 提交，包含当前构建所用源码与头文件 |
| .uvprojx、.uvoptx、启动汇编 | 提交，保留构建、调试选项和启动代码 |
| 第三方 LICENSE 与版权声明 | 随对应代码保留 |
| MDK-ARM/bms_bringup 构建输出目录 | 忽略，包含可重新生成的 AXF、HEX、MAP、对象文件和构建日志等 |
| .uvguix 用户界面布局、备份文件 | 忽略，减少个人界面变化造成的提交噪声 |
| 当前未引用的 CMSIS/DSP 与 CMSIS/Lib | 忽略，这些复制的示例、DSP 源码和预编译数学库未被当前目标使用 |

忽略不会删除本地文件，也不影响当前 Keil 构建。实际编译引用来自 .uvprojx；当前源文件与 IncludePath 均不依赖上述 DSP/Lib 目录。以后添加 DSP 时，要同步调整忽略规则。

不统一忽略所有 .lib、.sct 或 .uvoptx，以免以后遗漏确实需要共享的第三方库、自定义链接脚本或调试配置。当前自动生成的 .sct 位于已忽略的构建目录，项目没有使用自定义 ScatterFile。

首次编译结果已经以文本保存在开发记录，因此构建日志不纳入 Git 后，仍有可阅读的基线证据。需要交付固件时，后续可以使用 GitHub Release 发布 AXF/HEX。

## 3. 首次初始化与上传：每条命令的作用

以下为首次同步的步骤与复现说明。初始化和首次提交完成后，日常只执行第 4 节的流程，不必重新初始化。

在 PowerShell 进入项目根目录：

```powershell
Set-Location -LiteralPath 'D:\嵌入式学习\电池管理系统'
```

初始化本地仓库：

```powershell
git init -b main
```

生成本机 .git 目录，开始管理版本；main 为当前主分支名称。不会上传文件。当前 Git 已配置作者姓名与邮箱，本轮沿用既有配置，不覆盖全局设置。

创建 GitHub 公开空仓库 stm32-bms-prototype。远端初次创建不自动生成 README、.gitignore 或 LICENSE，避免与现有本地内容产生两套初始历史。用户已授权公开创建，本轮可使用已有 GitHub 登录执行。

关联远端：

```powershell
git remote add origin https://github.com/Devi1Fairy/stm32-bms-prototype.git
git remote -v
```

origin 是远端仓库地址的本地别名，remote -v 用于核对地址。关联远端本身不上传内容；已有 origin 时先检查，不重复添加或覆盖。

选择并检查第一次提交：

```powershell
git status --short
git add .
git diff --cached --stat
git commit -m "chore: initialize STM32 BMS bring-up project"
```

status 查看变化；add 将当前未被忽略的变化放入暂存区；diff --cached --stat 查看即将提交的文件统计；commit 保存一个本地版本，并附上说明。

首次推送：

```powershell
git push -u origin main
```

将本地 main 的提交发送到 origin；-u 设置 main 跟踪 origin/main，以后通常可以直接运行 git push。

## 4. 后续每次同步

完成一个小目标，例如 UART 日志通过，再保存相应源码和实际开发记录，然后执行：

```powershell
git status
git diff
git add .
git diff --cached --stat
git commit -m "feat: add UART logging"
git push
```

示例提交信息仅用于说明，只有实际完成 UART 功能后才使用。修改文档可以使用 docs: explain ADC configuration 这样的说明；每个提交尽量表达一个具体变化。

若在另一台电脑或 GitHub 网页修改过仓库，开始本机新工作前先运行 git pull --ff-only。它尝试以快进方式取得远端变化，若本地和远端历史分叉会停止并提示，届时按实际情况处理。

GitHub 网页应能看到提交历史、源码、.ioc、Keil 工程和文档，构建输出及个人布局不出现。git status 应显示工作区干净，main 与 origin/main 对齐。

本项目后续按可验证目标提交：最小工程、GPIO、UART、ADC、保护与状态机、CAN、FreeRTOS、故障测试。计划功能仍在 README 中明确标为待实现。

参考：[GitHub 官方本地项目上传流程](https://docs.github.com/en/migrations/importing-source-code/using-the-command-line-to-import-source-code/adding-locally-hosted-code-to-github?platform=windows)、[Keil 工程文件类型](https://www.keil.com/support/man/docs/uv4cl/uv4cl_b_filetypes.htm)。
