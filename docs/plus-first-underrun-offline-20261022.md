# Plus 睡醒后首次断供：显示时钟与内存恢复调查

> 本文是安装前的离线阶段快照。后续真机仍白屏，抓到 row426/producer0 首错，
> 并已恢复测试前 1.2.6-tab3；见 [最新真机记录](plus-first-underrun-device-trial-20261022.md)
> 和 [完整交接](plus-battery-sleep-review-handoff-20261022.md)。

2026-10-22，电量分支 `codex/plus-battery-stability`，工作树
`/Users/rick/rickyos/wt-plus-battery`，基线 `4455bf01`。
本轮未访问设备、打开串口、复位或刷机，充电参数、BQ 配置、容量模型均未改。
没有提交、推送或发布。轻睡眠继续默认关闭，设备维持此前恢复的原版 1.2.5。

## 结论与证据边界

本轮确认了可复现的**供行停顿 → 首次未就绪行**条件，并补齐首次错误证据。
没有找到足以认定“SDK 少等一次 PSRAM 恢复”或“DMA 描述符少刷一次内部缓存”的依据，
也没有把模拟停顿当成那次真机 PSRAM 变慢的证据。
此前第三轮白屏、第四轮 USB 失联的最初物理触发原因仍未确定。

第三轮原日志在恢复 USB 后显示两个 `0x400`，但首次发生时没有细分原因。
`EPD_DRAW_EMPTY_LINE_QUEUE` 同时表示行未就绪、队列空、帧提前结束和等待超时。
日志无法倒推出首次异常的行号或是哪条路径；第四轮也没有失联时的完整回放。

## PSRAM 软件恢复顺序

本地 SDK 为 `/Users/rick/rickyos/wt-plus/ports/metalio-e-ink4-plus/.local/esp-idf-v6.1-r`。

- `components/esp_hw_support/port/esp32s31/pmu_sleep.c:357-366,395-414`：
  睡前等待 PSRAM 事务完成、进入半睡并 hold 脚位；醒后恢复 MPLL、unhold、发出退出半睡操作。
- `components/esp_psram/device/esp_psram_impl_ap_oct.c:554-577`：记录退出时 RTC tick、唤醒 CE，
  resume 函数等待配置的 RTC 截止点，再恢复 MR4 刷新率寄存器。
- 当前配置 `PM_SLP_IRAM_OPT=y`、`SPIRAM_XIP_FROM_PSRAM=n`，因此
  `components/esp_hw_support/sleep_modes.c:1739-1757` 在恢复另一颗核心之前等待 PSRAM resume。
- 实际 `sleep_modes.c.obj` 的 RISC-V 反汇编核对到同样的调用顺序：
  `esp_psram_impl_resume_from_halfsleep_mode` → `esp_ipc_isr_stall_resume` → `sleep_smp_cpu_wakeup_prepare`。
  不是只看未预处理源码。链接 map 确认 resume 来自 `esp_psram_impl_ap_oct.c.obj`，位于内部 RAM。

新增 `test_plus_psram_resume.py` 执行实际 SDK resume 函数与实际 RTC 换算函数，
以替身提供 RTC 和寄存器接口，覆盖 4 种 RTC 周期、6 种已消耗等待量、3 种 MR4 值，共 72 组。
均在达到换算截止点后才写回 MR4；若醒后已有工作消耗完等待预算，不再重复等完整预算。
ASan/UBSan 通过。这里证明的是软件顺序及 tick 运算，
不是 CE 波形、实物 RAM 保持或精确 150µs 的量测；RTC 换算存在整数 tick 量化。
本轮未修改 SDK、半睡等待参数或 PSRAM 频率。

## 显示时钟与内存位置

本地默认时钟定义 `components/soc/esp32s31/include/soc/clk_tree_defs.h:179-194,284`：
LCD 核心为 PLL120M（核心分频 2），LCD 像素时钟父源为 PLL160M，RMT 为 REF80M。
自有驱动重建时重新选择这些来源，CKV 从 REF80M 分频 8 得到名义 10MHz。

