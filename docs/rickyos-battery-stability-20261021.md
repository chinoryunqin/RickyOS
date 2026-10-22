# 电量稳定性实验（2026-10-21）

## 隔离范围

- Read Pico：分支 `codex/pico-battery-stability`，基线 `6ec61ccd`。
- Metalio E-Ink4-Plus：分支 `codex/plus-battery-stability`，基线 `21382423`（Plus 1.2.4）。
- 不混入书库／存储目录管理实验；Pico 未提交、推送、发布或刷机。
- Plus 的独立第一阶段体验版已获授权刷入；该分支后续进度以其自身实验记录为准，
  本文件的 Plus 算法和产物描述仅作为早期对照，不能用来认定 Plus 当前工作树状态。
- 不写 PMU／BQ27220 的配置、学习容量、充电电压、校准区或 OTP。

## 修改依据与边界

原始 Pico `BoardReadPico.cpp:784-804` 把协议的未知 SoC 转成零；
`BatteryMonitor.cpp:229-246` 又把零解释成未知，并回退到 10% 阶梯电压表。
`HalPowerManager.cpp:440-454` 对有效 gauge 读数直接上屏，只有 1.5 秒轮询缓存。
Plus 的实际代码在移植目录恢复的 `upstream/`，不能拿仓库根目录代替：
`BatteryMonitor.cpp:250-258` 使用 3.30–4.33V 直线估算，没有采用已验证的容量模型。

本次先修读数和显示稳定性，不声称解决电池容量标定：

- Pico 保留合法 0%，未知保留协议 0xFFFF；检查 CRC、有效标记和电压范围。
- Pico 百分比、电压、充电状态来自同一帧；未知 SoC 的旧 PMU 可用连续插值的电压估算。
  该曲线沿用原通用 1S 节点，只用于后备估算，不冒充该电池的实测曲线。
- 两台共用独立编写的固定大小显示策略，无堆分配、后台任务、定时唤醒或持久化写入。
  按需读取，最短间隔 5 秒；不是承诺每 5 秒后台采样。
- 单采样者，忙时返回原子缓存；无效样本保留可信值并打断待确认状态。
- 三样本中位数；来源切换需同一来源连续三次且跨度至少 10 秒。
  插拔／充电方向变化等待 30 秒；大变化重复确认 30 秒；
  反方向修正重复确认 120 秒后允许校正，不永久锁定百分比。
- 连续两次有效低电量（不大于 5%）绕过正常显示限速。原低压保护路径未改。
- 超过两分钟未采样不积累追赶额度；轮询和时间窗口支持毫秒计数回绕。
- 不显示“未知电量”、问号或横杠标签。首次没有可信值时内部用 101 标记待定，
  UI 暂不绘制电量；真实 0% 仍显示 0%。读数恢复后下次页面绘制可显示。
  不加入额外屏幕刷新，也不把读取失败伪造成 0%。
- Plus 暂保留原电压估算，仅增加每分钟只读 SoC／剩余容量／满充容量／设计容量／运行状态诊断。
  接近零电流不被当成明确充电或放电方向。需实机证实出厂电量计模型后再决定是否采用原生 SoC。

## 公开实现调查

2026-10-21 核对的冻结版本：

