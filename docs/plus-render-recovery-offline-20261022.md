# Plus 供行超时后的刷新恢复：离线修正

2026-10-22，工作树 `/Users/rick/rickyos/wt-plus-battery`，
分支 `codex/plus-battery-stability`，基线 `4455bf01`。
本轮只改自有显示组件、主机测试及记录。没有访问设备、复位、刷机或修改充电参数，
没有提交、推送或发布。轻睡眠仍默认关闭，设备沿用此前恢复的原版 1.2.5。

后续对显示时钟、PSRAM 恢复顺序和首次断供条件的调查见
[首次断供离线调查](plus-first-underrun-offline-20261022.md)。

## 确认的问题

第三轮真机出现 `line buffer underrun occurred!` 和刷新结果 `0x400`。
`display/render.c:205-206` 对 `EPD_DRAW_EMPTY_LINE_QUEUE` 输出此日志；
该错误位也用于帧超时、供行任务超时和队列未就绪，不能只凭错误码定位起因。
第四轮失联时没有取得完整日志，本轮仍不能将两轮自动归为同一个原因。

检查本地 `display/output_lcd/render_lcd.c` 的普通刷新路径发现：

- 供行任务完成通知等待超时后，仅记错，之后仍直接重置行队列并返回。
  晚到的完成通知没有被消费，下一次刷新可能把它当成新一轮完成。
  此时前一轮任务也可能仍在使用共享 LUT，并写入 ready map 或行队列。
- `lcd_calculate_frame()` 的错误返回还会由供行任务自行重置队列。
  另一供行任务或扫描 ISR 可能尚未退出，不满足队列重置的独占要求。
- 本地 `phase_lookahead.inc:95-105` 已有“先停扫描、等待所有已派发供行任务退出，
  再清队列”的错误处理；普通 GC/DU 路径没有同等保护。

厂商只读目录 `/Users/rick/Documents/ChatGPT/rickyos/vendor/metalio-plus-2.0.76` 的
同名 `main/boards/metalio-e-ink4-plus/display/output_lcd/render_lcd.c:120-143`
也保留普通流程超时后仅记错、重置队列的处理。因此不能把照搬该错误路径视为稳定性保证。

这是确认的错误恢复缺陷，**没有证据证明它就是此前真机白屏/USB 失联的最初触发点**。

## 已修正

路径相对 `ports/metalio-e-ink4-plus/firmware/rickyos-plus-1.2/components/metalio_board/`。

`display/output_lcd/render_lcd.c:154-200`：

1. 每轮记录两个已派发任务中尚未收到完成通知的任务。
2. 错误位的跨任务更新改用原子 OR，控制端用 acquire 读取。
3. 错误时停扫描，再等待未完成任务的通知，消费晚到的通知。
4. 任务确认退出、扫描停止后，由控制端统一清空帧通知及两个行队列，再返回错误。
   `lcd_calculate_frame()` 的四处错误出口不再自行重置队列。

如第二次 2 秒等待后仍有任务不响应，沿用现有 lookahead 路径的 `abort()` 保护，
不把仍可能有写入者的上下文交给下一次绘制或睡眠。
这会触发固件的 panic/重启策略；它是持续卡死时的保护，不能声称所有显示错误都可无重启恢复，
也不能声称这种情形下 USB 会保持连接。普通成功路径不增加等待或改变扫描节拍。

只增加一个栈上的 pending 位图，没有新任务、堆对象或帧缓冲，
没有修改 LUT、波形、时钟频率、预填行数、充电配置或电量模型。

## 复现与验证

扩展 `tests/test_plus_phase_lookahead.py`，执行真实控制端、真实供行函数及原子行队列，
用两个主机线程供行、一个线程模拟扫描。只替代 RTOS/硬件边界，开启 ASan/UBSan。

- 在修正前的真实源码上注入“一次完成等待超时、随后任务完成”，新增断言发现残留完成通知，
  测试被拒绝。预期失败日志 `.local/test-render-recovery-negative-20261022.log`；
  当时普通控制器源码 SHA256 为 `2f6aca2e736312466ba32ab44f49713de1c4d3fdf724840790b18f9301d19604`。
  该注入模拟 RTOS 超时结果与晚到通知，不是对真实 ESP32 调度耗时的测量。
- 新增队列重置所有权检查：必须没有活动扫描及供行任务，随后才执行真实 `lq_reset()`。
- 普通 GC 路径覆盖正常/慢任务、帧超时、备帧和供行中途错误、时序拒绝、晚到完成通知。
  返回后两个供行通知均被消费、队列清空、回调断开、任务全部退出；
  随即复用同一上下文执行三相正常刷新，检查逐行数据、裁剪和掩码顺序。
- lookahead 开启/关闭两种构建都执行上述普通路径；原 GL16/GL16_FAST 相位顺序、
  跨相位重叠和错误清理回归保留。
- 三种配置中的永久不响应任务，必须由生产代码 `abort()` 终止；若函数返回，
  测试程序以独立退出码 97 失败。还必须捕获生产保护分支的日志标记，
  避免把测试自己的断言失败误当作生产保护成功。

完整套件通过后，进一步加强了上述测试的日志标记区分，26 个程序用例再次全部通过，
最终定向日志为 `.local/test-render-recovery-final-20261022.log`，生产源码没有再改。

完整 Plus 回归及普通电量/试验开启 S31 构建结果保存在
`.local/offline-render-recovery-20261022/verification.json`；所有产物仅离线保存。

主机测试证明的是错误通知、任务和共享资源的交接顺序。
它不能复现真实 PSRAM 保持、DMA 电气时序、玻璃图像或 USB 枚举，
也没有证明“睡醒后的第一次刷新”必定是此前白屏发生的位置。
本轮不安排物理测试，白屏根因与 USB 失联触发点仍未确认。

复现：

```sh
python3 ports/metalio-e-ink4-plus/tests/test_plus_phase_lookahead.py
python3 ports/metalio-e-ink4-plus/tests/test_plus_line_timing.py
bash ports/metalio-e-ink4-plus/tests/run_plus_port_tests.sh
```

## 构建结果

完整 Plus 回归退出码 0，普通电量与试验开启两种 S31 构建成功。
固定源码检查、触摸后端、时钟对象及网络内存检查通过。
普通版本没有睡眠试验/诊断定义；试验版本显式开启这两个定义。均未安装。

| 配置 | BIN 字节 | BIN SHA256 | ELF SHA256 |
|---|---:|---|---|
| 普通电量、睡眠关闭 | 4907264 | `7658c653cff3a63eb0ddd4f66f6c9cd5614427dfcdcf6d14dfa97561c5eeacaf` | `6dbc0d0fda62fc99a36c13dc78ea895e2b7fabc9021aabe8b7b32437faf67bbb` |
| 试验开启、诊断开启 | 4915024 | `da96da27c76a44446b88963e26c673ff721b2c590cd8befc722ac19f514dfcfd` | `e18c4fc392f606afb676acf2f6922d77ea871d59ab912464657ba8c93c9bd795` |
