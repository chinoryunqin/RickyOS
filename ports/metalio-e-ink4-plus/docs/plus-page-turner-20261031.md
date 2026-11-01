# Plus 蓝牙翻页器候选（2026-10-31）

状态：功能候选，已刷入测试机并核对启动，未提交、未发布。基于已发布 Plus 1.2.12
`9b3e96c0ab4ac2e8d79b5507604f67f2eb58293a`，分支
`codex/plus-page-turner-20261031`。仅参考旧实验的 BLE 核心提交
`27d201db4ec33543859caaa39c8ee41e95f2d953`，没有合入旧音箱实验。

## 操作与范围

- 控制中心增加蓝牙快捷按钮：点按开启／关闭翻页器；长按进入配对、扫描、按键映射与设备管理。
- 按钮区分关闭、等待联网、未连接、已连接；开启但未连接仍为选中状态。
- 默认支持方向键、Page Up/Down、音量类 HID 翻页键；用户自定义映射优先。
- 保留 SDK 的配对、取消、忘记设备、自动重连及按键捕获；需要输入配对码的设备会显示配对码。
- 没有音箱连接入口，没有 BH1098 音频模式切换。原本本机扬声器、录音及音量功能保持原路径。
- 待机不因覆盖在阅读器之上的页面继续保持 BLE 运行。

## 实现依据与资源约束

以下路径相对 `firmware/rickyos-plus-1.2/upstream`；该目录为还原源码，变更记录在
`upstream-plus.patch` 和 `port-changes.json`，不得直接提交被忽略的 upstream 文件。

- `src/activities/util/FrontlightPanelActivity.cpp:236` 的快捷按钮事件增加长按配对；
  `:519` 开始的状态和开关方法负责保存偏好、启动、停止及错误反馈。
  复用 ACTION_TILE，图标在编译期从现有图标生成，没有为按钮新增运行时堆分配。
- `src/activities/settings/BluetoothSettingsActivity.cpp:217` 处理启动重试、配对码与连接状态变化。
- `src/BleInput.cpp:171` 的 radioBusy 和 ensureStarted 统一 Wi-Fi／BLE 排他策略。
  使用 Wi-Fi 时不打断网络，蓝牙偏好仍保存；网络会话结束后，在阅读器或蓝牙页面／控制中心尝试恢复。
  这不是 Wi-Fi 与 BLE 同时工作。
- `src/NetworkStartup.cpp:74` 对 Plus 保留控制器启动时的内部 RAM 预留，避免使用网络后只能重启才能再次开启蓝牙。
  关闭 BLE 仍释放运行中 host 的动态内存，但保留启动预留会减少其他功能的可用内部 RAM，必须实机验证。
- NimBLE host 优先 PSRAM，限制 1 个连接、4 个绑定、MTU 23 和收发缓冲数量。
  没有降低现有内部内存／最大连续块启动门槛；低内存时显示提示并拒绝启动。

## 构建与发布阻塞

必须使用补丁 IDF `9a97f6c54ec638111ce55cd36581b3c192f15207` 和
NimBLE-Arduino 2.3.8。用全新 SDK config 启用 BLE，旧 release 配置不会自动启用新增 defaults。

从本 port 目录构建：

```sh
RICKYOS_PLUS_VERSION=1.2.13-bt1 \
RICKYOS_IDF_PATH=/Users/rick/rickyos/wt-plus/ports/metalio-e-ink4-plus/.local/esp-idf-v6.1-r \
RICKYOS_SDKCONFIG="$PWD/.local/sdkconfig-page-turner" \
bash scripts/build_rickyos_plus.sh
```

已构建候选：6,188,704 bytes；SHA-256：
`9efd403244abce76a80ec96acedacc48766dd2b967068a8262b9e867b7f2b7d6`。

现有 OTA 合约上限为 6,029,312 bytes（0x640000 减 512 KiB），候选超出 159,392 bytes。
见 `src/network/RickyOtaPolicy.h`、`RickyOtaManifest.h` 与 `OtaUpdater.cpp`。
候选虽然能放进物理应用分区，但不能通过已有版本的 OTA 校验。不得改小安全预留或跳过校验来发布；
需要继续做体积精简或设计完整的升级迁移方案。`bt1` 是串口试验包标识，不是正式 OTA 版本号。

冻结包及机器可读记录在本机：
`/Users/rick/Documents/ChatGPT/rickyos/output/device-tests/plus-page-turner-20261031/`。
这不是完整安装包，不应上传到正式更新源。

## 验证与待办

