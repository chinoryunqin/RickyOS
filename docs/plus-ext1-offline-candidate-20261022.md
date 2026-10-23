# Plus EXT1 侧键唤醒：离线候选

工作树 `/Users/rick/rickyos/wt-plus-battery`，分支 `codex/plus-battery-stability`。
本轮仅修改自有组件、主机测试及记录；未访问设备、刷机或修改充电参数。
普通电量构建仍关闭轻睡眠，诊断开关也不会单独启用轻睡眠。

后续显示供行错误恢复修正与更新构建记录见
[供行错误恢复](plus-render-recovery-offline-20261022.md)；本文保留 EXT1 阶段的独立记录。

## 依据与边界

厂商只读目录 `/Users/rick/Documents/ChatGPT/rickyos/vendor/metalio-plus-2.0.76` 中，
`main/boards/common/power_policy/power_hw.cc:90-103` 使用 GPIO2 的 EXT1 ANY_LOW 唤醒。
此前候选使用普通 GPIO 低电平唤醒，与厂商不同。
此前确实取得过计时器唤醒日志，因此不能仅凭差异认定普通 GPIO 是白屏或 USB 失联的根因。
本次调整用于让试验候选对齐厂商已使用的唤醒路径，硬件结果未验证。

本地 IDF 为 `/Users/rick/rickyos/wt-plus/ports/metalio-e-ink4-plus/.local/esp-idf-v6.1-r`：

- `components/esp_hw_support/sleep_modes.c:2117-2190`：追加/移除指定 EXT1 IO 只改睡眠配置；
  `2218-2266` 的睡前准备另外切 RTC mux、启用 RTC 输入，并可能启用 pad hold。
- `components/soc/esp32s31/include/soc/soc_caps.h:262-266,568`：S31 有独立 RTC IO mux、hold 及 RTC 外设掉电支持。
- `components/esp_driver_gpio/src/rtc_io.c:47-85`：显式 init 取得 RTC 时钟/输入脚位所有权；
  deinit 返回数字控制，并在持有所有权时清理 RTC 方向、上下拉、时钟使用记录。
- `components/esp_hw_support/port/esp32s31/io_mux.c:27-73`：RTC 时钟按 IO 跟踪，
  deinit 清除本 IO 的使用记录；其他 IO 仍持有时不关闭共享时钟。
- `components/esp_hal_gpio/esp32s31/include/hal/rtc_io_ll.h:77-98,197-231`：
  RTC mux、上下拉使用独立寄存器，不能把数字输入配置直接当成 RTC 配置。

因此本次没有只替换唤醒 API：同时补齐脚位取得、输入保护与返回流程。

## 候选行为

`components/rickyos_application/port/PlusBoard.cpp`：

1. 原有 USB、充电状态只读检查、触点检查、输入空闲及显示锁保护保留。
2. GPIO2 必须为输入并支持 EXT1。屏蔽 CPU ISR，显式初始化 RTC 输入，镜像既有数字上拉/下拉。
   不修改数字 sleep-select，不将普通下降沿 ISR 改成低电平 ISR。
3. 为 GPIO2 追加 EXT1 ANY_LOW 和原有 1–60 秒计时器。再次检查 USB、扩展器及输入；
   这次用 RTC 输入读共享中断电平，避免在 RTC mux 下误读数字 GPIO。
4. 睡眠返回后先读取 P17，然后仅移除 GPIO2 EXT1 配置、清除计时器、释放 RTC hold、
   反初始化 RTC 脚位、恢复普通 ISR。设置失败、睡前竞态及睡眠被拒绝也走清理。
   不使用 `ESP_SLEEP_WAKEUP_ALL`，不清空其他 EXT1 IO 配置。
5. 独立 USB 恢复、checked 显示重建、有效侧键分类及清理故障停用后续睡眠沿用上一轮修正。
   极短且在首次端口读取前已释放的共享中断仍不冒充侧键。

新增仅有栈变量和返回值，无新任务、堆对象或缓冲。
没有复制厂商的 SD 卸载、扩展器方向切换或外设电源切断，充电配置与电量模型未改。

## 离线验证

`test_plus_standby_sleep.py` 执行真实板级后端，硬件由替身提供：

- 普通/诊断与默认关闭/明确启用四种组合；默认关闭时不碰 RTC、USB、LCD 或状态采样。
- RTC 控制时只允许 RTC 电平读取；进入睡眠必须保持普通 ISR 下降沿类型、RTC 输入和原上下拉。
- 睡眠准备会模拟 RTC hold，连睡眠拒绝也设置 hold；返回必须先移除唤醒、释放 hold、返回数字脚位，
  然后才启用普通 ISR、恢复 USB 和显示。
- 原有 5 类睡前竞态、I2C 失败、显示重建失败、慢显示恢复短按、长按所有权与时间回绕回归保留。
- 新增 RTC 初始化/方向/上下拉错误、无效 EXT1 引脚、清理错误停用后续睡眠。
- 上拉、下拉、数字 sleep-select 的 8 种组合各循环 125 次；每种启用模式共 1,000 次。
  每次 RTC 资源回到未使用，数字配置不变，另一个 EXT1 IO 的配置仍保留。

主机测试启用 ASan/UBSan。它不能证明真实 GPIO 电气保持、显示图像、USB 重新枚举或 3mA。
构建与完整套件结果保存在本地忽略目录 `.local/offline-ext1-20261022/verification.json`。

本候选没有安装，设备继续保持此前恢复且用户确认主页正常的原版 1.2.5。
白屏和 USB 失联的准确触发点仍未证实；这轮不安排实机测试。

复现：

```sh
python3 ports/metalio-e-ink4-plus/tests/test_plus_standby_sleep.py -v
bash ports/metalio-e-ink4-plus/tests/run_plus_port_tests.sh
```

## 构建结果

完整 Plus 主机回归退出码为 0，试验开启及普通电量两种 S31 构建均成功。
固定源码检查为 2844 个文件、141 项适配，触摸后端、时钟对象及网络内存配置检查通过。
普通构建编译参数已确认不含睡眠试验和诊断定义。两份产物均仅离线保存。

| 配置 | BIN 字节 | BIN SHA256 | ELF SHA256 |
|---|---:|---|---|
| 试验开启、诊断开启 | 4914848 | `61a2821497816d269a5f4f40cab797e2205beb5317e570e268923763254566e8` | `d888ee76b0baa7ab3d292bfc759b25ddd0e63de3d2e3619d68a54ef51d6bf1af` |
| 普通电量、睡眠关闭 | 4907072 | `c8000998c7517a4aa93d967cc77008584889fe4b7055a9cd19a7cf49332b3c55` | `0c81e68e15881bf03850856735ab1540a4b686804333b28e5d7aea63acc46b32` |
