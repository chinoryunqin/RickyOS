# Plus 电量与轻睡眠排查交接

记录日期：2026-10-22。面向接手排查的工程师；本文是当前状态入口，旧文档按各自阶段保留。

## 2026-10-23 最新进展（优先于下方旧轮次状态）

最新离线修复：[按键恢复失败阻止P14及CrossMux对照](plus-p14-restore-guard-20261023.md)。checked恢复逐次保留错误、验证寄存器，两个电轨入口都检查本轮成功状态；失败跳过P14/P5并锁存禁止再次睡眠。12项主机回归、前光脚本、Plus离线编译与源码检查通过。未刷机，未改充电参数，设备仍是此前恢复的turnC；电源毛刺根因未证明。厂商完整公开文件树与发布资产仍未找到整板原理图。CrossMux的S3适配已按固定提交核对，可参考错误处理/位所有权，不能套用其引脚、关机回退或历史验收结果。

后续仅离线核查：[P14复位审计](plus-p14-reset-offline-audit-20261023.md)。原厂与当前板级函数通过同一生产传输封装，各128轮正常位序列一致；P13不被正常P14恢复置高。故障注入复现“按键恢复忽略I2C错误，仍执行P14恢复”，原厂亦如此。**须修正下方“恢复按键”的含义：阶段16之后的记录只证明函数返回，不证明方向/输出写入成功。** 未确定电气根因，本轮未改生产固件或操作设备。

按用户指示先走原厂静态待机路线，时钟浅睡延后；40/80 MHz 清醒对照未安装，方向暂停。

- **static1**：用户确认画面/返回正常，但实际入睡前校验误判，设备保持清醒。新增 TCA9555 校验误把成对循环读取当作连续六字节；已修正，并补实际寻址模型回归。
- **static2**：用户确认静置时未按任何键，设备自行回主页。重连时 uptime 重新开始，RAM 环清空。使用对应 ELF 的 `s_reset_reason` 地址，通过 USB-JTAG 在 CPU running 状态读取到 `0xe = ESP_RST_PWR_GLITCH`；读取未复位、暂停 CPU、写 Flash 或改目标看门狗。这是芯片的复位分类，尚未取得模拟供电波形或确切失败阶段。
- **static3**：有效 RTC 记录 `reason=14 trace=1 last=17 seq=39 cycles=2 t=228737`。第二轮已从浅睡返回并清理唤醒源、恢复按键，随后止于17（恢复P14/P5电轨及紧接的扩展口检查），尚未释放/重挂SD、恢复USB或重建LCD。复位窗口已缩小；具体哪条电轨和模拟波形未取得。
- **static4失败，停止候选试验**：`reason=14 trace=1 last=27 seq=46 cycles=2 t=220288`，`wake cause=4 err=0 timers=2`。已证明两次Timer浅睡返回；第二次在P14恢复I2C写入窗口复位，未到P14写后阶段28，更未到P5写入阶段30。40ms间隔没有解决问题。用户质疑反复试验后，已停止候选刷入和拔线复测，保存记录并恢复测试前turnC。后续先离线核实原厂/本地状态与实际供电网络，不能将该窗口当成已证明的电气根因。

**当前设备已恢复测试前1.2.6-turnC**：完整活动槽回写与回读、槽外Flash MD5、正确ELF和启动/软件主页绘制校验全部通过。串口已释放，没有候选继续运行。恢复记录见 `.local/device-installations/restore-turnC-after-static4-standby-20261023/`。实际画面未另索取确认，用户无需继续试验。离线差异核对入口：[原厂实现差异与再次实机前门槛](plus-static-sleep-oem-differences-20261023.md)。

本轮安装前真实固件为 **1.2.6-turnC**，已只读备份完整活动槽并校验。回退应恢复 turnC，不应使用旧 tab3/1.2.5。身份、备份哈希、实现、各轮实机结果见 [静态待机候选记录](plus-static-standby-candidate-20261022.md)。充电参数、电量模型与 brownout 设置保持现状，3 mA 尚未验收。

