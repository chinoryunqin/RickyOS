# Plus 白屏与 USB 失联：离线错误恢复修正

日期：2026-10-22（Asia/Shanghai）。工作树 `/Users/rick/rickyos/wt-plus-battery`，
分支 `codex/plus-battery-stability`，基线 `4455bf01`。
本轮没有打开串口、复位、刷机或操作设备；充电参数、BQ 配置、容量模型均未改。
没有提交、推送或发布。设备维持上次恢复的原版 1.2.5，用户当时确认主页正常。

后续唤醒配置已对齐厂商 EXT1，并补齐 RTC 脚位恢复；见
[EXT1 离线候选](plus-ext1-offline-candidate-20261022.md)。本文保留显示/USB 修正与对应构建记录。
普通刷新供行超时后的队列/通知交接也已继续修正，见
[供行错误恢复](plus-render-recovery-offline-20261022.md)。

## 证据与判断边界

之前第三轮诊断记录有 `line buffer underrun occurred!`、刷新 `result=0x400`，
第四轮用户报告白屏，重接和短按后 USB 没有枚举，但未收到失败时的 RAM 回放。
因此不能把第四轮自动归因为同一个 0x400，也不能由离线测试证明物理白屏已经解决。

本次对照的厂商目录仍为
`/Users/rick/Documents/ChatGPT/rickyos/vendor/metalio-plus-2.0.76/`：

- `main/boards/common/power_policy/power_hw.cc:120-138,367-378`：醒后恢复按键、电源轨，
  做过 SD 处理时先挂载 SD、恢复 USJ 调试接口，再恢复显示总线。
- `main/boards/metalio-e-ink4-plus/periph/metalio_sd.cc:29-47,91-116`：
  厂商会卸载 SD、改 CNNT 选择和脚位，再在醒后挂载。该流程不能直接作用于本地仍有打开文件的 SD。
- 厂商 `sdkconfig` 同样开启 CPU 掉电及 PSRAM 半睡。它们仍可作为控制变量调查，
  但不能因为本地开启了这些选项就断言它们是根因。

本地原显示恢复链是：`bus_restore()` → `epd_lcd_reinit()` → `epd_lcd_init()`。
后者重新申请两块 bounce buffer、DMA 描述符、两个中断和 DMA 通道，任一返回错误便 `abort()`，
且没有清理已经申请成功的部分资源；上层没有错误返回值，也不单独恢复 USB。
这是源码能确认的失效路径，并非已经证实它在第四轮真机发生。

## 已修改的软件路径

以下路径相对 `ports/metalio-e-ink4-plus/firmware/rickyos-plus-1.2/`。

### 显示重建失败可返回、可清理、可重试

`components/metalio_board/display/output_lcd/lcd_driver.c:1034` 新增
`epd_lcd_init_checked()`。它在内存、中断、DMA 配置失败时清理本次已经取得的资源并返回 false，
包括第二个中断失败、第一个或第二个 buffer 已分配、DMA 通道已分配但未连接、
连接后配置/回调失败等部分状态。DMA strategy 的错误也不再被忽略。

- DMA 清理先停通道，再按实际连接状态断开/删除并清空句柄；没有连接时不会错误地断开。
- 中断只释放实际取得的句柄并置空，避免部分初始化失败时释放无效句柄。
- LCD 源时钟引用仍沿用上轮修正的配对释放。
- 正常反初始化先移除数据/完成回调、停止 DMA，再释放缓冲；重复反初始化无副作用。
- 重复初始化会返回失败，不再覆盖已有资源；后续正常反初始化仍能释放原资源。

冷启动入口 `epd_lcd_init()` 保留失败终止的原契约；睡眠恢复改用 checked 入口，
因此本轮不宣称整个固件所有启动或硬件错误都不会终止。
没有新增任务、堆对象或缓冲，只增加少量生命周期布尔状态和错误返回值；
仍复用已有分配。内存压力下的物理长期稳定性没有离线证明。

`components/metalio_board/display/epd_board_metalio_eink4_plus.c:347` 新增
`bus_restore_checked()`：失败时维持“总线尚未恢复”、清除 TPS 就绪状态并返回 false，
后续可重新尝试。`epd_board_poweron():701` 检查返回值，重建未成功就不升起显示驱动路径、
不向不存在的 LCD/DMA 资源提交波形。这里的 TPS 是屏幕电源，不涉及电池充电器。

### USB 恢复与显示分离

`components/rickyos_application/port/PlusBoard.cpp:51` 新增调试接口恢复步骤，
采用厂商 USJ 顺序：开 USB 总线时钟、关闭 PHY pad、等待 150ms、恢复 PHY 默认值、重新启用 pad。
只在已有 USB Serial/JTAG console 配置下编译其硬件操作。

