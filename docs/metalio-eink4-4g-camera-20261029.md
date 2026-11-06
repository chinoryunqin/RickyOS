# 普通版 E-Ink 4：4G 双卡与 USB 摄像头接入

本轮目标：在已经实机验证阅读、输入、待机的 `1.2.11-metalio-dev3` 上增加 NT26 蜂窝联网/双卡选择和 USB UVC 静拍。4G 已完成实机数据验证；相机和双卡切换仍在验证，不能视为全部完成。

## 当前恢复验收（2026-10-29，dev11）

用户确认机器在首页并要求恢复。首先重新读取到dev10应用启动日志，随后核验ROM MAC `10:20:BA:6D:6C:CC`、分区表及OTA选择后写入dev11，Flash摘要通过，分区/OTA选择不变。dev11应用SHA256 `de075841a15ae04394f7159a79bc7c9ec01d39e104e13c4fb339ae87525b1a9e`。

刷后串口最初仍未回应。通过libusb只对匹配VID/PID且USB序列号完全一致的目标执行USB总线复位（返回0），没有写Flash或操作其他USB设备；之后连续两次串口会话都能读取dev11启动及命令回应。这表明本次恢复包含电脑USB连接状态的重新枚举，不能把恢复全部归功于固件修改。

| 恢复检查 | 结果 | 证据 |
|---|---|---|
| 应用启动、SD挂载、显示后I2C输入检查 | 通过 | `post-usb-reset-dev11.log` |
| 首页实际帧 | 通过 | `home-dev11.png`，480×800，文字/封面/导航正常 |
| 串口命令连续响应 | 通过 | `post-usb-reset-dev11.log`、`restored-dev11.log` |
| 中国移动内置卡 | 通过 | Connected、slot1/SIM2、CSQ20、192.168.10.2 |
| DNS/TCP/HTTP | 通过 | `Data probe tcp=1 http=HTTP/1.1 200 OK` |
| NTP/RTC校时 | 通过 | NTP reply与系统/RTC同步日志 |
| 相机取景/保存 | 待验证 | 本轮按恢复要求未运行相机；此前仍未得到有效图像 |
| 外置卡及双卡切换 | 待验证 | 当前只有内置卡 |

设备留在首页，本轮未触发相机或改变APN。此前USB失联、候选未刷等记录是历史阶段，当前状态以本节及dev11 manifest为准。

## 硬件与来源

- 普通版目标 ESP32-S3，USB/ROM MAC `10:20:BA:6D:6C:CC`。用户本轮已重新连接，目前只有内置 SIM；官方固件可使用该卡。
- 工程源：`CloudZao/Metalio-E-INK4`，固定树 `d12f481f4a42420634bb56a928f3959a977367d4`。下载参考位于主工作区 `artifacts/metalio-4g-camera-20261029/vendor`。
- NT26：UART1 TX12 / RX11，MRDY21（低=忙，高=空闲）/ SRDY5，2 Mbaud，厂商四字节封包协议，不是裸 AT 串口。参考 `main/boards/common/board/uart_eth_modem.{cc,h}`，Apache-2.0。
- SIM：NT26 `AT+ECSIMCFG="SimSlot",0/1`，通过封包 AT 通道发出。当前卡、切换/重启和重新注册必须实测；不发短信/拨号，不修改 APN。
- UVC：GPIO19/20 经 FSUSB42UMX 复用；TCA9555 P0.0 LOW=相机，HIGH=电脑调试/MSC。与 USB 传书互斥。摄像头 VID/PID 和分辨率待枚举。
- 相机电源：CX25601N 0x6B，厂商 `cx25601n_enable_otg` 设置 5040 mV / 1000 mA，先关充电，再配置输出，最后开 EN_OTG（0x18 bit6），等待至少30ms。必须保留/恢复原寄存器状态。
- 当前 Arduino 3.3.11 / IDF5.5.5 预编译库已包含 USB Host/UVC，无需更换核心。
- 新依赖固定版本：78/uart-uhci 0.2.1（归档 SHA256 263e37a3214ac5ad0873f0a4acd039cc071d0fa56e6c4379a3f9b3a1f7f64464）；espressif/iot_eth 0.1.0（541c9a440975d01be2250e98721ebebb5e89162d1a90c3db14fa387bdf4ce927）。原件在 artifacts 的 dependencies 下。