扩展原时钟生命周期测试：每次完整反初始化后，使模型中的旧来源选择和分频失效，
检查真实初始化函数重新编程来源和 RMT 分频。两种 LL 核心接口、有/无共享引用者，
共 4,000 次循环通过，引用计数和中断资源仍归还。
其他总线使用者持有公共核心配置的情形按原有所有权建模，没有擅自重置共享硬件。
该测试验证重建代码的编程动作，不能测量物理 PLL 是否锁定或真实 PCLK/CKV 频率。

新增 `test_plus_lcd_first_frame.py` 将真实配置赋值、PCLK 设置、CKV 波形构建、
首帧启动、bounce 填充和 EOF 完成函数放在同一个测试中，沿用真实 LCD 状态结构。
每轮保留上一帧的结束/计数状态，清除模型中的旧时钟配置，前移模拟时钟 60 秒，
交替请求名义 20/16/10MHz，执行 1,000 轮。结果：

- PCLK 分频和数字门控重新写入，CKV 周期/高电平与默认行周期重新建立；
- 启动顺序为 DMA reset、LCD stop、FIFO reset、两块 bounce 各填 8 行、
  DMA start、CKV 前缀、LCD start；前缀及可选 deadline 等待不改变既定行周期；
- 重置旧 EOF/供行计数后，688 行按顺序输出（含 4 行 padding），完成仅通知一次；
  正常起帧未继承上一轮的结束状态或立即误报完成。

它不执行真实睡眠/RTOS、checked 分配函数或电气传输：分配/回滚、时钟父源生命周期和
并发供行分别由其他测试覆盖。假 HAL 只返回与整数分频相符的名义时钟值，
不能用这些请求频率或主机计时推断设备性能。

`display/render.c:294-315,337-350` 把 LUT、ready map、行队列和供行 scratch 放在内部内存；
PSRAM 图像源由任务读取，ISR 只消费提交后的内部行队列。
`lcd_driver.c:814-843` 的 bounce buffer 与描述符也申请 `INTERNAL|DMA`。

S31 `components/heap/port/esp32s31/memory_layout.c:43-76` 与
`components/soc/esp32s31/include/soc/soc.h:152-170` 对应内部 RAM `0x2f000000…0x2f080000`。
`components/hal/esp32s31/include/hal/cache_ll.h:1028-1040` 只将 Flash/PSRAM 地址范围视为 cacheable。
S31 也没有 P4 的 `SOC_NON_CACHEABLE_OFFSET_SRAM` 布局。
因此“照搬 P4，为内部 DMA 描述符补缓存别名/刷新”不是目前有证据支持的 S31 修正。
驱动中把 S31/P4 一起描述为内部缓存的旧注释不能替代目标芯片的地址判定；本轮没有据此改内存布局。

## 新增首次错误记录

源码为自有组件 `display/output_lcd/lcd_fault.h`、`render_lcd.c`、`phase_lookahead.inc`。

- 一个静态 32 位字，原子 CAS 仅记录本次启动的第一个失败；后续 ISR/任务错误不覆盖它。
- 编码为 `row[31:16] | producer[15:8] | reason[7:0]`，未知行/线程为 `65535/255`。
- 区分行未就绪、队列空、帧提前结束、整帧超时、供行任务超时、队列等待超时、预填等待超时。
  未经过这些入口的错误可保留 0，不能把 0 解释为整机显示健康。
- 不在 ISR 中打印，不新增任务、堆对象或持久化写入，不改变预填行数、扫描时序、波形和频率。
  错误时普通控制端可打印该字；睡眠诊断会在板级进入检查中将其放入已有 RAM 日志环。
  最终日志放在已有的“实际进入睡眠”消息中，避免每次拒绝重试都刷写 RAM 日志环。