- SDK BLE HID host 桩环境：265 项检查通过，覆盖配对／断连／重连与输入报告解析；不等于真机验收。
- 实际按键路由、快捷按钮状态／保存／错误提示、无线内存释放策略的 host 测试通过（ASan/UBSan）。
- 实际控制中心布局 256 种组合通过：横竖屏、540／684 宽、阅读模式、外设能力及四种蓝牙状态。
- 固件构建、链接 BLE host、源码记录检查，以及全新目录离线还原后逐文件校验均通过。
- 完整 port 回归 `bash tests/run_plus_port_tests.sh` 已退出 0；保留既有 6 项条件性跳过，不能据此替代真机验收。
  日志：`/Users/rick/Documents/ChatGPT/rickyos/output/research/plus-page-turner-regression-final-20261031.log`。
- 回归中修正一处旧测试：1.2.12 已将自动关机设置移动到显示与待机；测试不应要求它仍出现在壁纸页。

仍需连接真实翻页器验证扫描、首次配对、配对码、记忆重连、按键映射、一次按键一次动作、待机恢复。
还需检查开启／关闭 BLE 前后 Wi-Fi 下载、热点传输、USB 磁盘、录音与显示的内部空闲内存及最大连续块。
没有真机数据前，不宣称连接稳定性、功耗或性能改善。

本候选没有合并另一个独立的 EPUB 容错实验；后续发布整合时必须另行核对。

## 2026-10-31 串口安装

用户授权后，核对 Plus ESP32-S31／MAC `1c:29:04:d0:2a:50`。设备当前采用两个 OTA 应用槽，
不是早期含厂商 apps 槽的三槽布局；安装助手第一次在布局校验时拒绝、没有写入。
确认实际分区 MD5、OTA CRC 与当前 app0 后，按实际槽数量解析选择，仅写入 app0 `0x10000`。
先前应用版本为 1.2.12；写入上述冻结镜像后，设备摘要及 bootloader／分区／OTA／NVS 保持检查通过。
没有访问 SD，也没有进行全 Flash 备份。

40 秒被动启动日志确认 1.2.13-bt1、匹配 ELF `35108c876…`，进入 InxRecent，显示刷新 result=0x0，未见崩溃。
首页稳定时内部空闲约 65,863 B、最大连续块 30,720 B；这只是未启动 BLE 时的现场快照，
不代表 BLE 启动或外设共存验收通过。真实翻页器测试仍待用户操作。
本轮是串口安装，不是 OTA 验收；体积超限的发布阻塞仍在。

## bt2：实机内存门槛拒绝启动

bt1 用户在控制中心开启后提示内存不足。实际日志显示内部 free/max 为 64,451／29,696 B，
PSRAM 空闲约 12 MB；Explicit 门槛要求 71,680／24,576 B。释放字体缓存前后快照相同，
所以本次失败为内部空闲总量不足，不是 PSRAM 不足或最大连续块不足，也没有进入控制器初始化。
日志在 `output/device-tests/plus-page-turner-20261031/interaction.log`（本机 workspace）。

bt2 保留所有启动门槛、64 KiB 内部预留、显示 DMA 缓冲及任务栈，只启用 IDF 已提供的
`CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY`，并明确设置 PSRAM 的早期硬件初始化／启动初始化。
依据补丁 IDF 的 `components/esp_psram/Kconfig.spiram.common:137` 和
`components/bt/controller/linker_esp_ble_controller.lf:77`。这个官方链接规则同时迁移允许的
bt／lwIP／Wi-Fi BSS，不是把控制器 ISR 或显示缓冲任意移到 PSRAM。

最终 ELF 验证 `dns_table` 为 `0x500004a8`、`_bt_bss_start` 为 `0x50000000`，
控制器 `_bt_controller_bss_start` 仍为内部地址 `0x2f0395bc`。
本 IDF 的 NimBLE 主机全局状态在单独的 libnimble archive，`ble_hs_state_ctx` 仍在内部 RAM；
不能把该修改描述成“全部蓝牙内存外置”。与 bt1 对照，约 20 KB 已命名的 BSS 数据改放 PSRAM；
实际可用内存增量以启动日志为准。

bt2 镜像：6,188,880 B，SHA-256
`fc0d6ef48c84d11bf8db4cb8c9f75c357720e3b4d35e08903cea33e89f466555`。
补丁 IDF 构建、实际 ELF 放置检查、按键／控制中心、Wi-Fi 安全配置、USB 后端、待机及源码校验通过。
本轮没有重新运行全部 port 回归，也没有因此声明 Wi-Fi／USB 真机兼容已经验收。
固件与记录在 `output/device-tests/plus-page-turner-bt2-20261031/`（本机 workspace）。

同一 Plus 已从 bt1 刷入 bt2，写后摘要与非应用区域保持检查通过，启动版本匹配，进入首页，显示 result=0x0。
10 秒快照内部空闲为 86,175 B（bt1 同位置 65,863 B，增加 20,312 B），最大连续块为 51,200 B
（原 30,720 B）。这是 BLE 尚未启动的快照，已请用户再次开启并扫描；不能写成已完成配对验收。