## 当前实现状态

SDK 新增 MetalioModem、UartUhci、IotEth 源码与来源散列。对厂商 modem 修正构造分配失败 abort、部分任务创建失败时提前释放资源、Stop 未等待所有任务便释放资源、SRDY ISR 队列释放顺序。HAL 增加 HalCellular（异步控制、配置保持、状态快照）和 HalNetwork（Wi-Fi/4G 通用联网判断）。保留官方 APN，默认关闭，用户启用后记住选择；普通 Wi-Fi/局域网服务保留原路径。

已实现 UVC/供电/MUX 会话封装、4G/相机应用入口、互联网应用连接条件、串口诊断；真实内置卡/相机验证进行中。相机退出将用受控重启回收 USB Host/PHY 并恢复电脑通路，避免厂商注明的 hub 卸载 abort；相机流/任务持有期间不能强制释放对象或电源。

## 资源与恢复

- 已验收 dev3 固件和源码在 `output/firmware/metalio-eink4-1.2.11-dev3` 和 `output/source/metalio-eink4-1.2.11-dev3-source.tar.gz`。
- 原 Pudding 16MB 完整备份 SHA256 `e646a9633c3d75c80960527aa8e5e9296f5f109f2472a85a9da181d96af46524`，已 ROM 摘要核验，保存于 `.local/device-backups/metalio-eink4-20261029/pre-port-full.bin`。
- Modem DMA 为 4×1600字节，协议重组1600字节；三个 SDK worker 栈4096+4096+3072字节。HAL 控制8KiB栈放PSRAM，仅按操作创建。
- TX最多32个完整帧（每帧不超过1600+4字节），需要入队长度检查。UVC DMA/内部堆与图像PSRAM预算须在真实启动前后记录；不允许图像分配回退到内部RAM。

## 构建与检查

- `pio run -e rickyos_metalio_eink4`：通过。版本 `1.2.11-metalio-dev4`，应用 6,153,184 字节，6.25MiB 分区余量 400,416 字节。静态 RAM 124,316/327,680 字节。
- 应用 SHA256 `3e4cca229edb52c68b3d61a9b4b1606d15d6365d9a798a78db0fe8ab62de6143`。
- `python3 test/metalio_peripherals/run.py`：7 组通过，实际生产封包头/相机供电代码，覆盖独立协议向量、校验进位、破损、序号回绕、OTG 设置/精确恢复、部分失败回滚、读失败不写入、已有 OTG 拒绝、重启恢复和恢复失败后继续保留快照。
- 33 项现有阅读相关主机测试通过；SSD1677 驱动、路由与输入后端测试通过。
- 新增 HAL/应用文件 clang-format21 检查通过，`git diff --check` 通过。
- `./bin/ci-check` 未通过：本移植工作区的历史 SDK 子模块被标记未初始化，脚本在第一道检查退出；不能声称跨平台完整 CI 通过。没有为了跑 CI 重置现有 SDK 适配。

## 用户操作与诊断

应用页新增“4G 网络”“相机”。4G 可连接、断开、选择 SIM1/SIM2、刷新状态。插槽数字保留模块索引+1；内置/外置对应关系、第二张卡联网待实机确认。相机拍照写入 `/images/camera/CAM_*.jpg`，能预览并进入图库；退出后受控重启回首页恢复电脑 USB。没有视频、实时取景、短信、电话和相片上传。

串口命令：`CELL_START / CELL_STOP / CELL_STATUS / CELL_REFRESH / CELL_SIM1 / CELL_SIM2 / CELL_TEST`；`CAMERA_TEST / CAMERA_STATUS`；`CELL_PAGE / CAMERA_PAGE / HOME / SCREENSHOT`，均带 `CMD:` 前缀。`CAMERA_TEST` 保存结果到 NVS 后自动重启，即使 USB 临时断开也能在恢复后查询。

## 实机记录与剩余事项