下文“已恢复 tab3”“试验结束”等均描述此前 first-underrun 轮次，保留其证据，不代表新一轮设备当前状态。

## 先看结论

1. **轻睡眠真机验收仍失败，白屏根因未解决。** 最新一轮抓到首次错误：
   `0x01aa0001`，即 `ROW_NOT_READY / row=426 / producer=0`。
   它发生在第一次计时器唤醒后的刷新窗口。已定位到扫描消费行时生产者尚未提交，
   尚未定位生产者为什么落后。
2. **设备已恢复本轮测试前的 1.2.6-tab3。** 完整活动槽回写校验、槽外 Flash 校验、
   启动身份和软件主页绘制均通过；本轮恢复后的实际屏幕画面仍待用户确认。
   以前“恢复 1.2.5 且主页正常”是上一阶段记录，不是最新设备版本。
3. 满电基准的独立采样修复保留在电量分支：清醒主循环每 30 秒有一次采样机会，
   不再依赖页面重绘。离线回归通过，真机取得无重绘时仍采样的证据；
   **尚未在合格满电条件下验收基准建立，也未证明长期电量准确度。**
4. 普通构建默认关闭 Plus 轻睡眠；只有显式试验开关才开启。单独开启诊断不会开启睡眠。
   充电参数、BQ 配置及容量模型未修改。没有提交、推送或发布。
5. 本轮已结束物理试验，没有继续安装下一候选。接下来先请他人审查证据和代码。

后补证据：用户确认功耗 CSV 来自刚才同一台 Plus，运行我们自己的 RickyOS 1.2.5，
不是厂商固件测试。阅读与两段待机的电量计读数均约 57mA；
关机段计数变化很小，但不足以确认残余电流或彻底断电。见
[功耗 CSV 补充记录](plus-power-csv-evidence-20261022.md)。此文件不改变上述恢复版本记录。

后续降耗实现：已加入保持清醒的 40/80MHz 显式对照参数及显示首错后停止继续睡眠的保护，
见 [空闲降频候选](plus-awake-idle-optimization-20261022.md)。尚无新真机功耗结果，未安装该候选。

后补假设：用户指出关机页可以选择“空白屏保”。正常关机清屏不应算显示故障；
最新一轮仍有连续轻睡眠返回、短按退出待机及 row426 错误，不能仅由该设置解释。
此前 USB 失联轮没有完整现场，关机可能性不能只凭白屏外观排除，详见最新真机记录的补充节。

## 工作位置与版本

| 项目 | 值 |
|---|---|
| 实际工作树 | `/Users/rick/rickyos/wt-plus-battery` |
| 分支 | `codex/plus-battery-stability` |
| Git 基线 | `4455bf01e67895feb2517e1a8c626784a55c58ac` |
| 当前修改 | 未提交；仅 checkout 此分支不能取得全部修改，须带上工作区快照/补丁 |
| Plus 端口根目录 | `ports/metalio-e-ink4-plus/`，下文简称 `PORT/` |
| 固件目录 | `PORT/firmware/rickyos-plus-1.2/`，下文简称 `FW/` |
| 自有源码 | `FW/components/` |
| 恢复应用源码 | `FW/upstream/`，被 Git 忽略；修改通过 `upstream-plus.patch` 与 `port-changes.json` 交付 |
| 固定源码清单 | `FW/source-manifest.json`：2844 文件；适配校验 141 项 |
| 本地 IDF | `/Users/rick/rickyos/wt-plus/ports/metalio-e-ink4-plus/.local/esp-idf-v6.1-r` |
| 厂商只读源码 | `/Users/rick/Documents/ChatGPT/rickyos/vendor/metalio-plus-2.0.76/` |
| 目标 | ESP32-S31，RISC-V；不能套用普通 ESP32-S3 的架构假设 |