bt2 用户已完成连接。日志确认两次 `BLE HID host started`、PSRAM 分配探测成功，并出现 connected；
连接后内部空闲约 43,727 B、最大块 25,600 B。退出设置页后主循环按旧页面策略主动 stop，
继而进入阅读器／再次开启时内部空闲 64,131 B，触发原 Explicit 门槛。用户反馈与日志一致，
不能把这次重复启动拒绝描述成配对失败或 PSRAM 耗尽。

## bt3：普通页面切换保留已运行的连接

依据 `upstream/src/main.cpp:108` 的 updateBluetoothLifecycle：原 wanted 只看 Activity 的
keepsBluetoothAlive，设置主页／系统首页没有该标志，因此离开配对页就主动关闭。
对 Plus 追加“已经运行且当前不是待机”的保持条件，不在普通页面切换中重建控制器。
不开启新的首页自动启动；用户开关关闭、Wi-Fi、USB 独占访问和待机仍执行 stop。
Reader 的自动重启和所有内存门槛保持不变。此项是连接保持修复，不等于已经验证待机／网络结束后的重启。

host 测试执行实际 updateBluetoothLifecycle，覆盖配对后返回普通页面、进入阅读和章节准备时保留，
以及待机／Wi-Fi／USB／关闭开关时停止；其他机型保留原页面策略。测试通过。

bt3 已刷入同一 Plus 并通过设备摘要／非应用区域保持检查，启动版本匹配，进入首页，显示 result=0x0。
镜像 6,188,896 B，SHA-256 `2a31106c61405094ed92d400bc20c3715022a0c743404d8f6850cfa9110d7422`。
编译、实际生命周期 host 测试、ELF 内存放置、待机 backend 与源码记录检查通过；
bt2 的完整 port 回归也已退出 0。bt3 普通页面的连接保持仍待用户实测反馈。
本机记录：`output/device-tests/plus-page-turner-bt3-20261031/`。

## 2026-11-01：bt4t 阅读中启动诊断候选

用户报告型号／广播名称为 Free3R。bt3 后续实时采集显示设备重新启动后先打开了书，
内部空闲约 68,539 B，随后开启蓝牙仍被 71,680 B 的 Explicit 门槛拒绝，没有建立本次连接。
因此当前“按键没反应”样本不能用来推断 Free3R HID 不兼容；先处理这条启动路径。
已有 bt2 connected 也不能证明 bt3 此时仍在线。

bt4t 保留 70／80 KiB 蓝牙门槛、24／32 KiB 连续块门槛和 64 KiB 内部预留，
将普通 malloc 的内部优先阈值从 2048 改为 0：可放入 PSRAM 的普通 UI／书籍元数据优先使用 PSRAM，
显式 INTERNAL／DMA 和任务栈仍按原能力分配，PSRAM 申请失败时仍采用 IDF 原有回退。
这会影响普通小对象的访问性能及其他外设的分配初态，不能仅凭编译声称无回归；需核对真机阅读、
蓝牙、Wi-Fi 和 USB。同步更新 `scripts/check_plus_web_memory.py`，防止拿旧 SDK 配置构建。

临时诊断由 `RICKYOS_PLUS_BLE_DIAG=1` 单独启用：

- 串口 `CMD:BLE_DIAG_OPEN` 暂时关闭 BLE（不保存开关）并打开原有最近阅读图书。
- `CMD:BLE_DIAG_START` 按当前阅读／显式场景执行真实内存门槛、分配器探测及启动。
- `CMD:BLE_DIAG_HOME` 返回首页，核对既有连接未被页面策略停掉。
- 最多记录 32 条 HID 原始报告（每条最多 16 B）及 32 条已解码按键／映射结果；
  只用于本地排查，不提交原始数据。诊断包不要用于正式发布。

自动循环脚本位于本机 `output/research/run-plus-ble-reader-bt4t-20261101.py`，计划自动执行三轮
“打开原有图书 → 阅读模式启动 → 返回首页保持”，不需要用户反复配对。
串口命令和按键诊断在普通构建中关闭。诊断读取原有书籍，正常阅读可能更新该书缓存／最近阅读记录；
不修改书籍源文件，不持久化临时蓝牙偏好覆盖。

构建已通过，来源检查为 2842 文件／308 项适配；候选 6,189,840 B，SHA-256
`57b6157e9167fc0c4bc414973ec459ae6aeae9cec8911b4ca514ad345a8cad48`。
265 项 SDK HID host 检查、实际输入／生命周期测试、USB 后端、配置审计通过，
完整 port 回归退出 0，新目录离线还原及逐文件摘要校验通过。
设备当前未连接，尚未刷入或取得该候选的真机内存结果；OTA 体积超限的发布阻塞仍然存在。
本机冻结包与记录：`output/device-tests/plus-page-turner-bt4t-20261101/`。