| 检查 | 固件/目标 | 状态 | 证据 |
|---|---|---|---|
| ROM身份、应用写入摘要、分区表/OTA不变 | dev4 / S3 普通版 | 通过 | `install-dev4.log`、`dev4-installation.json` |
| 启动、SD、显示后的I2C探测 | dev4 / S3 普通版 | 通过 | `firstboot-dev4.log` |
| 内置卡注册/信号/IP | dev4 | 通过 | CHINA MOBILE，CSQ22（约-69dBm），192.168.10.2 |
| DNS/TCP/HTTP | dev4 | 通过 | `cellular-scenario.log`：`Data probe tcp=1 http=HTTP/1.1 200 OK` |
| 蜂窝网络校时 | dev4 | 通过 | `SNTP sync started`、NTP reply、RTC和系统UTC同步 |
| 4G应用界面截图 | dev4 | 通过 | `cellular-screen.png`，状态/信号/运营商/IP可见 |
| 当前SIM插槽读取 | dev4 | 失败 | slot=255；原查询形式无有效响应 |
| 相机静拍和落盘 | dev4 | 待验证 | 启动后USB断开，未能重新读取NVS结果；不能确认成像/保存成功 |
| 相机退出恢复电脑USB | dev4 | 失败 | 观察期内USB未重新枚举；已请求断电重启恢复物理连接 |
| Read Pico回归构建 | 当前源 | 失败 | `Imu.cpp:263` 引用旧 SensorsConfig 中不存在的 imuIntOpenDrainLow；该文件及 BoardConfig.h 与 dev3 源快照逐字节相同，为既有移植BSP不匹配 |
| 第二张SIM上网 | 当前硬件 | 待验证 | 用户当前只有内置卡，尚无第二卡 |

运行内置卡时，内部堆空闲115,840字节、最低109,832字节、最大连续块69,620字节；PSRAM空闲7,790,428字节。来自实际日志，不能把这些值等同于相机工作中的DMA余量。

### dev5 修正（构建/刷机状态见 manifest）

