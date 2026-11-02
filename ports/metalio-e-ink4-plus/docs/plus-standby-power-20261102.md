# Plus 待机耗电排查与候选（2026-11-02）

基线：正式 `plus-v1.2.13` / `9ae7ed7b946e16056ece99ec788bac8dbb85fde1`。
分支：`codex/plus-standby-power-20261102`。
当前测试版：`1.2.13-power1`；尚未发布。固件构建、应用刷写/启动、板载电量计和联网进入待机短测已验证。早期 `1.2.13-standby1` 的安装与观测作为历史记录保留。

用户转述厂商测量：半小时待机与正常使用功耗接近；其他用户的进入方式和外设状态不同。
本轮已有板载电量计记录；尚无厂商外部仪表的原始曲线，不将单台设备的一条路径认定为全部设备的根因。
10 月 23 日单机约 6 mA 的历史记录不代表当前版本、其他机器或所有待机状态。

## 当前验证结果

- 正式行为对照包 `1.2.13-baseline1` 在关闭无线的普通壁纸场景中也能睡眠，末端滤波电流约 13 mA；没有据此声称优化收益。
- 对照包在要求联网进入待机的一轮中没有进入睡眠，末端滤波约 82 mA，清醒后约 65 mA；该轮联网成功条件未独立记录。
- 修复版短测由用户和串口共同确认进入待机前联网成功；进入待机时断开无线并释放驱动，成功睡眠 316700786 us，末端滤波约 13 mA，清醒后约 64 mA。
- 短测用于核对明显异常，不作为精确平均电流或续航提升。SD 保留降级、无图片、无效输入以主机回归为主，其他入口/外设组合仍需实机覆盖。
- 镜像身份及长测记录见 [测量构建记录](plus-power-capture-build-20261102.json)，本次联网短测原始数值及边界见 [对照记录](plus-wifi-standby-retest-20261102.json)。

## 已复现的路径

以下位置为修改前正式版的恢复源码：

- `upstream/lib/hal/HalStorage.cpp:529–550`：一次 SD 唤醒挂载需要重试后，
  `standbyParkDisabled` 保留该卡挂载；后续 `StandbySession::suspend()` 返回 false。
  `components/rickyos_application/port/PlusBoard.cpp:542` 将其变成整次睡眠失败，CPU 继续运行。
  这是原挂载兼容处理的副作用。启动时 `0x109` 日志本身不能证明这一标志已经置位。
- `upstream/src/activities/apps/standby/StandbyActivity.cpp:175–180,472–480`：
  时钟有效、自动校时关闭或静态画面跳过 NTP 时，没有关闭继承的 Wi-Fi；
  STA/AP 即使没有活动连接，非 NULL mode 仍永久挡住浅睡。
- 同文件 `461–465`：没有图片的提示页一直禁止睡眠，哪怕闲置数小时。
- 同文件 `573–592`：对纯待机画面没有作用的点击、Confirm、滑动和松键仍重置计时；
  点击还会提前返回，延后校时处理和自动关机检查。

SD 保留挂载、遗留 Wi-Fi、无效输入的用例在旧实现上失败，修改后通过。
无图片也可睡眠已加入第一帧和计时回归；不能用这些主机测试代替真实电流测量。

## 修改与边界

- 保留 SD 的慢挂载/失败恢复策略。无法安全卸载时，在同一个 HAL 存储租约下，
  清理本次 RTC 唤醒配置后使用已有浅睡路径，单片不超过 60 秒；不改变 SD 引脚、
  电轨、打开的文件或文件位置。清理失败仍停止，不在错误的 RTC 状态上继续睡。
- Plus 不需要校时时关闭继承的 Wi-Fi；需要校时则沿用原有有界同步和收尾流程。
  不自动恢复一条已经为待机关闭的无线连接。
- Plus 无图片提示在完成第一帧和原有 35 秒闲置后也可睡；实体电源键唤醒/退出。
  闲置前仍可选图或打开下载页，其他机型保留原行为。
- 纯待机画面忽略的输入不再重置闲置时间或跳过自动关机；选图动作和实体 Back 保留。
- 保留蓝牙退出、录音/音频停止、前光关闭、真实按压、I2C/显示故障和 USB 保护。
  不更改充电参数、屏幕波形、电量模型、brownout 阈值、bootloader、分区或 OTA 清单。

SD 保留模式仍比完整外设停放多保留硬件状态，不能承诺达到完整静态待机的电流。
USB 数据连接/外部供电仍会挡住当前睡眠路径；带 USB 的数值必须单独标注，
不能与拔线电池侧的待机数值混为一组，也不能用诊断宏绕过保护后代表普通固件。

## 观测方式与资源

