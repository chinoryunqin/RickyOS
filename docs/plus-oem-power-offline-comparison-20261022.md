# Plus 厂商电源流程对照与离线修正

日期：2026-10-22，Asia/Shanghai。工作树 `/Users/rick/rickyos/wt-plus-battery`，
分支 `codex/plus-battery-stability`，基线 `4455bf01`。
本轮严格离线：未打开串口、复位、刷写或访问设备；未改充电参数、BQ 配置或容量模型，
未提交、推送或发布。设备状态沿用此前已恢复的原版 1.2.5，用户确认主页正常。

后续更新：已继续修正显示重建失败的错误传播与部分资源回滚，并把 USB 恢复放在显示重建之前。
最新行为与验证边界见 [显示与 USB 离线恢复](plus-display-usb-offline-recovery-20261022.md)。
侧键唤醒方式的后续调整见 [EXT1 离线候选](plus-ext1-offline-candidate-20261022.md)。
下文保留上一阶段记录；其中“显示重建失败会 abort”的描述已由新睡眠恢复入口修正，冷启动契约仍保留。

## 可确认的厂商实现

厂商只读对照目录：
`/Users/rick/Documents/ChatGPT/rickyos/vendor/metalio-plus-2.0.76/`。
以下厂商路径相对此目录，修正路径相对本工作树
`ports/metalio-e-ink4-plus/firmware/rickyos-plus-1.2/`。
源文件 SHA256 固定在本地忽略目录
`ports/metalio-e-ink4-plus/.local/offline-oem-comparison-20261022/comparison.json`。

| 环节 | 厂商源码依据 | 本轮处理 |
|---|---|---|
| 页面和后台任务 | `main/boards/common/power_policy/power_policy_standby_lp.cc:365-379` 先让触摸睡眠、等首帧、停放显示，再运行睡眠循环；`main/display/lv_adapter_epdiy.cc:599-632` 冻结提交、暂停其触摸任务 | 本地 Activity 已持有 RenderLock 并等显示完成；厂商触摸任务与 SDK 轮询不同，不能直接调用其暂停函数假装完整静止 |
| 睡前外设 | `main/boards/common/power_policy/power_hw.cc:293-354` 配合 SD、TPS、显示总线、运动中断、按键方向、外设电源，然后 EXT1/计时器睡眠 | 保留默认关闭的侧键/计时器候选，未复制会改变扩展器方向或切断 SD/触摸等电源的完整流程 |
| 唤醒侧键 | 同文件 `357-359`，恢复外设之前锁存 P17 | 修正候选先读端口锁存，再清理中断、重建显示，避免恢复耗时覆盖短按 |
| USB 恢复 | 同文件 `106-138,373-378` 明确是在做过 SD hold/reinit 后恢复 USB，再恢复 LCD | 不能把其 USJ 软拔插直接当作本地 USB 失联的确定修复，本地未采用相同 SD 生命周期 |
| 真实关机请求 | 同文件 `260-273` 循环发送 PWR_KEY 脉冲，每组后等 200ms | 对齐重试行为，停止一次性请求后永久空等的路径 |
| 电量来源 | `main/boards/common/power/bq27220_gauge.cc:11-12,60-82,118-186` 读 BQ27220 电压/电流，用电压估算、滤波、限速显示；不是直接采用原始 SOC | 本轮不改电量算法，保留独立周期采样修复；PMU 指代仍需区分电量计与充电器 |

在厂商上述电源、文档和板级实现中未找到可以复现“整机待机 3mA”的测量记录或明确测量条件。
厂商口头指标不能当作本次候选的实测结果。关机脉冲流程也不能证明所有负载已断电，
仍需后续独立电流测量；本轮没有进行该测量。

## 本轮已修正

### 唤醒时先锁存侧键

`components/rickyos_application/port/PlusBoard.cpp:235-260`：
成功从睡眠返回后，先在普通 GPIO ISR 仍屏蔽、显示总线尚未恢复时读取并锁存 P17。
之后无论端口读是否成功，都清理唤醒配置并恢复显示资源；只有有效锁存才允许分类按键。
不能把读取失败、共享中断或配置失败当成一次短按。

这修正的是顺序造成的可复现漏键，不承诺抓住首次读端口前就已经结束的极短脉冲。
长按最终仍由原主循环处理，显示重建耗时对长按计时的影响尚需实机验证。
诊断模式增加锁存、显示恢复开始/结束标记，便于区分下次失败发生在哪个阶段；不新增持久化写入。

### 配对释放 LCD/RMT 时钟引用

`components/metalio_board/display/output_lcd/lcd_driver.c:460-471,982-1014`：
现有初始化分别取得 RMT、LCD 像素和本地 IDF 支持时的 LCD 核心时钟源引用。
原反初始化只停止/释放外设和总线时钟，没有释放这些源引用；每次总线重建都会增加引用。
本地 IDF `components/esp_hw_support/include/esp_private/esp_clk_tree_common.h:84-96`
及 `port/esp32s31/esp_clk_tree.c:510-590` 明确该 API 使用引用计数。

