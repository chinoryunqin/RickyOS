# Plus 静态浅睡：停止实机试错后的差异核对

当前结论：有对照厂商代码，但移植顺序不代表运行前提等价。static4 在第二次 Timer 成功返回后、P14 恢复写入窗口再次 power-glitch 复位；40ms恢复间隔的假设失败。停止候选刷机与拔线复测，恢复测试前turnC。充电参数、电量模型和brownout阈值不改。

后续离线核查见 [P14逐位、实际传输与故障注入记录](plus-p14-reset-offline-audit-20261023.md)。两边正常位序列一致；已复现按键方向恢复失败仍继续写P14的检查缺口。原厂也有此缺口，尚不能认定为复位根因。阶段记录没有证明第二轮按键恢复/I2C成功，不能将它当成已验证前提。

## 已对照的原厂依据

原厂根目录 `/Users/rick/Documents/ChatGPT/rickyos/vendor/metalio-plus-2.0.76/`：

- `main/boards/common/power_policy/README.md`：静态Park/绘制冻结、最长60s分片、GPIO2/P17唤醒。
- `power_hw.cc:293–382`：SD→TPS→总线→IMU→屏蔽其他键→电轨关闭；返回时键→电轨→SD→USB→显示总线。原厂Timer每片也恢复外设。
- `main/boards/metalio-e-ink4-plus/display/epd_board_metalio_eink4_plus.c:225–235`：P14先恢复、随后P5。
- `input/metalio_keys.cc:202–219`：屏蔽/恢复扩展键后同步普通按键状态，有20ms等待。
- `periph/metalio_sd.cc`：SD主机卸载、S31 CNNT回GPIO、引脚低电平/保持和释放。
- `main/boards/common/sensor/sc7a20h.cc:105–137`：静默共享中断配置。

## 必须核实的运行差异

| 环节 | 原厂 | RickyOS当前候选 | 未证明的前提 |
|---|---|---|---|
| I2C驱动 | 原厂旧版IDF驱动/共享锁 | Arduino Wire1、新版IDF总线；外层共享锁 | 浅睡后控制器恢复、时钟/引脚及并发所有者等价 |
| 文件系统 | 原厂VFS SdCardManager | SDK/SdFat；新增只读句柄关闭/重开租约 | 仅host回归不能证明所有实机句柄/任务均安全 |
| 按键状态 | 原厂ResumeAfterSleep同步按键任务状态并等待20ms | 板级恢复方向、主循环稍后采样 | 中断、锁存与任务采样顺序等价 |
| CPU/PM | 原厂sdkconfig PM_ENABLE=y | 当前PM_ENABLE关闭，手动CPU频率和显式浅睡 | 原厂电源域、外设恢复策略与当前IDF版本一致 |
| 电轨 | 同名P14/P5输出顺序 | 已按相同顺序，但第二片P14写入时复位 | 实际板版、使能网络、负载和上电前状态一致 |
| 屏幕 | 原厂LVGL/屏幕Park | RickyOS EPDiy/LCD资源释放重建 | 原厂保留内存/任务生命周期与本地一致 |

不要把上述差异直接宣布为根因；先逐项生成可检查证据。P14阶段只缩小了故障窗口，仍可能涉及整口写入其他位、总线或恢复上下文，不等同于已测得P14浪涌。没有原理图/模拟波形时应明确保留未知。

## 再次实机前的门槛

先完成Port1逐位锁存/方向及并发写入者审计、真实板版和P14网络核对、相同IDF的睡眠恢复路径对照，形成一个由证据支持、可区分竞争原因的方案。单元测试只能验证控制流，不能作为电源完整性证明。没有通过这些门槛，不继续追加static5等候选或让用户循环操作。