`PlusSleep` 的 `enabled/ready/fault/audio/touch_pending` 和 `sleep blocked` 说明板级条件；
`STANDBY gate` 记录 Wi-Fi、蓝牙、USB、校时、按键等活动条件。
这些是现有主循环中的节流日志，不添加计时器或唤醒源。

每次待机只记录固定大小的计数：成功/拒绝次数、SD 保留次数，以及成功
`esp_light_sleep_start()` 调用累计经过的微秒数。该时段包含 SDK 入睡/恢复开销，
用于识别“从未睡/立即醒/大部分时间睡眠”，不等同于电池电流测量。
退出时冻结 `summary duration_ms/sleep_ms/entered/rejected/sd_retained`，经 HAL 写入
已有日志；随后通过已有 30 秒清醒电池轮询重放同一冻结摘要，方便接回 USB 后收集。
再次待机清除上一轮可用标记。睡眠期间主循环阻塞，不为重放而唤醒或写 SD。

新增存储为板级计数和 HAL 冻结快照等不足 100 字节静态状态，以及小型栈快照/时间戳；
没有新增堆对象、任务、帧缓冲或日志文件。SD 降级最多多进入一层现有函数调用，
空 storage 参数保证不会再次递归；不复制硬件驱动或存储租约。

## 构建与软件验证

- 继续使用补丁 IDF `9a97f6c54ec638111ce55cd36581b3c192f15207`、NimBLE-Arduino 2.3.8。
- 保留正式显示参数及默认睡眠/电轨配置；未开启 `STANDBY_DIAG`、`POWER_LOG`、
  `FIELD_DIAG`、`BLE_DIAG`、SD 故障注入或 CPU 频率对照开关。
- 14 个待机/前光/输入/蓝牙/电池/恢复相关脚本完成；旧 P14 脚本的一个可选外部基线审计跳过。
  新增 Wi-Fi 的 96 个时钟/自动同步/无线模式与目标组合；SD 保留覆盖请求 1 秒至 24 小时、
  唤醒键、清理错误、SD 引脚及文件保持。冻结摘要重放不改变电池轮询次数。
- 固件构建、镜像头/机型/版本/校验和/验证摘要及 OTA 预留检查通过。
- 独立目录从固定 Git 对象和记录补丁重建 2,842 个源码文件、327 项适配成功；
  新恢复源码上的 Wi-Fi 和无效输入测试通过。

精确镜像身份见 `plus-standby-power-build-20261102.json`。应用安装、启动书页及一次实体键待机/
唤醒已验证；蓝牙/SD 连续性矩阵和半小时实际功耗仍待验收，不能标为已解决续航问题。

## 实机复测

1. 先备份整片 Flash、记录现有应用版本/哈希，再只写应用槽；校验镜像和槽外数据。
2. 检查正常启动、翻页和短按进入/退出待机。先做一次 1–2 分钟拔线试验，接回线后
   收集冻结 summary，确认 entered 增长且 sleep_ms 占主要时段；不反复盲刷或复位。
3. 功耗仪在电池侧测量，保持同一设备/电压/画面。分别记录正常静置阅读与待机半小时的
   平均电流和累计电荷，同时保存唤醒尖峰；不要仅凭电量百分比判断结果。
4. 覆盖实体键/控制中心/自动待机；普通壁纸、封面、无图片、保留书页和日历；
   尤其检查用过 Wi-Fi、用过蓝牙、前光原先开启、SD 慢挂载后保留挂载的状态。
   日历/时间叠层记录正常更新造成的周期唤醒。SD 错误不在用户卡上主动制造。
5. 唤醒后验证文件位置和继续翻页、书库、SD 字体、触摸、前光恢复及蓝牙重新连接。
   任一白屏、挂载失败、按键失效或 fault 锁存都保留原日志，再定位，不删除保护绕过。

若 `entered=0`，先按 gate 和板级阻挡原因定位；若成功调用次数很多但 sleep_ms 很小，
检查错误码/唤醒源；若大部分时段已在睡眠但电流仍高，再测电轨和外设。
后者需要厂商电流与电轨证据，不能仅从函数调用次数推断硬件已省电。

## 本轮安装记录

已核对同一台 ESP32-S31 Plus 当前为正式 1.2.13（47988765…），保存完整 16 MiB Flash 备份，
只替换 app0（0x10000）。新应用写入/摘要核验通过，应用槽以外 Flash 逐字节一致。
串口版本/ELF 匹配候选，设备按保存状态恢复 EpubReader 并完成书页刷新，显示 result=0x0；
35 秒启动观察未见 panic。首次自动检查只接受 InxRecent，随后用实际读书恢复日志补充验证，
没有为此重复刷机或更改启动偏好。拔线待机和电流结果单独记录，不由启动检查推断。