此步骤放在睡眠返回后、侧键锁存和唤醒配置清理之后、显示重建之前。
睡眠请求返回错误/被拒绝时也执行，因为睡眠准备可能已经改变过时钟/pad。
未实际调用睡眠的提前拒绝路径不改 USB。它不等待主机 SOF：拔线状态正常，不等待 800ms 直到超时。
这会在每次实际睡眠尝试返回后增加约 150ms 唤醒开销，本轮没有量测其功耗影响。

保留厂商控制思想，但不复制 SD/CNNT 卸载、脚位选择或 USB 磁盘路由切换；
不因恢复 USJ 而让未静止的文件句柄失效。恢复寄存器顺序通过主机替身验证，
**真实 USB 重新枚举没有经过新一轮设备验证，不能将软件函数执行完当作枚举成功。**
如果主控根本没有从睡眠函数返回，这个恢复步骤也无法执行。

显示重建失败会锁存 `sleepFault`，停止本次启动后续轻睡眠，返回普通循环；
不再重复睡眠/重建失败循环。普通绘制以后仍可经 checked 路径重试显示。
USB 恢复不受 LCD 内存/中断申请是否成功影响。

## 离线验证

- `test_plus_lcd_restore_failures.py`：2 项。执行真实 checked 初始化、buffer 分配/释放、
  DMA 建立/销毁、反初始化及板级恢复方法。覆盖 3 个 buffer/描述符分配位置、
  两个部分中断阶段、5 个 DMA 阶段、部分 GPIO 设置失败，共 11 种故障。
  每次失败后所有模型资源归还，下一次重试成功；重复初始化/反初始化无泄漏。
  另运行 1,000 次完整资源建立/销毁。该测试的 GPIO 失败检查是外层错误分支注入，
  不声称本地原 GPIO 初始化里的每个 LL 调用已有错误返回。
- `test_plus_lcd_clock_lifecycle.py`：1 项。新增实际中断申请函数在第一个/第二个申请失败的回滚检查；
  原有两种核心时钟接口、有/无其他引用者的 4,000 次生命周期继续通过。
- `test_plus_standby_sleep.py`：4 项。真实后端默认关闭/显式打开与普通/诊断四组合继续验证；
  USB 开时钟、断开至少 150ms、恢复默认、重接顺序受检查，发生在 LCD 恢复之前。
  没有主机也不额外等待，睡眠错误同样恢复，显示失败后仍有 USB 恢复动作并停止后续睡眠。
  原有慢显示恢复丢键、无效端口读、清理失败等回归保留。
- 满电采样定向回归保留；完整 Plus 主机回归和两种 S31 构建结果记录于本地
  `.local/offline-display-usb-recovery-20261022/verification.json`。

上述主机测试启用 ASan/UBSan；硬件由替身取代，能检查顺序、资源归还及错误传播，
不能模拟电气时序、PSRAM 保持、玻璃图像或 USB 总线枚举。

## 构建结果

完整 Plus 主机套件最终退出码为 0。试验开启/诊断开启和普通电量/睡眠关闭两种 S31 构建均通过，
包括链接后的触摸后端、时钟对象、网络内存配置及固定源码检查（2844 个文件、141 项适配）。
普通构建编译参数中没有睡眠试验或诊断定义。产物仅保存在上述本地目录。

| 配置 | BIN 字节 | BIN SHA256 | ELF SHA256 |
|---|---:|---|---|
| 试验开启、诊断开启 | 4,915,584 | `02f02302414230ac37241adfa749174d1c754ed7194f67600bb80e7f5932a329` | `e7d7a9e77ebcd96421584eaacad689c1e6107c5a4608fba358a5d07450df2215` |
| 普通电量、睡眠关闭 | 4,907,072 | `fa7f176029ddbd4b0c58d0d96d14a2dac7ce1441e29c8fb74047e0ca12fe957a` | `3e9f3319a70c0ab306caed76bba2a6d9196959a22191436028e0ef09e42649b7` |

## 当前状态

轻睡眠仍默认关闭，诊断不会打开它。新产物仅离线保存，不安装。
此次修正关闭了一条显示重建失败导致程序终止的路径，补上独立 USB 恢复步骤；
白屏的供行欠载与第四轮失联的准确触发条件仍未确认，不能宣布硬件问题已修好或达到 3mA。
厂商完整 SD/触摸/电源轨生命周期仍需匹配本地 HAL 所有权后才能迁移，当前不绕过 HAL 卸载 SD。

复现：

```sh
python3 ports/metalio-e-ink4-plus/tests/test_plus_lcd_restore_failures.py -v
python3 ports/metalio-e-ink4-plus/tests/test_plus_lcd_clock_lifecycle.py -v
python3 ports/metalio-e-ink4-plus/tests/test_plus_standby_sleep.py -v
bash ports/metalio-e-ink4-plus/tests/run_plus_port_tests.sh
```
