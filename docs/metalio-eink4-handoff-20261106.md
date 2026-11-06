# 普通版 E-Ink 4 接续记录

本分支包含 Plus 1.2.16、1.2.17 的应用同步，以及普通版相机驱动调试。
用户最新反馈为严重卡顿、相机打不开，要求换电脑继续。整体适配尚未验收。

## 开发环境

- 分支 `codex/metalio-eink4-1.2.11`，构建版本 `1.2.17-metalio-dev19`。
- 构建目标 `rickyos_metalio_eink4`，ESP32-S3，普通版 SSD1677 480×800。
- SDK 固定在 `96de1be6ce08eb732909e6e8149af8f892b9a2c5`，改动通过仓库内补丁和摘要清单恢复。
- 本机未提交的 SDK 工作树由补丁完整覆盖。换电脑需要先恢复该补丁，再构建。
- 字体压缩和元数据由构建脚本生成，输入字体、所需 CFF 源码及许可证随分支保存。

```sh
git submodule update --init freeink-sdk
python3 scripts/prepare_metalio_sdk.py
python3 scripts/prepare_metalio_sdk.py --check
pio run -e rickyos_metalio_eink4
python3 test/metalio_peripherals/run.py
```

## 设备现状

目标已通过 ROM 检查为 ESP32-S3，MAC `10:20:BA:6D:6C:CC`。
本机串口为 `/dev/cu.usbmodem101`，换电脑需重新发现。
最近刷入 app0 `0x10000`，应用大小 6,405,840 B，SHA-256 为
`bbfdae3549d11b4b327ca3395609e10bedf18e6ef1c782bb482eddc77e5c93e3`。
该版本已验证写入摘要，刷写前后分区表、OTA 选择和 NVS 一致。
它包含动态 USB FIFO、枚举过滤回调和 200 kHz I²C，尚未包含最新会话复用改动。

设备原 app 槽为 6,553,600 B，构建后须再次检查余量。
本机的镜像、设备备份和原始串口日志位于工作区根目录
`artifacts/metalio-camera-dev19/`，这些本地产物不随源码推送。

## 相机证据

- 400 kHz 时曾在相机供电前出现 `SDA=1 SCL=0`，随后 SD 或触控失败，关闭 4G 也能复现。
- 纯电池测试读回 `Invalid FIFO config` 和 `Root port reset failed`。
  当前驱动按 USB 控制器实际报告的容量分配 FIFO。
- Arduino SDK 开启了枚举过滤，厂商构建关闭。当前驱动显式提供允许内置设备的回调。
- 200 kHz 对照两次正常启动并挂载 SD，记录期间未再出现 SCL 拉低。
- 单次拍照诊断返回 `state=2 error=0 bytes=2150 size=320x240`，
  文件 `/images/camera/CAM_1793906482_00.jpg`。
  `descriptor=361`，`enum_stage=33 enum_failed=0`，退出后 USB 恢复。
  照片像素内容尚未独立读取检查。
- 连续取景第一帧后，下一次重新枚举超时，旧实现每帧关闭相机供电并卸载 UVC。
  当前源码让预览保持会话，退出或准备休眠时关闭，需重新上机验证。

单拍诊断摘录来自 `enum-200khz-still-check.log`。

```text
CAMERA_STATUS state=2 error=0 bytes=2150 size=320x240 file=/images/camera/CAM_1793906482_00.jpg
CAMERA_DIAG phase=6 descriptor=361 internal=89760/79080 largest=45044/32756
CAMERA_USB_ERROR  enum_stage=33 enum_failed=0
CAMERA_HOST hprt=00021405 gint=04000029 phy=00180000 usj=00000201 mux=0a60
```

## 继续验证

优先确认首页、SD 和触控的启动稳定性，再检查相机界面能否进入。
运行取景时设备内置相机占用 USB PHY 和 MUX，电脑串口会断开。
退出后通过受控重启恢复串口，随后发送 `CMD:CAMERA_STATUS` 读取保留诊断。
`CMD:CAMERA_VIEW_TEST` 会自动取得三帧、保存取景 BMP 和 JPEG 后退出。
`CMD:CAMERA_TEST` 只拍一张并保存后重启。
`CMD:CAMERA_DELAY_TEST` 延迟 20 秒启动三帧测试，可配合拔线做电池对照。

要检查屏幕是否真正显示所拍画面、快门保存是否与画面一致、连续取景是否稳定，
以及重复进入退出、待机唤醒后的触控和 USB 恢复。
现有取景为约三秒一次的 320×240 起步配置，属于待调试的基础取景。
用户报告的严重卡顿仍须复现和定位。

最新会话源码首次构建在链接阶段因磁盘空间不足失败。
清理本工作树的两个 SCons 缓存目录后，重试构建成功。
交接的源码通过 `rickyos_metalio_eink4` 构建、SDK 补丁摘要检查和差异空白检查，
尚未刷入当前设备。
10 组外设主机测试验证供电回滚、恢复快照、取景光栅和 AT 分帧，无法替代真机使用验收。
