# Plus 57mA 基线后的首个降耗候选：保持清醒、空闲 40MHz

2026-10-22，电量分支 `codex/plus-battery-stability`，工作树
`/Users/rick/rickyos/wt-plus-battery`。本次只改代码、运行主机验证及离线编译，
没有打开串口、复位、刷机或改充电参数；没有提交、推送或发布。

## 从现状到本次改动

用户确认功耗 CSV 是刚才同一台 Plus 运行我们自己的 RickyOS 1.2.5 的记录。
阅读和两段待机的电量计电流均约 57mA，见
[功耗 CSV 证据](plus-power-csv-evidence-20261022.md)。
这说明当前待机没有显示出明显低于阅读的电流，尚不能把 57mA 分配到具体芯片或电源域。
厂商口述的 3mA 没有同机实测对照，不能据此声称本次候选的目标已经达成。

源码依据（相对 `ports/metalio-e-ink4-plus/firmware/rickyos-plus-1.2/`）：

- `components/rickyos_application/port/PlusBoard.cpp:23`：普通构建 Plus 轻睡眠默认关闭，
  此前是为阻止白屏候选自动进入普通版本。页面名为 Standby 不代表主控实际睡眠。
- `upstream/lib/hal/HalPowerManager.h:45`：原 PSRAM 目标空闲频率为 80MHz；
  `upstream/lib/hal/HalPowerManager.cpp:228` 通过互斥锁串行切频，保留 Wi-Fi 和正常速度锁保护。
- `upstream/src/main.cpp:1421`：空闲超过 3 秒请求降频；输入处理恢复正常速度。
- `upstream/src/activities/ActivityManager.cpp:214`：渲染持有正常速度锁。
- 实际 S31 SDK `components/esp_hw_support/port/esp32s31/rtc_clk.c:279-306`：
  本构建 40MHz 晶振条件下，40MHz 选择 XTAL，80MHz 选择 320MHz CPLL 的四分频。
  本构建配置 `CONFIG_XTAL_FREQ=40`、正常 CPU 320MHz。
- `components/metalio_board/config.h:91` 把 P14 写为 BT/PA 电源，但显示板头文件及 OEM 恢复流程
  又将其描述为 VOUT、关联背光。因此不能仅凭名称把它当成独立 BH1098 电源直接关闭。

因此先建立一个不经过睡眠/PSRAM 恢复的空闲频率对照，实测后才决定是否保留。
此改动改变空闲 CPU 时钟来源和频率，有降低 CPU 动态功耗的机制；
**它不会自动关闭其他外围电源，也不能保证整机从 57mA 降到任何特定电流。**

## 实现

1. 新增显式构建参数 `RICKYOS_PLUS_IDLE_FREQ_MHZ=40|80`。
   未指定时保留原有普通 Plus 80MHz 策略；其他设备沿用原频率设置。
   40MHz 仅允许 S31。非法值或与 Plus 轻睡眠试验同时开启，构建会拒绝。
2. 40MHz 候选继续通过 HAL 切频，不绕过既有互斥锁、Wi-Fi 及正常速度锁。
   音频播放或录音忙时，通过板级检查保持正常速度。
   重复空闲请求不重复切频，切频失败不修改 HAL 的成功状态。
3. 显式频率对照构建会记录每次实际切换的目标值、读取到的频率及空闲标记，
   不能只从构建参数认定设备实际运行在 40MHz。
4. 另补首错保护：只要轻睡眠试验启用后发现已有显示首错，就锁存禁止继续睡眠，
   直到重启。对应最新真机 row426 首错后仍继续睡眠/刷新数轮的问题。
   它可避免继续覆盖现场和重复失败，**不修复第一次供行断档，也不保证白屏自动恢复**。

新增静态频率常量、代码分支和板级音频忙查询，没有新任务、堆对象、缓冲或定时唤醒。
显示波形、LCD/RMT 时序、预填行数、SDK 保持设置、BQ 与充电配置均未修改。

## 验证记录

- `test_plus_idle_power.py` 执行生产 HAL 类声明、真实切频函数及 Lock 构造/析构，
  ASan/UBSan 下检查普通 Plus、显式 40/80、C3 和其他 PSRAM 目标。
  覆盖 Wi-Fi、正常速度锁、40MHz 音频忙、切频失败、重复请求和两线程竞争；
  时钟改变的模拟回调不允许并发执行。
- 使用实际 CMake 条件测试合法/非法参数及睡眠混用拒绝。
- 编译执行实际 S31 SDK 的频率配置选择函数，确认 40MHz 为 XTAL、80MHz 为 CPLL/4，
  320MHz 为 CPLL；测试不是实物时钟或功耗测量。
- `test_plus_standby_sleep.py` 新增真实第一次 TIMER 返回后发生 row426 首错的序列：
  下次不得再睡眠或拆显示资源；首错保护保持到重启。原输入竞态和恢复回归保留。
- 首轮完整 Plus 主机套件通过（两个 Read Pico 专用测试按平台跳过，SDK 测试确实运行）。
  随后补音频忙保护，再执行受影响的频率/睡眠定向回归。
- 两个 HAL 文件的固定源码补丁及 `port-changes.json` 已同步；
  固定源码清单未变。补丁以验证过 SHA256 的固定原文件重放并逐字节核对。

记录目录：`ports/metalio-e-ink4-plus/.local/offline-idle-power-20261022/`。
最终构建/哈希以该目录 `verification.json` 为准；早期产物会被最终产物替代。
此分支仍是既有 1.2.4 实验应用基线，不代表用户的 1.2.5 已被修改或安装；
普通分支频率策略仍为 80MHz，物理降耗结果待验收。

定向复现：

```sh
RICKYOS_IDF_PATH=/Users/rick/rickyos/wt-plus/ports/metalio-e-ink4-plus/.local/esp-idf-v6.1-r \
  python3 ports/metalio-e-ink4-plus/tests/test_plus_idle_power.py -v
python3 ports/metalio-e-ink4-plus/tests/test_plus_standby_sleep.py -v
python3 ports/metalio-e-ink4-plus/scripts/check_rickyos_plus_sources.py
```

## 下一次同机验收应解决的问题

先做相同源码、相同诊断开销、同样外围与页面条件的 80/40 对照，保持轻睡眠关闭。
核对设备当前镜像、备份与回滚目标后，验证真实空闲 40MHz、绘制/交互及音频时回到正常频率，
再在电池供电、前光和 Wi-Fi 关闭条件下比较稳定窗口的累计量与电流。
采集必须能覆盖拔线期间，不把插线充电读数当成电池放电功耗。
现有 1.2.5 CSV 只作为历史基线，不能与不同构建/诊断开销的 40MHz 结果直接作严格归因。

验收需同时观察按键、触摸、翻页、分钟刷新、SD 访问、前光、音频与 USB 重接；
SDK 接受 40MHz 不等于这些外围已通过物理验证。异常时先保留记录并恢复本轮原镜像。

若这一项收益有限，剩余部分仍要靠受控的时钟/外围对照定位。
要继续向低毫安待机推进，还必须解决第一次轻睡眠唤醒后的显示供行断档，
补生产者当时的进度/等待位置与实际时钟证据，再评估 CPU/PSRAM 保持的单变量对照。
当前不以降频候选代替这项根因调查，也不宣布整机功耗或白屏问题已解决。