## 过程与每阶段得到的证据

| 阶段 | 采取的行动 | 结果与边界 |
|---|---|---|
| 电量调查与离线修复 | 查到满电证据采样受页面读取驱动；接入清醒主循环有界采样 | 无重绘的合成合格样本可在 180 秒建立基准；不改变原资格条件 |
| 首轮候选及诊断 v2 | 用户授权临时安装；增加既有 RAM 环回放和进入阻挡诊断 | 首轮没有真正入睡证据；v2 查到 GPIO5 低不等于手指按住，原充电分类字段也不能可靠判断物理 USB 插线 |
| 诊断 v3 | 读取实际触摸帧，补 USB 主机 SOF 检查；拔线后观察 | 有 TIMER、共享 GPIO 唤醒与软件侧键退出记录；用户报告静置期间白屏，重接后刷新报 `0x400`；复位后用户确认显示恢复 |
| 显示恢复 v4 | 睡前释放/保持显示总线，醒后重建 | 再次白屏，600 秒采集内 USB 未重新枚举；没有失败时 RAM 回放。用户长按恢复后回写原版 1.2.5，用户确认主页正常 |
| 后续离线修正 | 对照 OEM，补时钟引用配对、资源失败回滚、USB 恢复、EXT1/RTC 清理、供行错误交接 | 主机回归及普通/试验构建通过；不能据此宣称真机显示或 USB 已修复 |
| 首错诊断候选（最新） | 额外记录首次错误；再次获准临时真机试验 | 安装前发现当前已是 1.2.6-tab3，旧版本保护在写入前停止；重新只读备份当前版本后安装。再次白屏，但抓到 row426 首错及 33 条 RAM 记录；本轮 USB 能重接 |
| 最新恢复 | 恢复本轮测试前完整 1.2.6-tab3 活动槽 | 镜像、启动、软件绘制检查通过；实际画面确认待补。本轮不再做其他物理候选试验 |

前四轮原始过程见 [早期真机记录](plus-battery-device-acceptance-20261022.md)。
最新一轮应以 [首错候选真机记录](plus-first-underrun-device-trial-20261022.md) 为准。

## 最新真机时间线：为什么缩小到了首次刷新供行

下列时间取 RAM 消息内 uptime，不能按重新接 USB 后的打印时间排序。

| uptime（ms） | 记录 |
|---:|---|
| 150399 | 进入 48 秒睡眠，首错字仍为 0 |
| 198400 | TIMER 唤醒返回，P1=b7，没有确认电源键 |
| 198554 | USB 恢复软件步骤结束；不等于此刻已枚举 |
| 198565 | 显示资源重建返回 ok=1 |
| 198583 | 待机处理计时器刷新 |
| 199099 | 下次睡眠前，首错字已是 `0x01aa0001` |
| 199183 | EXT1 返回，P1=b7；这是共享中断，不能算电源键 |
| 258421 | 第二次 TIMER 返回，之前请求 54 秒睡眠 |
| 268902 | EXT1 返回，P1=37，P17 确认按下 |
| 269162 | 软件按短按退出待机 |

因此首次错误发生于首次计时器刷新的约 0.5 秒窗口内。后续两个 GC 刷新仍报 `0x400`。
四次资源重建均返回成功，软件也能处理按键退出，但光学显示仍失败。
首错记录只证明消费行时 ready 未置位，不能直接归因为 PSRAM、CPU 恢复、任务抢占或 LCD 时钟。
本轮保住 RAM 回放和 USB 重连，也不能据此解释第四轮 USB 完全失联的触发条件。

## 已修改的代码：请分别审查，不要把恢复缺陷当成初始根因

路径相对 `FW/`；行号对应本次交接快照。