首次拔线试验的采集无效：重新打开 USB 串口出现 `CHIP_USB_UART_RESET`，清掉 RAM 中的
睡眠计数，不能据此判断固件待机成功或失败。采集器原先在 open 前将 DTR/RTS 均置 false；
现按安装的 `esp_idf_monitor/base/serial_reader.py::open_serial(reset=False)` 先置 true，
打开后先 RTS 再 DTR 置 false。修正后重开观测到运行时间 301896/311923/321991 ms 连续，
没有 ROM 复位横幅；未重刷固件。原始无效记录保留为 `standby-observation.log`，
新记录为 `standby-observation-v2.log`。重新拔线试验结果见下。

## 早期 standby1 待机结果：真实入睡已确认，当时未测电流

用户再次短按进入待机、拔线放置、短按唤醒并接回，确认已回到首页/书页且操作正常。
USB 再次枚举为同一 MAC，但睡眠恢复后串口没有输出；包括单独置 DTR 的握手也未恢复输出。
为保留本轮计数，使用安装的 OpenOCD 读取运行中 CPU 的系统总线 RAM，没有执行 halt、
reset、写 Flash 或应用内函数调用。调试模块在 shutdown 时退出。

与本镜像 ELF/map 对应：`SleepAccounting` 在 `0x2f02e170`、冻结快照在 `0x2f02e188`，
可用标记 `0x2f038b59=1`。原始日志 `jtag-sleep-accounting-v2.log`：

```text
CPU_STATE running
ACCOUNTING 0x17aefc11 0x0 0x1 0x0 0x0 0x95f1f 0x17aefc11 0x0 0x69b28 0x1 0x0 0x0
SUMMARY_AVAILABLE 0x1
USB_CONNECTED 0x1
```

- 总待机 `432936 ms`（7 分 12.936 秒）。
- 成功睡眠调用累计 `397343761 us`（6 分 37.344 秒），占总时段约 91.78%。
- 其余 `35592 ms` 与原有 35 秒闲置等待及进入/恢复开销吻合。
- 成功 `1` 次，拒绝 `0` 次，SD 保留降级 `0` 次。
- 起始 uptime `614175 ms` 与进入待机原始日志吻合，计数未被接线复位清掉。

这证明本轮确实经历一次长浅睡，且用户确认能够正常唤醒操作；不能证明多少 mA、
比正式版省多少电，或所有进入方式都已实机通过。SD 保留降级等路径仍只有软件回归证据。
睡眠后的 USB 串口日志恢复另记为诊断通道限制，不据此判定 CPU 未睡或唤醒失败。
下一步以同设备、同电压的电池侧半小时电流/电荷对照验收，覆盖第 4 步状态矩阵。

## Connected Wi-Fi entry physical short retest

User confirmed successful association before standby. No-reset UART logs confirm fresh Wi-Fi association after the preceding software restart (WeRead job/package completion; the source explicitly calls silentRestartToReader at WeReadActivity.cpp:1419). The same new-boot log then records Standby entry at 26740 ms, frontlight suspended at 26741 ms, STA disconnected at 26762 ms, followed by hardware TX shutdown and RX descriptor deinitialization. Thus the test did not merely inherit Wi-Fi-off state from a reboot. The entry cleanup uses StandbyActivity.cpp:180-183.

Installed power1 image hash 6ff24c56efdff8369a76bf1e3977820af96ca86d58d16002d73ab3325c61f2e2. App-only write, protected-region equality, startup ELF/version, restored home/reader frame and initial RAM read passed. Battery-only counter window: 340447 ms, 2 mAh, valid endpoints, no faults. Last instantaneous/filtered current -15/-13 mA. Awake over 90084 ms: -64/-64 mA. Frozen sleep summary: total 352324 ms, successful sleep 316700786 us (5 min 16.701 s), entered=1, rejected=0, SD-retained=0; non-sleep overhead about 35.62 s. Both reads have identical frozen standby/summary.

Optimization-before baseline under the requested Wi-Fi-entry protocol had entered=0 and last instantaneous/filter -83/-82 mA, versus awake -65/-65 mA. Baseline successful association was not independently recorded. This supports restoration of sustained sleep in the verified connected-entry case, not a precise matched average-current saving or universal resolution of standby complaints. Short-window quantization, different durations/voltages, external-meter acceptance and other peripheral/entry cases remain explicit limits. No release, commit or push was performed for this retest.

Results: /Volumes/Seagate/RickyOS-build-output/plus-power-capture-20261102/wifi-short-retest-comparison.json. Private raw UART logs remain only in device backup; the packaged entry evidence omits SSID/address details.