- 记录不随正常重绘或 LCD 总线释放/重建而清空，只在重新启动后清空。
  如果 CPU 根本不能返回/执行，或供电丢失，仍无法保证得到该 RAM 记录。
- 实际链接记录位于内部 `.sbss.s_first_fault`，大小 `0x4`；
  编译对象未生成 out-of-line `__atomic_*` 调用。未新增互斥锁或 ISR 日志调用。

## 首次断供的离线复现

扩展真实控制器/供行函数/原子行队列的并发主机测试，模拟线程只控制故障发生顺序，
不把主机毫秒数当成板上 DMA 时序。

| 注入条件 | 首错字 | 含义 |
|---|---|---|
| 预填后，生产者 1 在提交第 193 行前停住；确保此前各行已经提交 | `0x00c10101` | 行未就绪，row=193，producer=1 |
| 扫描报告完成，但没有消费任何行 | `0x0000ff03` | 帧提前结束，row=0，producer 未知 |
| 帧完成通知缺失，等待返回超时 | `0xffffff04` | 整帧超时，行和 producer 未知 |

三种故障在普通 GC 调度、lookahead GL16_FAST 调度及关闭 lookahead 的构建中得到不同且准确的记录。
35 次程序运行（含原有回归与 3 次预期的生产卡死保护）通过 ASan/UBSan。
普通错误清理后的三相正常重绘继续成功，首次错误字保持不变。
慢图像读取、任务被长时间抢占等都可能形成第一种软件条件，
**测试没有证明具体是 PSRAM 速度下降、CPU 恢复慢还是其他硬件条件造成了真机停顿。**
第二、三种记录也只能说明扫描/帧完成异常，不能单独归因为物理显示时钟。

## 验证与复现

完整 Plus 主机套件通过，含以上 SDK 恢复测试（本次显式提供了实际 SDK 路径，未跳过）。
试验与普通电量两种 S31 构建和固定源码检查结果、源文件哈希及反汇编证据保存在
`.local/offline-first-underrun-20261022/verification.json`。产物只离线保存，不安装。

```sh
python3 ports/metalio-e-ink4-plus/tests/test_plus_phase_lookahead.py
python3 ports/metalio-e-ink4-plus/tests/test_plus_lcd_clock_lifecycle.py -v
python3 ports/metalio-e-ink4-plus/tests/test_plus_lcd_first_frame.py
RICKYOS_IDF_PATH=/Users/rick/rickyos/wt-plus/ports/metalio-e-ink4-plus/.local/esp-idf-v6.1-r \
  python3 ports/metalio-e-ink4-plus/tests/test_plus_psram_resume.py -v
```

当前没有足够设备证据可以在离线环境确认首次物理触发原因；
本轮把可复现的软件条件、恢复顺序及下一次所需的首错证据补齐，轻睡眠仍默认关闭。

## 最终构建记录

完整 Plus 回归退出码为 0；原套件中 2 个 Read Pico 专用测试按平台跳过，
本轮 PSRAM 恢复测试确实运行并通过，没有跳过。普通电量与试验开启两种 S31 构建均成功，
固定源码检查（2844 文件、141 项适配）、触摸后端、时钟对象与网络内存检查通过。
普通版本编译参数不含睡眠试验和诊断定义。所有镜像仅离线保存。

| 配置 | BIN 字节 | BIN SHA256 | ELF SHA256 |
|---|---:|---|---|
| 普通电量、睡眠关闭 | 4907792 | `7d1b6a28df6ca4fffa1d48e586f9a9286aaa3b692f18ce733302c1495ba777e6` | `56cabe4d27645313deafff8989a28ffcaed7747526ea21dc8e5ef1401552e834` |
| 试验开启、诊断开启 | 4915584 | `5534044199c6ffe4ad7647aca0a274f8eb265bf83ab9478250d9f3c651a443dd` | `a8cd8990bef7f98502a836ec97db600c9a63e8e002d61aa0daa29818b20440d3` |