- `HalCellular.cpp` 改用模块配置读取命令 `AT+ECSIMCFG?`，在返回的 `SimSlot` 行读取0/1。查询形式参照[合宙官方EC618 AT文档](https://docs.openluat.com/air780eeu/at/app/Command_List/Device_control/ECSIMCFG/)，具体NT26固件仍需实机确认。设置命令继续采用厂商NT26源码的 `AT+ECSIMCFG="SimSlot",N`。
- 相机使用标记保存在RTC内存，启动恢复充电器寄存器后，在没有Host任务的重启阶段复位OTG并恢复Serial/JTAG PHY，然后才让BSP将物理MUX接回电脑。依据已有 `src/platform/UsbSerialJtagHandoff.cpp:44-51`；不会在Host工作时强行删除其PHY。
- 所有分辨率开流尝试共享10秒预算，限制此前逐项8秒叠加带来的长时间等待。保留相机阶段/返回值/内部堆和最大块的RTC诊断，USB恢复后可查。
- NT26停止时清空已释放的iot_eth mediator，防止下一次SIM切换/重新连接在netif重新建立前向旧对象投递RX；每次启动清空上张卡的运营商/IMSI/ICCID缓存。
- Read Pico构建切换框架时遇到磁盘不足，已从官方缓存包原位恢复 Arduino3.3.11/IDF5.5.5 全部框架文件（没有删除用户资料）。

当前下一步：目标USB/ROM身份重新可见后，读取dev4相机结果，校验并刷入dev5；验证SIM插槽读取及相机成像、保存、预览与USB恢复。需要第二张卡后，再完成双卡均能注册/数据联网的验收。

### dev4 崩溃定位与 dev6 取景实现

用户确认相机测试崩溃且屏幕没有取景。重新接回后通过USB/ROM MAC复核，读取coredump分区0xff0000/64KiB保存为 `camera-dev4-coredump.bin`，有效长度0x7824。转储包含 `rick_camera` 任务与断言 `spi_flash_disable_interrupts_caches_and_other_cpu cache_utils.c:127 (esp_task_stack_is_sane_cache_disabled())`，栈内有rickcam NVS namespace。dev4的相机工作任务使用PSRAM栈，完成拍照后在该任务内创建/写NVS，正好触发禁用cache时任务栈必须位于内部RAM的断言。这个证据比仅观察USB未重连更直接；不能把dev4失败只归因于PHY。

- dev6相机worker只执行USB/图像/SD任务，不写NVS。结果通过静态快照和原子标记交给主循环 `HalCamera::pump()` 提交，再允许测试重启；4G切卡worker同样改为主循环提交设置。
- 修复相机worker直接vTaskDelete跳过C++析构的问题：电源锁和临时对象均先退出作用域，再删除任务。
- 新增真正的取景Activity，进入后自动请求预览、JPEGDEC转灰度并有序抖动成1bit屏幕图。每帧完成后至少间隔3秒刷新；可暂停、保存画面、查看图库。显示坐标使用renderer逻辑坐标绘制，避免drawImage当前尚不旋转位图的限制。
- 取景JPEG最多512KiB PSRAM，保留一个；屏幕位图按视口动态分配PSRAM，JPEGDEC对象用checked heap，不占任务栈。预览不写SD或NVS，仅用户拍照才落盘/保存最近照片。USB传书在相机持有Host期间被HAL拒绝，避免图库内入口抢占PHY。
- 增加取景缩放/黑白极性/源裁剪/缓冲区哨兵测试，底层测试共8组通过。
- 诊断 `CAMERA_VIEW_TEST` 运行真实取景界面，解码3帧，保存屏幕BMP与一张JPEG后退出恢复USB。`CAMERA_VIEW_DUMP`仅读该测试BMP，限制100KiB；不上传图片。

### dev6 / dev7 USB枚举修正

- dev6实测：不再触发PSRAM栈Flash断言；相机失败可正常退出、复位并重新枚举电脑USB。相机返回263 (`ESP_ERR_TIMEOUT`)，阶段3（USB/UVC枚举），未取得图像，不能称取景/拍照完成。
- 发现当前Arduino预编译IDF5.5.5配置 `CONFIG_USB_HOST_CONTROL_TRANSFER_MAX_SIZE=256`，厂商sdkconfig为4096；Espressif `components/usb/enum.c:554-560`明确拒绝超过该长度的配置描述符。
- dev7仅将匹配IDF5.5.5的enum.c及私有声明作为本板SDK依赖编译，枚举控制缓冲区改为4096，使用 `--undefined=enum_install` 保证替代预编译枚举器，其他USB Host/PHY实现仍用原库。源码/Apache许可/输入散列在SDK `MetalioCamera/src/host`。增加实际描述符长度RTC记录，待实机验证。
- 同时修复NT26多行响应被丢弃的问题：等待最终OK/ERROR时保留前面各UART帧，最大4096字节；终结行完整匹配，防运营商等文本中的OK误判。增加分帧、未完OK和错误终结行测试，底层共9组通过。

### dev8 诊断结果

枚举仍超时，配置描述符记录为0，说明目前没有证据证明256字节限制是本次未枚举的原因。4096配置保留为厂商匹配值，但不能称它解决了相机问题。读取充电芯片0x15/16/18/1e/38：开始前5c,a1,04,01,02；临时供电后1c,80,44,01,02，说明EN_OTG与HIZ/CHG/自动识别/看门狗位已按目标写入，不能据此推断相机VBUS实际电压或数据线电平。退出恢复USB通过、未再出现NVS/PSRAM栈断言。

NT26多行回复修复后，实际 `AT+ECSIMCFG?` 返回 `SimSlot,1`，内置卡对应模块slot1/界面SIM2；先前未知slot问题已解决。外置SIM卡/切换后重新注册仍待验收。

下一诊断版保存USB主机HPRT、全局中断、PHY选择和TCA9555输出读回，以及8秒后的充电芯片状态，帮助区分未连接与枚举协议失败。增加 `CAMERA_DELAY_TEST`，仅用于用户配合拔线后的纯电池比较；20秒后运行取景测试，不在每次启动自动拍照。

## 当前交付状态（dev9编译完成时）

- 设备最近实际刷入dev8，应用6,160,368字节，SHA256 `982cbf4c66f36a18f9cf46d1b26390b919ed6efbbf6de447e38ab7643f730290`；应用校验通过，分区表和OTA选择不变。
- dev9编译通过，产物在 `output/firmware/metalio-eink4-1.2.11-dev9`，但ROM/USB身份检查发现目标未连接，安装器在任何Flash变更前退出，不能声称dev9已上机。
- 当前可确认：内置SIM4G联网、DNS/TCP/HTTP200、NTP校时、SIM插槽读取。相机NVS写入崩溃已定位并修复，dev6/7/8测试退出能恢复USB。
- 当前未完成：真实摄像头枚举、屏幕取景、JPEG保存/预览、纯电池对比和第二SIM切换后联网。取景界面和底层服务已有实现，但没有成像证据，不可标记相机可用。
- 下一步：目标USB恢复后刷入dev9；先检查实际HPRT连接位、PHY选路、MUX输出读回与延时电源状态，再按证据修正。`CAMERA_DELAY_TEST`可用于配合用户拔USB后的纯电池测试。
- 磁盘不足处理：仅清理本工作树的生成SCons `.cache` 和本轮Read Pico失败构建生成的依赖缓存，释放5GB以上；未清理其他工作区或用户资料。

### dev9 / dev10 恢复顺序

dev9已在目标MAC和分区表核验后实际刷入，6,160,832字节；取景测试后只收到ROM重启消息，电脑仍枚举USB但应用串口不回应，自动ROM握手也失败。已请求用户完整断电重启，不能用USB设备列表存在代替通信恢复验收。用户确认尚未测试过官方相机，因此当前没有相机硬件工作正常的独立证据。

发现应用setup在main.cpp:731先Serial.begin，随后gpio.begin中的相机恢复钩子可能再次复位OTG/PHY。dev10将本板Serial.begin延后到gpio.begin之后，避免USB恢复后留有旧FIFO/ISR初始化状态；其他板不改。诊断快照使用NVS私有格式版本1保存（严格版本+sizeof匹配），只在主循环内部栈提交，支持断电恢复后查询。该修正仍需实际上机验收，不能单凭顺序推断认定故障已解决。

## 最新状态：dev10候选已备妥，等待硬件恢复通信

- dev10最终构建通过（`build-dev10-final.log`），应用6,161,472字节，分区余量392,128字节；SHA256 `6c9ffbce3afd79c28ba6cdc0435fb317f912ceb1edfca2ec33c55f836dd6c2b5`。固件、完整安装镜像、压缩调试符号和manifest在 `output/firmware/metalio-eink4-1.2.11-dev10`。
- dev10安装尝试在ROM握手阶段失败（No serial data received），未执行Flash写入；设备仍是dev9。不能把电脑USB枚举存在视为目标可刷。
- 需要用户拔USB、完整断电（长按电源约15秒，松开等10秒）后接回开机；若仍不能通信再使用BOOT+RESET下载模式。此时不继续反复自动复位，也不向未核验的设备写入。
- dev10还对Host寄存器诊断增加时钟开启检查，Host安装失败已关时钟时记录0xffffffff哨兵而不访问OTG寄存器。
- 4G已验收范围：内置中国移动卡、IP/DNS/TCP/HTTP200、NTP校时、当前slot读取。未验收范围：外置卡实际联网和切换后注册。
- 相机仍未验收：dev4的PSRAM栈NVS断言已经定位修复；dev6-8枚举超时能退出并恢复USB；dev9出现仅USB枚举但不通信，恢复顺序已在dev10修正待上机。始终没有有效图像，不能说取景、拍照或照片保存成功。
- 当前全代码CI仍受历史子模块阻挡；Read Pico回归构建受原有SDK SensorsConfig/Imu不匹配阻挡。9组外设底层/光栅/AT测试通过、33项阅读主机测试通过，不能据此替代硬件验收。

### dev10上机及dev11候选

用户完成断电重新连接后，dev10已核验目标ROM MAC并写入app0，摘要通过，表/OTA选择不变。重启只收到ROM启动片段，后续应用串口无回应；usb-reset自动下载握手也无回应。已询问用户屏幕是否仍可操作，当前不能声称dev10恢复成功。

重新检查发现main首行 `BoardConfig::holdPowerRails()` 内部已调用BSP begin，因此dev10延后Serial.begin没有改变真正的BSP恢复顺序。dev11改为HAL在holdPowerRails前强制USB PHY启动路由为Serial/JTAG（已有官方LL接口），不再依赖RTC route标记猜测当前路由，也不在BSP恢复阶段复位/关闭OTG模块时钟。BSP恢复钩子只恢复相机充电器寄存器；串口初始化回到原来的位置。这是对恢复策略的修正，仍须构建和上机验证。

## 最新状态：dev10已写入，dev11为待刷候选

- 用户完成断电重连后，dev10已实际写入，Flash摘要通过。不能沿用上一条“dev10未刷入”的历史状态。
- dev10启动只收到ROM消息，应用串口无响应；一次原生USB专用复位时序加同句柄持续监听也未获得应用日志。现有USB设备枚举记录仍能看到普通版MAC，但自动ROM握手无回复。已询问用户屏幕是否在首页并能触控，等待该观测后决定恢复方式；暂不反复要求同样断电操作。
- 额外尝试读取旧coredump时，因目标仍在stub而ROM对象发送SPI_ATTACH，被协议拒绝（C000 Bad data length），没有修改Flash或得到新coredump；不能将其当作新的崩溃定位证据。
- dev11构建通过（`build-dev11.log`），应用6,161,280字节，余量392,320字节，SHA256 `de075841a15ae04394f7159a79bc7c9ec01d39e104e13c4fb339ae87525b1a9e`。已打包，未刷入/未硬件验收。9组底层测试、格式检查、diff检查通过。
- 4G数据验收来自先前dev4/8等记录，不能以此宣称当前dev10启动/4G已经确认。当前最高优先级是恢复目标调试/下载通道，再验证取景。

## 1.2.17 dev19 相机实机诊断

- 普通版相机候选已在已核验的 ESP32-S3 上写入 app0 并通过摘要校验。分区表、OTA 选择和刷写时的 NVS 内容均通过写前写后比对。首次启动挂载 SD 并回到首页。
- 相机先注册 UVC 客户端再打开主机根端口，使用与厂商配置相同的 RX 优先 FIFO。诊断保留最后一次 USB 枚举阶段和失败阶段。相机确认充电器 OTG 已启用后才切换 USB，关闭供电失败时保留充电器恢复快照。10 组底层及光栅测试通过。随后工作树增加了快门的屏幕刷新代次检查，保存时核对 USB 帧与上一轮完成刷新的画面。
- 这轮有线复现中，板载 I²C 在尝试相机供电前已故障，`SDA=1 SCL=0`，候选停在阶段 1。关闭 4G 并重新启动仍能复现，测试后已恢复原 4G 开关。更早的一次阶段 3 记录显示 USB 主机已检测到连接，配置描述符仍为 0。这两条记录来自不同测试。
- 用户反馈纯电池测试报错或等待，后续还反馈开机未到首页或触控失灵。相机整体使用验收仍未通过。
- 纯电池测试到达阶段 3。USB 主机检测到相机连接，随后报 `Invalid FIFO config` 和 `Root port reset failed`。枚举器未启动，描述符长度为 0。原候选按标称容量分配 208/16/32 字，超过该设备报告给主机驱动的可用容量。修正版读取 USB 控制器的容量寄存器，给两类发送 FIFO 保留同样的 16 和 32 字，将余量分给接收 FIFO。
- FIFO 修正版在有线测试通过端口复位、设备寻址和完整设备描述符读取，随后停在 `SELECT_CONFIG`。Arduino 的 ESP-IDF 构建启用了 `CONFIG_USB_HOST_ENABLE_ENUM_FILTER_CALLBACK`，厂商构建关闭该选项。此选项启用时，空回调会取消枚举。相机驱动显式允许 MUX 后的内置设备使用默认配置。
- 200 kHz 实机对照两次正常启动并挂载 SD，记录期间未出现此前的 SCL 拉低。单张拍摄返回 `error=0`、`320x240`、`2150` 字节，保存到 `/images/camera/CAM_1793906482_00.jpg`，配置描述符 361 字节，枚举完成阶段 33，之后电脑重新识别 USB。照片像素内容尚未独立读取检查。
- 自动连续取景取得第一帧后，第二次相机重新枚举超时。工作树将预览改为保留 USB 会话，退出相机或准备休眠时关闭并恢复供电。普通版板级、电量计和 RTC 的共享总线配置统一到 200 kHz。此会话复用版本尚未上机验收。
- 用户最后反馈设备严重卡顿、相机打不开，要求推送后换电脑继续。当前设备仍为单张拍摄验证过的版本。接续步骤和已刷固件摘要见 `metalio-eink4-handoff-20261106.md`。
