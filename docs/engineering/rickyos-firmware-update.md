# RickyOS 设备在线更新

本契约适用于 RickyOS 产品的 Read Pico 和普通版 Metalio E-Ink 4。上游其他机型的
CrossMux Stable / Nightly 逻辑不变。设备设置入口为「RickyOS 更新」，只使用
自己的正式频道，不显示 Nightly 开关，也不读取旧设置中的 Nightly 标记。

## 单一发行来源

设备读取 <https://chinoryunqin.github.io/RickyOS-site/ota.json>。
该文件由 `tools/rickyos-flasher/scripts/build-ota.js` 在网站构建时生成，
唯一输入是与网页安装共用的 `public/releases.json`，不另维护版本列表。
更换托管地址需要同时修改 `src/network/RickyOtaPolicy.h` 与审核 TLS 信任根。

当前没有正式固件，目录为空，输出为：

```json
{
  "schema": 1,
  "product": "RickyOS",
  "board": "readpico",
  "channel": "stable",
  "status": "no_update"
}
```

它会显示「暂无可用更新」，不是检查失败。网络失败、无效目录、证书或时间
错误则停止并提示失败；不得把这些情况当作无更新，也不得回退 CrossMux。
模拟器只显示无更新，不伪造可安装的上游版本。

正式发布需明确授权、完成实机验收，并通过网站发行校验。非空目录派生出
`status: update_available` 和 `version/file/bytes/sha256/approved/hardwareAccepted/chipId/flashBytes`。
网页与设备使用同一二进制、版本、长度和 SHA-256。`approved` 与
`hardwareAccepted` 必须都为真，不能以开发候选充当正式版本。

版本格式为 RickyOS 自己的 `major.minor.patch`（从 1.1.0 起，platformio.ini `[rickyos] version`），
按三段无符号整数比较，拒绝降级或相同正式版本；同编号的已运行 `-dev` 版可以升级正式版。
发布后 main 改为下一个修复号加 `-dev`（如 `1.1.1-dev`）。不接受未知版本格式或其他品牌的版本号。
1.0.0 之前的 `1.6.5-rickyos-pico.12` 只认旧格式，收不到 1.1.0 的在线更新，需要用网站更新一次。

## 下载与写入边界

- 请求必须从固定 HTTPS 来源开始。仅允许 `firmware/名称.bin` 相对路径；
  不接受目录中的任意 URL。每次 TLS 连接验证 CA 与域名，跨源或降级跳转在
  新 socket 连接前停止；没有 insecure 回退。
- 设备需要有效 UTC 时间；缺失时先同步时间，再开始 TLS。时间或证书校验
  失败不能通过关闭校验来解决。
- 对最多 2 KiB 的平面 JSON 做严格流式解析：类型、字段、重复键、结束符和
  长度都受限。解析器栈空间不超过 512 字节，不缓存整个目录或镜像。
- 只写另一应用槽，下载前检查槽位不同于运行槽、镜像对齐和至少 512 KiB
  保留空间。标准 0x640000 槽的最大镜像为 6,029,312 字节。
- 流式校验长度、SHA-256、ESP32-S3 头、RickyOS 品牌、完整版本字符串及
  必须存在的 Read Pico board tag。任何其他机型 tag 都会拒绝。
  最终校验及 `esp_ota_end` 成功后，才切换启动槽。
- 失败时中止未完成的应用写入，不选择新启动槽。不改写 bootloader、分区表、
  NVS、其他数据分区或 SD 卡；切换启动槽会正常更新 OTA 启动元数据。

已有手动 SD 更新入口不在本次改动范围。上述摘要校验不是独立的固件签名：
发行可信度仍依赖 HTTPS、受控的托管仓库、部署账户和审核流程。

## TLS 依赖与资源

复用 SDK 的 wolfSSL，不引入第二套 TLS。`RickyOtaTrust.h` 保存 flash 常量
ISRG Root X1 与其审阅指纹；需关注托管服务证书链变更。
`scripts/patch_rickyos_tls.py` 仅在 RickyOS 环境构建时为固定 SDK 添加显式
peer 验证、CA 加载失败拒绝和域名检查。此机械补丁可重复执行；SDK 源码不匹配
或补丁不完整时构建失败，不能静默漏掉验证。补丁由父仓库维护，不提交 SDK 指针变更。

SHA-256 增量状态与标识扫描器使用固定栈空间，期望摘要仅 32 字节。
证书自身驻留 flash，证书解析仍消耗 TLS 堆内存；成功编译不证明设备握手的
峰值内存或联网稳定性。原有联网前字体释放与清理仍保留。

## 可重复验证

主机测试编译实际 `OtaUpdater`、解析器和 `HttpDownloader`，仅替换网络与
Flash 边界；摘要使用主机 OpenSSL。可单独运行，不需要下载模拟器：

```sh
cmake -S test/rickyos_ota -B test/build-ota
cmake --build test/build-ota
ctest --test-dir test/build-ota --output-on-failure
python3 test/rickyos_ota/test_tls_patch.py
pio run -e rickyos_readpico
```

macOS Homebrew 可向 CMake 传 `-DOPENSSL_ROOT_DIR=/opt/homebrew/opt/openssl@3`。
网站另运行 `npm test`、`npm run build` 和 `node scripts/check-preview.js dist`；
预览发布检查仍强制网页与 OTA 目录都为空，不允许固件文件。

实机还必须验收有效/错误时间、正常/错误证书、低内存、断网恢复、两种运行槽、
错误摘要/机型/版本拒绝、重启后版本及阅读数据保持。主机测试和编译不能代替
真实 TLS 握手、Flash 写入与重启验收。测试构建不可自动发布或刷入设备。

## 普通版 Metalio E-Ink 4

普通版使用 `metalio_eink4` 机型标记和 ESP32-S3 芯片 ID 9。从同一网站和
COS 根目录读取 `metalio-eink4/ota.json` 和 `metalio-eink4/ota-notes.txt`，
固件文件仍相对于根目录的 `firmware/`。应用槽大小为 0x640000，沿用 512 KiB
预留和 4 KiB 对齐，单个 OTA 镜像最多 6,029,312 字节。普通版产品目标构建时也检查这个上限。

网站 `public/releases.json` 的可选 `metalioEink4` 字段使用一条已通过实机验收的
`app-upgrade` 记录。省略此字段时生成明确的 `no_update`。网站构建核对镜像长度、
摘要、版本和普通版机型标记，网页 USB 安装入口仍只处理原有 Pico 目录。
COS 发布脚本的 `--device metalio-eink4` 参数可验证普通版的发布计划。
测试镜像和正式目录分开审核，首次装入支持普通版 OTA 的固件用 USB 或已验收的 SD 更新路径。

首发真机验证应从一份带阅读进度、统计和设置的普通版开始，验证成功写入另一槽、
重启和再次更新，并验证中断下载时原槽可启动。现有主机测试同时验证 Pico
和普通版的解析、镜像完整性、另一槽写入、错误机型拒绝和失败时不切换启动槽。