### 自动测试入口修正与三轮实测

bt4t 刷入成功，但原自动测试只监听到输出：Plus 日志由 IDF console 输出到 USB，
而 main 原命令入口读取的是未初始化的 Arduino UART0。bt4u 起仅在 BLE_DIAG 下轮询已有 USB RX FIFO，
48 字节固定行缓存、每轮最多读取 64 字节，按 IDF 驱动顺序确认 RX packet 状态；不安装第二个 USB ISR。
host 测试覆盖分包、CRLF、超长丢弃、缓冲区容量及重同步。

自动路由不产生实体触摸事件，也不会自动唤醒 3 秒空闲后降低的 CPU 频率。
最初自动开书出现 legacy 供数欠载；诊断会话补上性能锁后，同一开书路径不再报欠载。
该锁仅在 BLE_DIAG 中存在，普通候选不得保留该启用标志，普通省电行为尚需最终整包复测。
测试开始蓝牙前也必须像正常触摸入口一样 cancelIdleRender，再等待 RenderLock，
否则会等在字体后台预热之后；修正后阅读页启动耗时约 154 ms。

另核对编译清单发现 Plus 实际 display 源码来自 `components/metalio_board/display`，
不是 SDK 副本。早先仅修改 SDK 副本的两处改动已经撤回；实际修改为 port 驱动的
`output_common/line_queue.c` 和 `highlevel.c`：行指针表及 dirty_lines 明确申请 INTERNAL|8BIT，
保持原有队列深度、行缓冲大小和 boot-time OOM 断言。
新内存测试直接编译实际 port 文件，覆盖布局、内部能力标记、生命周期及 65 个原有 OOM 断言。
实际 LCD 并发 host 测试通过；曾在与固件构建并行时出现一次故障注入断言未触发，
隔离重跑原断言后通过，没有放宽测试条件。

最终用于三轮验证的诊断镜像仍标为 1.2.13-bt4v，由摘要区分内部试验修订：
6,190,592 B，SHA-256 `9f7cf9cd3c97aa4c73d22d6753d6174fb8798f5cfcef8b757d5288a84a749b67`，
本机目录 `output/device-tests/plus-page-turner-bt4v4-20261101/`。
同一 Plus 三轮“打开原有 EPUB → Reader 场景启动 BLE → 返回首页保持”全部通过，
每次 after-start 内部 free 均为 100,519 B，最大连续块均为 63,488 B；
无 Start rejected、LCD first fault、underrun、非零显示结果或崩溃。
这里验证的是 host／控制器启动和页面生命周期，不能替代 Free3R 实际连接／按键与普通候选验收。
已请求用户唤醒 Free3R 并各按一次前后翻，正在记录报告。


### Free3R 实机反馈及普通省电候选 bt5

用户确认“能正常翻页”。bt4v4 日志捕获 Free3R 的三字节报告 `01 00 00` 和 `00 02 00`，
分别命中已有自定义映射 PageBack 和 PageForward，释放报告为 `00 00 00`。
这验证既有自定义按键映射，不应描述为 Free3R 出厂默认映射已内置。
日志另观察到对端断开后重连，断开原因未确定。

普通候选 1.2.13-bt5 已串口写入同一 Plus，摘要校验通过；bootloader、分区表、OTA 选择和 NVS 保持一致，
没有访问 SD。镜像 6,188,896 B，SHA-256
`ab35b8ecbb008977b172976670bec39382fc40c900f189844c504ea3e5a3b058`。
实际 ELF 和编译参数确认无自动诊断命令、持久性能锁或原始 HID 报告输出。
最新完整 port 回归退出 0（有条件跳过项），补充诊断隔离检查通过。
正常省电包的长时间阅读、Wi-Fi 下载及 USB 拷贝尚未完成实机组合验收。
三轮内存稳定仅限已测场景，不代表证明整个系统无泄漏。

内存配置是全局变化：普通小对象优先 PSRAM，可能影响小对象访问延迟；新增 BLE 仍占部分内部内存。
保留 65536 B 内部预留与原网络静态 TX 缓冲配置，显示热路径指针表和 dirty_lines 明确放内部。
Wi-Fi 启动及 USB 独占存储时停止 BLE 的生命周期策略已有 host 测试；不能用此替代实际吞吐与重连测试。
阅读字体、排版及灰阶波形未因本轮调整而修改。
OTA 原大小上限仍为 6,029,312 B，bt5 超限，仅供串口体验，未提交、未推送、未发布。
本机记录在 `output/device-tests/plus-page-turner-bt5-20261101/`。