| 文件/位置 | 修改或关键行为 | 已有验证 |
|---|---|---|
| `upstream/lib/hal/HalPowerManager.cpp:458`、`upstream/src/main.cpp:1107` | 清醒主循环每 30 秒尝试电量采样，复用原采样门和缓存，不额外唤醒或刷 UI | 合格/不合格电流、失败重置、长间隔、争用、计时回绕等回归；真机有无重绘采样日志 |
| `components/rickyos_application/port/PlusBoard.cpp:144,171,186,263,279` | 默认关闭；USB/触摸/音频/输入保护，GPIO2 EXT1 ANY_LOW + 定时器，RTC 所有权清理；醒来先锁存 P17，再 USB、显示恢复 | 真实板级代码主机测试，输入竞态及失败出口；最新真机仍白屏 |
| 同文件 `:121` | 按 OEM 策略重复请求硬件关机；不写 BATFET/充电配置 | 离线逻辑验证；没有关机残余电流/彻底断电实测 |
| `components/metalio_board/display/output_lcd/lcd_driver.c:396,470,890,900,1021,1023` | LCD/RMT 时钟源取得/释放配对；checked 初始化及部分资源回滚 | 4000 次时钟生命周期、失败注入回归 |
| `components/metalio_board/display/epd_board_metalio_eink4_plus.c:347,701` | checked 总线重建，失败保持安全状态，不继续发显示波形 | 失败路径测试；资源重建成功并不证明物理扫描正确 |
| `components/metalio_board/display/output_lcd/render_lcd.c:162,195` | 出错先停 DMA，等已派发供行任务退出并消费晚到通知，最后统一清队列；生产者不自行清活跃队列 | 旧代码负对照能失败；修正后注入超时、晚通知、三相恢复和卡死保护通过 |
| `components/metalio_board/display/output_lcd/lcd_fault.h:17`、`render_lcd.c:30,56`、`phase_lookahead.inc` | 内部 RAM 4 字节、原子 CAS 只记本次启动首错；不在 ISR 打印，不加堆/任务/持久化 | 三种故障注入能区分；真机获得 `0x01aa0001` |

持续不响应的供行任务在第二次 2 秒等待失败后沿用已有 lookahead 的 `abort()` 保护；
这会进入重启策略，不能宣称所有错误都可无重启恢复。普通成功刷新不增加这段等待。

## 厂商源码和 SDK 已查到什么

- OEM `main/boards/common/power_policy/power_hw.cc:90,120,293`：EXT1、USB 恢复及睡眠顺序。
  OEM 醒来会先锁存 P17，再恢复其他资源。本地已对齐相关顺序及 RTC 引脚清理。
- OEM 卸载 SD、切换 CNNT 和外设电源的完整策略没有直接照搬：本地可能有打开文件，
  需先明确 HAL 所有权，不能在底层突然卸载。触摸任务模型也不同。
- OEM 普通渲染错误路径也有晚通知/清队列问题；厂商源码不能自动视为正确答案。
- 当前 S31 SDK 的睡眠路径先等 PSRAM 事务、半睡并保持脚位；醒后恢复 MPLL/脚位并退出半睡。
  实际配置和 RISC-V 对象反汇编均显示：等待 PSRAM resume 完成后，才恢复另一核心。
  未找到“少等一次半睡恢复”的软件证据；未改 SDK 等待参数或 PSRAM 频率。
- LUT、ready map、行队列、供行 scratch、DMA bounce 与描述符位于内部内存；
  供行任务会读取 PSRAM 图像，ISR 消费已提交的内部行。S31 内部地址区不是 P4 的缓存别名布局，
  不能把 P4 的内部 SRAM 缓存修复直接套用。
- LCD 核心/像素父源、RMT 来源及重建编程已审查；名义 CKV=10MHz。
  没有示波器证据证明睡醒后真实 PLL、PCLK、CKV 的频率或稳定时间正确。