本轮在硬件停止后配对释放自身引用，保留其他使用者持有的源引用。
RMT 仅在原来取得源引用的 IDF6 分支释放，LCD 仅在原来取得引用的 S31 分支释放；
旧版没有核心时钟 API 时，也不会额外释放不存在的核心引用。
不增加堆对象、任务、缓冲，不修改时钟频率、波形或充电寄存器。

该泄漏是确定的生命周期缺陷，但没有证据表明它是此前白屏或 USB 消失的唯一原因。
既有 LCD 重建仍会释放再分配原来的 DMA 缓冲/中断，初始化失败仍会 abort；本轮未改变这个边界。

### 关机请求对齐厂商重试

`components/rickyos_application/port/PlusBoard.cpp:103-113`：
保持原有音频关闭、厂商板级脉冲波形和关机前主流程；若脉冲函数返回，说明 CPU 仍在执行，
每组间隔 200ms 再发相同脉冲，直到硬件移除主控供电。
复用现有调用栈，无新增关机任务或堆分配，也不写 CX25601N/BQ/BATFET 配置。

原 SDK 在关机回调返回后会每秒空等，故只发一组脉冲会停留在“仍通电但无法正常使用”的状态。
本轮避免这一退路；并不因此宣称硬件已可靠断电，USB 供电时的实际行为和残余电流仍需单独验收。

## 离线验证

- `tests/test_plus_standby_sleep.py`：4 项通过。真实板级后端覆盖普通/诊断、默认关闭/明确启用四组合；
  新增显示恢复耗时 100ms/1000ms 时短按已释放、同时计时器与侧键、端口读错等情形；
  错误路径仍恢复显示资源，不冒充按键。关机方法以第三组脉冲时模拟断电，确认前两组返回后仍重试，
  音频仅关闭一次、没有返回 SDK 空等；模拟断电用的异常仅存在于主机测试，固件不使用异常。
- `tests/test_plus_lcd_clock_lifecycle.py`：1 项通过。执行实际 LCD/RMT 初始化和反初始化函数；
  两种本地支持的 LL 核心时钟接口、有/无其他使用者各 1,000 轮，所有源引用、总线引用及中断数回到起始值，
  释放发生在外设关闭后。ASan/UBSan 启用；不模拟电气时序。
- 对修改前的真实 `HEAD` 驱动执行同一时钟测试，第一次生命周期即因源引用未归还而被拒绝，
  证明新回归能够捕获该问题；这是预期失败的对照，不是修正版测试失败。
- `tests/run_plus_port_tests.sh` 完整套件通过，包括满电采样、显示队列/相位、输入、USB、前光和录音等回归。
- 固定源码检查通过：2844 个文件、141 项适配。此轮修改均为自有组件/测试；固定应用与 SDK 基线不变。

构建结果另记本地同目录 `experimental-build.json` 和 `default-build.json`。
两者只为离线验证保存，不是发布镜像，不安装到设备。

| 配置 | BIN 字节 | BIN SHA256 | ELF SHA256 |
|---|---:|---|---|
| 明确开启试验、诊断开启 | 4,914,816 | `d989f5234afa39d317dd3820baf395ba2d1cddd81be21a9c287d230f49a73191` | `3ad4174e63fdcc2afbeacbb9c705f9116b34e489faef58079102b0fa2af99cf8` |
| 普通电量、睡眠及诊断关闭 | 4,906,544 | `b603e5c2cf5aca6fcd9ae879df442d64080a594feeeee93d0e436792a16b8d20` | `ad1b9ef359ad8412ed0471510269a342e059e7c81709a7384a04db10624c790f` |

两种构建均通过链接后的 Plus 触摸后端、时钟对象及网络内存配置检查。
普通构建的 PlusBoard/LCD 编译命令已核对没有睡眠试验或诊断定义。
主机测试已加入完整 Plus 回归入口，源码及结果摘要记录在本地同目录 `verification.json`。

## 尚未消除的风险与下一步

轻睡眠继续默认关闭，诊断开关不能打开它。本轮没有证明白屏/USB 问题已解决。
厂商会停放/重建 SD、切断部分外设电源、改变 TCA 方向并处理共享中断；
本地 SD 管理器保留已打开的书籍/缓存文件，现有 `shutdown()` 是深睡前的终止入口，
不是可以直接搬用的浅睡暂停/恢复接口。必须先建立文件使用者静止和句柄保持契约，
才能对齐那部分厂商流程；不能在普通文件访问仍可能发生时绕过 HAL 卸载 SD。

本地候选配置还启用 CPU 掉电/动态保持分配与 PSRAM 半睡；它们是后续需分别控制的调查因素，
没有足够证据指定其中任何一个为根因。本轮未更改这些配置。
后续如需新候选，应先离线验证完整恢复契约和内存失败路径，再另行安排设备验证；本轮不刷机。

复现主机回归：

```sh
python3 ports/metalio-e-ink4-plus/tests/test_plus_standby_sleep.py -v
python3 ports/metalio-e-ink4-plus/tests/test_plus_lcd_clock_lifecycle.py -v
bash ports/metalio-e-ink4-plus/tests/run_plus_port_tests.sh
python3 ports/metalio-e-ink4-plus/scripts/check_rickyos_plus_sources.py
```