- [厂长 d5b3d33a 的电量显示过滤](https://github.com/wegooo-cell/read-pico-reader/blob/d5b3d33af8560998efe612ce0f7b07a07691359d/components/read_pico_pmu/pico_soc_filter.c)：
  有 PMU 有效性区分、五样本过滤和拔线高电量稳定处理。该文件带个人学习用途许可；
  未复制、移植、链接或分发其代码。本分支规则和实现独立。
- [CrossMux eaf5c73a 的 HAL](https://github.com/0x1abin/crossmux/blob/eaf5c73a71aaa03e372b6bc274923c2cc621a033/lib/hal/HalPowerManager.cpp)：
  gauge 仍以短缓存直接显示。
- 该主线实际引用 SDK `9aa7e95e02c6374a23a562c883eb302552ff24f1`；
  其中 Read Pico 的未知／零值混同仍存在，没有现成的小纸修复可直接采用。
- [SDK e040e9a2](https://github.com/0x1abin/freeink-sdk/commit/e040e9a2095b0b109316396da4b5c05a081507af)：
  修正 X3 BQ27220 默认 3000mAh 与真实容量不符的问题，会解封并改写参数。
  对 Plus 是排查线索，不能套用 X3 的 650mAh 或未经验证的写入流程。
- [SDK 0b6258d2](https://github.com/0x1abin/freeink-sdk/commit/0b6258d258f43238719ceaa7ff5d7c9081530fcc)：
  文石 Onyx Picco 的 LiHV 电池曲线／充电芯片初始化，**不是 Read Pico**，不移植。
- [TI BQ27220 TRM](https://www.ti.com/lit/ug/sluubd4/sluubd4.pdf)：
  只读诊断采用 Voltage 0x08、Current 0x0C、RemainingCapacity 0x10、
  FullChargeCapacity 0x12、StateOfCharge 0x2C、OperationStatus 0x3A、DesignCapacity 0x3C。

## 厂商电池测试表（Plus）

用户提供 `424860-2000 0.5C充电至4.3V 放电至3.3V.xlsx`，原文件只读，
未上传到仓库。SHA256：
`45d5698e685162faa8ff470ed0f434163b2c09d7be7af812b7f7b48396ffd291`。

- `Step!C6:K9`：完整第二循环充电 1941.1mAh；约 1A 放电到 3.3V，放出 1870.7mAh。
- `Record!A1500:L2174`：完整放电 675 个样本；
  在该测试窗口剩余可放容量约 50% 时，端电压约 3701.6mV（第1837行）。
- `Record!E2174:E2205`：停止放电时 3299.8mV，五分钟后 3625.3mV。
  这是负载解除后的回弹，不是凭空充入容量。
- 第一段放电是从部分电量放到 3.0V，第三循环充电只到约 4.0V；
  不能把所有行混成一条百分比表。温度列为零，不能据此认定环境为 0°C。
- 这是约 0.5C 的带负载曲线，不是设备阅读负载下的 OCV/SOC 标定；
  没有据此改充电截止电压、低压阈值或直接替换 Plus 的容量算法。

## 验证与后续验收

- 2026-10-21 第一阶段验证：Pico 6 项电量测试通过；Plus 4 项适用测试通过，
  两项 Pico 专用测试跳过。Pico 阅读 chrome 5 项通过，状态图标 4 项通过，
  一项依赖可选 SVG 转换器的测试跳过。编译使用 ASan/UBSan 的主机回归通过。
- 两台独立完整固件编译通过；Pico 应用 5,901,632 字节，
  SHA256 `40aafdc63784c9b6f5100331d8ce5b943a411c3e7b10ef01485a22f2ea05f428`；
  Plus 应用 4,908,128 字节，
  SHA256 `34b897b1c3916932cd767f137294469072c4c611f2d8021c0ebe929f1ce6cd2f`。
- Plus 最终源校验通过：2844 个固定源文件、141 项适配记录，
  保持原应用／SDK 基线，未作上游整体升级。
- 自动化测试编译实际过滤器、HAL 百分比方法、UI 电量文字和 SDK 状态读取方法。
  覆盖有效零、未知／I2C／CRC 错误、轮询频率、来源交替、瞬时跳变、
  持续校正、充电切换、低电量、休眠间隔及计数回绕。
- Pico 运行 `python3 scripts/tests/test_battery_stability.py -v`。
- Plus 运行 `python3 ports/metalio-e-ink4-plus/tests/test_battery_stability.py -v`。
- Plus 源改动通过 `sync_plus_design.py --record --add-source lib/hal/BatteryDisplayFilter.h`
  记录到补丁及清单；必须重新恢复后也能复现。
- 完整固件编译及补丁校验不等于电量准确度验收。设备仍需：
  冷启动、充电／充满／拔线、连续翻页与联网负载、待机恢复、正常使用逐步放电；
  对照原始 mV、电流、原始 SoC、显示值和时间。不要求用户主动放至保护关机。
- 本分支不自动刷新状态栏，刷新时机仍由现有页面负责；不宣称已测得耗电或续航改善。

## Pico 第二阶段：先厘清原始估算，再做独立标定

2026-10-21 用户要求继续处理 Pico，并强调不把厂长固件当准确基准。
本节覆盖原先的通用电压后备表／充电状态处理；Plus 的第二阶段在另一工作树，
没有把 Plus 的电池曲线、1870.7mAh 或库仑计代码移入 Pico。

### 证据和能力边界

核对的官方固定版本为 MindReset/read_pico_firmware
`6a247cc94420b85c650b32a6b2c063a360070eee`：

- [PMU 1.0.8 主机说明](https://github.com/MindReset/read_pico_firmware/blob/6a247cc94420b85c650b32a6b2c063a360070eee/components/read_pico_pmu/docs/llms-full_zh_cn.md#电池)：
  SoC 由电压查表而来，官方标注约 ±10 个百分点；充电期间保持先前值，判满置 100%。
  这不是我们对本机测得的误差，也不是硬件电量计的真实容量读数。
- 同一说明列出的运行时电池电压更新约每 30 秒一次。因此反复读取快照，不等于获得
  多个独立 ADC 样本；中位数和确认窗口只管理显示，不能据此声称提高了测量精度。
- [公开接口](https://github.com/MindReset/read_pico_firmware/blob/6a247cc94420b85c650b32a6b2c063a360070eee/components/read_pico_pmu/include/read_pico_pmu.h)
  暴露电压、原始 ADC、估算 SoC、充电状态和 MCU 温度，没有电池电流／累计 mAh 读数。
  MCU 温度不冒充电芯温度。没有依据宣称现有硬件可直接进行 Plus 式库仑积分。
- 文档的 BATTERY_SET_PROFILE 命令返回 NOT_SUPPORTED。ESP 应用不能通过该命令替换
  PMU 内部查表；不改写 PMU、充电、低压阈值或 VCOM。

### 已实现的确定性修正

- 后备估算从通用 1S 表改为官方 Pico 表的独立插值实现。举例：3.70V 的官方参考为 45%，
  旧后备表约 13%；3.30V 官方参考为 5%，旧表已经显示 0%。统一参考减少来源切换的
  额外偏差，但官方表本身仍不是本机经过独立标定的曲线。
- 保留原始千分比，百分比按最接近的整数呈现；PMU 电压有效范围按公开协议
  2700–4400mV 检查，不改保护阈值。CRC／有效标记检查及真实 0% 处理保留。
- PMU 给出的百分比单列为 `PercentageSource::PmuVoltageEstimate=4`，
  不再把它标作硬件容量测量。保留原 source 0／1／2，3 留给 Plus 的主机容量模型。
- CHARGING（2）与 FULL_INFERRED（3）分开；3 不是流入电池的充电电流。
  UNKNOWN／FAULT 不伪装成确认拔线。USB 检测改用 PMU 推断的接入状态，
  以免充满后错误地认为 USB 已拔出；没有改变网络、休眠的设置或控制逻辑。
- 同一 PMU 快照最多每 30 秒（按需）输出一行 `BATREF`，字段为
  mV、PMU 原始千分比、参考表千分比、原始充电状态、电源接入有效性、显示值和来源。
  不增加 I2C 读取、强制采样命令、定时唤醒、屏幕刷新、SD／Flash 写入或堆分配。
  启动身份日志附实际 PMU 协议／固件版本，不能凭文档版本假设实机也是 1.0.8。
- `scripts/pico_battery_characterize.py` 可读日志转 CSV，或不操作 DTR／RTS 的只读串口记录。
  只保留电量记录，不保存无关日志；独占创建输出，不覆盖旧测试。
  工具始终标记 `accuracy_validated=false`：PMU 与参考表相符，不是准确度证明。

### 更好的方案及验收

现有机型优先做 **同型号电芯的独立标定与主机模型**：

1. 向厂商确认电芯型号、批次、放电曲线及 ADC 标定误差；Plus 的数据不能替代这些资料。
2. 由具备电池测试条件的厂商／实验室记录电池端的累计放电容量，同时记录 Pico 电压。
   覆盖正常阅读、连续翻页、联网三种负载和充电后静置。USB 输入端累计充入 mAh 含整机
   消耗和转换损失，不能直接当作电池剩余容量真值；也不能用“运行时间百分比”代替它。
3. 用一轮数据建立带工况的曲线，另一轮／另一台设备验证。静置时的电压才可用作
   更接近 OCV 的校正候选；仅凭电压变化小，不能证明电池已充分静置。
4. 数据不足时仍保留当前估算，不编造充电曲线或“每小时扣几点电”。先在旁路模型里
   比较，再决定替换 UI 来源；继续独立保护低电和无效读数。

如果以后允许改硬件，可评估独立电量计：
[TI 电量计方案说明](https://www.ti.com/product-category/battery-management-ics/battery-fuel-gauges/overview.html)
与 [ADI ModelGauge](https://www.analog.com/en/products/max17048.html)。前者可组合电流积分和电池模型，
后者提供基于电池模型的方案；不是下载一个算法就能在当前 PMU 上取得同样精度。
芯片／电池模型仍须匹配具体电芯，未经验证不承诺具体误差。

只读采样示例（需这次体验固件，设备正常运行，不进入 USB 磁盘）：

```sh
python scripts/pico_battery_characterize.py capture --port /dev/cu.usbmodem101 --seconds 300 --out pico-battery-run01.csv
python scripts/pico_battery_characterize.py analyze serial.log --out pico-battery-offline01.csv
python scripts/tests/test_battery_stability.py -v
python scripts/tests/test_pico_battery_characterize.py -v
```

注意：接电脑 USB 通常也会给设备供电。这个串口工具适合核对充电／充满／插拔和解析行为，
不能把 USB 供电下的数据冒充不插线的阅读放电曲线。独立放电标定需要厂商测试平台在电池端
计量并采用不改变供电工况的记录链路。此处没有为测量而修改 USB 供电或充电控制。

这一阶段不承诺电量已准确，不借“更稳定”冒充准确度提升。
开发完成时尚未刷机、提交、推送或发布；本次用户授权的分支交付见文末。

第二阶段最终验证：电量 8 项、采样工具 2 项、电源模型 8 项、设置 5 项、
USB 配置 4 项、阅读 chrome 5 项、状态图标 4 项，共 36 项通过。
另 1 项图标重新生成测试因缺少可选 `rsvg-convert` 跳过。
原生 C++ 测试使用 ASan／UBSan；非模拟器硬件准确度尚未验收。
`pio run -e rickyos_readpico` 最终编译通过，应用 5,902,096 字节，
BIN SHA256 `18b43417ce78e49ddc409e0b2e8daa66f41edd78ebafff60adafe9a8c5dd1ca5`。
该镜像是未刷入的 Pico 第二阶段产物，不是前文第一阶段或 Plus 镜像。

## 分支交付（2026-10-21）

用户要求先推送电量优化分支。交付目标为私有仓库 `chinoryunqin/RickyOS-dev` 的
`codex/pico-battery-stability`，保留与 Plus、书库／存储实验的隔离。
本次包含已说明的代码、回归测试、只读采样工具及进度记录，不上传固件、设备日志、
备份或厂商原始表格；不合并正式分支、不发布固件，也不执行新的刷机。
提交和远端是否同步以实际 Git 提交／远端分支 SHA 为准；准确度仍需独立实测验收。

## Pico 书库与电量整合体验（2026-10-21）

用户先验收书库／存储基本正常，随后明确要求把 Pico 电量改动一起合入并刷机测试。
原分支的隔离要求是此前交付时的历史状态；本次使用独立本地体验分支
`codex/pico-library-battery-trial`，基于书库 `70740e22` 引入电量 `e1c58b3f`。
保留 `codex/library-storage-sources` 和 `codex/pico-battery-stability` 的既有历史；
不改 Plus 1.2.5 或任何线上目录，本次未授权正式发布。

- 当前整合完整构建通过，应用 5,924,800 字节，未超过设备在线升级大小门槛。
- BIN SHA256：`03b02ae84fe6eabd90ccda49710bedac414bc50fa68c040bf27224d9bdabecca`。
- ELF SHA256：`3ef30c48fc63f0992e5049b82b1073831f2a47b471d19e54dc9a3b7b524476be`。
- 电量／采样、设置／电源／USB／阅读状态栏，以及书库／存储工作流专项共 56 项通过；
  另 1 项可选图标重新生成测试因缺少 `rsvg-convert` 跳过。
- 修正旧存储测试与新版容量概况不一致的断言，并恢复原先落在 `unittest.main()`
  之后而未发现的两项测试；相关 7 项全部通过。仅修改测试，不改变已刷书库行为。
- 应用描述的框架元数据仍显示 `wt-sync`／旧的 eego 构建描述，不能用它作为机型身份；
  真实应用含唯一 Read Pico 标签，运行时版本为 1.2.5，产物按以上摘要追踪。
  正式发布前仍需单独统一这项元数据，不把旧描述当作已经验收的发行信息。

安装已完成：Pico 身份、分区 MD5、OTA CRC／活动 app0 和在机书库镜像摘要核对通过；
只写 app0（0x10000），应用写后摘要一致，app0 外 Flash 摘要不变，显式 USB RTS
复位后运行时报告 1.2.5、恢复上次 EPUB 并完成绘制。没有改变启动槽或操作 SD 卡。
本机实际 PMU 为协议 1.1／固件 1.0.9／硬件 1；不能把调查的 1.0.8 文档版本当作在机版本。
恢复阅读的首轮日志没有电量采样行：电量获取为按需逻辑，仍需在首页／控制中心查看并记录。
随后 45 秒只读串口采样未收到 BATREF 行（0 个样本），已释放串口；不能据此推断电量精度、变化趋势或充放电状态。
用户随后明确反馈「显示正常，继续观察电量变化」，确认合并版百分比／充电图标基础显示正常。
此反馈不扩大为长期充放电精度、插拔瞬态或待机恢复表现全部通过；这些继续由用户正常使用观察。

本次不新增电池曲线标定结论，不修改 PMU／充电／保护参数。仅本地整合与体验安装，未提交、推送或正式发布。
原始日志、恢复文件和固件位于本机 `.local/pico-library-battery/`，不提交这些文件。