- 厂商“待机 3mA”仅是用户转述，测试条件未知，本机未测得该指标。
  BQ27220 是电量计，CX25601N 是充电芯片；“读 PMU”不足以说明具体寄存器或标定策略。
  读取芯片不等于百分比准确；满电资格、容量模型和充电参数均没有因此改动。

## 离线验证与它不能证明的部分

完整 Plus 主机套件最终退出码 0；两个 Read Pico 专用测试按平台跳过。
本次 PSRAM SDK 测试显式提供路径，确实运行，未跳过。

| 验证 | 结果 | 不覆盖 |
|---|---|---|
| 真实渲染控制器/供行线程/原子行队列，ASan/UBSan | 35 次程序运行；停在 row193、帧提前结束、帧完成丢失分别记录正确首错 | 真机调度、物理 PSRAM、DMA 时序 |
| LCD/RMT 时钟引用及重编程 | 4000 次生命周期通过 | 真实 PLL 锁定、电气时钟 |
| 真实首帧配置/填充/EOF 函数，假硬件 | 1000 次陈旧状态循环；688 行含 padding，完成通知一次 | 完整真实睡眠、RTOS 和光学显示 |
| 真实 SDK PSRAM resume 与 RTC 换算 | 72 组等待/MR4 条件通过 | RAM 保持、CE 波形、物理恢复时间 |
| 分配失败及清理、RTC/EXT1 输入竞态 | 对应主机回归通过 | 多轮真机堆碎片、共享 IRQ 真实来源 |
| 固定源码恢复/适配 | 2844 文件、141 项适配通过 | 硬件行为 |
| 普通电量/显式睡眠诊断两种 S31 固件 | 均完整编译、链接检查通过 | 白屏已修复、3mA、续航、电量精度 |

复现命令在仓库根目录执行（需要恢复的固定应用源码及对应依赖）：

```sh
python3 ports/metalio-e-ink4-plus/scripts/check_rickyos_plus_sources.py
bash ports/metalio-e-ink4-plus/tests/run_plus_port_tests.sh
RICKYOS_IDF_PATH=/Users/rick/rickyos/wt-plus/ports/metalio-e-ink4-plus/.local/esp-idf-v6.1-r \
  python3 ports/metalio-e-ink4-plus/tests/test_plus_psram_resume.py -v
```

固定源码恢复入口为 `PORT/scripts/restore_rickyos_plus_sources.py`，其清单记录应用及 FreeInk SDK
提交与 SHA256；可提供已有 Git 对象仓库作离线源。完整 ESP-IDF 工具链、OEM 源码和其他依赖
不随轻量审查包分发。审查包是代码/证据快照，不能解压后直接当完整独立工程编译。

## 关键镜像与回滚依据

| 对象 | BIN SHA256 / 信息 |
|---|---|
| 最新失败诊断候选 | `5534044199c6ffe4ad7647aca0a274f8eb265bf83ab9478250d9f3c651a443dd`，4915584 字节 |
| 普通电量、睡眠关闭，未安装 | `7d1b6a28df6ca4fffa1d48e586f9a9286aaa3b692f18ce733302c1495ba777e6`，4907792 字节 |
| 本轮原版 1.2.6-tab3 完整活动槽备份/回写 | `836631827c6b999c12cb6f71340230759af7f2bed89e40071bfd7b0550414bc8`，6553600 字节 |
| 原版启动 ELF SHA256 | `d962e720d887eb83cbb7b4a3e4bdc889ce39f31af1db7e7c177ce3ee795e969f` |

最新临时写入和恢复均限活动 app0（`0x10000`），回读通过，槽外 Flash 摘要前后一致。
不重写 bootloader、分区表、OTA 选择、另一应用槽、NVS 或 SD。
正常应用运行可能按原行为保存状态；不能宣称运行期间所有设置/存储都完全不变。

## 原始资料位置与阅读顺序

以下 `.local/` 均在 `PORT/` 下；历史 verification.json 是当时快照，
其中“未安装/原版 1.2.5”等字段不能覆盖最新设备记录。

1. 最新失败原始日志、33 条去重 RAM 时间线及恢复状态：
   `.local/device-installations/plus-first-underrun-trial-20261022-attempt2/`
   中的 `physical-trial.log`、`physical-trial.connections.jsonl`、`trial-analysis.json`、
   `trial-status.json`、`evidence-manifest.json`。
2. 最新原版恢复：`.local/device-installations/restore-current126-after-first-underrun-20261022/`
   的 `installation.json`、`boot.log`；测试前备份在
   `.local/device-installations/plus-current-before-first-underrun-20261022/`。
3. 最新离线证据：`.local/offline-first-underrun-20261022/` 的 `verification.json`、
   `restore-order.json`、反汇编与链接符号；最终套件日志
   `.local/test-first-underrun-full-final-20261022.log`。
4. 旧真机第三、四轮：`.local/device-installations/plus-standby-sleepdiag-v3-20261022/`
   与 `.local/device-installations/plus-standby-display-recovery-v4-20261022/`。
5. 详细阶段说明：
   [电量采样](plus-battery-awake-sampling-20261022.md)、
   [OEM 对照](plus-oem-power-offline-comparison-20261022.md)、
   [显示/USB 恢复](plus-display-usb-offline-recovery-20261022.md)、
   [EXT1](plus-ext1-offline-candidate-20261022.md)、
   [渲染错误交接](plus-render-recovery-offline-20261022.md)、
   [显示时钟/PSRAM/首错调查](plus-first-underrun-offline-20261022.md)。

## 请接手者重点判断

1. 第一次醒来后，producer0 到 row426 时为什么未及时提交：源图像/PSRAM 访问、
   任务调度与跨核同步、CPU 恢复、扫描消费节拍，哪些观察能将它们区分？
   现有首错不含生产者当时执行位置、耗时或实际时钟，不能先选定一种根因。
2. 首次计时器刷新实际使用什么刷新路径/相位，是否有仅在睡眠恢复后出现的 ready/队列/通知交接？
   请连同 GC 与 lookahead 两种路径审查；错误清理已修复不代表起始断供已解决。
3. 如何在不影响 ISR 节拍的前提下，记录少量生产者进度、扫描进度及醒后时钟/内存状态，
   并在首次故障后停止继续入睡、保留现场？这只是待审查方向，尚未实现新的候选。
4. 第四轮的 USB 完全失联缺少现场，是否应单独设计诊断，避免把它硬归并为此次 row426？
5. 若需要下一轮硬件试验，先给出可区分假设的单变量方案、观测点和回滚条件。
   历史试验已获准不代表本文要求立即刷机；本轮仅整理交接，充电参数继续保持原值。

尚缺：真实 PCLK/CKV/电源波形、生产者停顿位置、完整失联现场、独立电池侧功耗积分、
满电基准实机建立及恢复主页的用户画面确认。请不要把主机测试数量当作这些缺口的替代证据。

## 随附审查包

本地 `PORT/.local/review-handoff-20261022/plus-battery-sleep-review-20261022.zip`
包含本报告及各阶段文档、自有组件快照、恢复应用的关键文件、补丁/清单、测试与脚本、
选定的真机日志和离线证据、工作区差异及逐文件 SHA256 清单。
不含可刷写镜像、用户 Flash/NVS/SD 备份、完整 SDK 工具链或自动安装/恢复脚本。
只是本地整理，尚未发送给任何人。

## 2026-10-22 补充：耗电实测与原厂待机做法

实测醒着约 58 mA（阅读静止与时钟待机相同），以及原厂 2.0.76 待机 3 mA 的做法（浅睡分片、计时器醒来不刷屏、完整的外设入睡/唤醒序列）和建议路线，见
[待机省电：按原厂做法的路线](plus-standby-oem-approach-20261022.md)。
