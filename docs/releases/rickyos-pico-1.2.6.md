# Read Pico 1.2.6 · 修复版

当前状态：已于 2026-10-23 正式发布。用户对同一冻结整包确认「可以了，发布吧」，串口安装/启动和用户功能验收通过；发布按先文件、后目录完成。历史候选标志保留原始状态，正式验收依据另存于本地 `/Users/rick/Documents/ChatGPT/rickyos/.local/pico-publish126/acceptance.json`。本轮未执行实际设备 OTA 流程，不将串口安装混称 OTA 验收。

## 发布范围

仅 Read Pico，基于已发布且验收的 1.2.5 / dafc0601，加三个提交：

- fd546f17：允许部分固定版式 MOBI/AZW3 漫画导入，补回提示弹窗正文。
- ba0e8186：已转换书籍使用抽样标记检查，避免每次重新读取整本原文件；旧缓存经一次完整校验后复用。
- 03b8fb26：按图片内容识别 PNG/JPEG/BMP，修复扩展名写错导致看图或壁纸设置失败。

独立关机壁纸、词典/补字资源迁移、EPUB 精排、书库/存储和此前 Pico 电量优化均已在 1.2.5 发布，不作为本次新增宣传。
不引入 Plus 的电量计参数、静态待机供电或显示波形。
发布准备同时将 UI/OTA 和 ESP 应用描述符版本统一为 `[rickyos] version`，避免后者自动取最近 Git 标签；这不是新增用户功能。

## 用户更新说明

1. 修复部分固定版式 MOBI/AZW3 漫画无法导入，并补全错误提示弹窗的文字。
2. 优化已转换 MOBI/AZW3 书籍的再次打开，减少重复读取；首次打开仍需转换。
3. 修复部分 JPG/PNG 图片后缀与实际格式不一致时，无法打开或设置壁纸的问题。

仅适用于未加密 MOBI/AZW3，不新增 DRM、KFX 支持，也不是首次导入提速承诺。

## 发布门槛

在独立工作区构建实际版本为 1.2.6、机型为 Read Pico 的镜像，不能复用旧工作区嵌入 eego 版本标识的历史产物。
主机回归和镜像大小/机型/版本检查通过后，仍需这份最终整包的真机启动和基本功能确认。
只更新 Pico 的官网、OTA、COS 发行记录；Plus/A4 的目录、固件及更新说明保持不变。
未完成整包验收前，不标记 approved/hardwareAccepted、不开放正式更新，不用历史单项验收代替整包验收。

## 2026-10-23 本地候选包

用户暂时不能连接 Pico，明确要求先准备发布包。本次未连接或写入 Plus，也未更改公开官网、OTA、COS。

- 工作区：`/Users/rick/Documents/ChatGPT/rickyos/.local/pico-release-126`。
- 分支：`codex/pico-release-1.2.6`；源码基线 `03b8fb266515f257a1773d56ebbdb90b882a8a70`，版本收尾及新增回归仍是本地未提交改动。
- 最终构建：`pio run -e rickyos_readpico` 成功；Pico 原有工具链，不使用 Plus 的 IDF。
- 应用描述符版本：`1.2.6`；芯片 ID 9；机型标记只包含 `readpico`。
- BIN：5,935,904 字节，按 Flash 扇区向上取整后仍满足应用槽保留 512 KiB 的限制。
- BIN SHA-256：`e87651589d43a466efbd46fe6f2d60fe36eb6740c8c7512ec2143e5ffa5fe1c4`。
- ELF SHA-256：`d241164f4f18e6ba59342685ee993c40f126cbc110b6dd185edf6703a7fcbaed`。
- 候选压缩包：`/Users/rick/Documents/ChatGPT/rickyos/output/firmware/RickyOS-ReadPico-1.2.6-Candidate.zip`。
- 压缩包 SHA-256：`3b4e3712345496b665121bb42994156e93ca9cefb9711a66ab29c54b8963a0b3`。

已通过：实际 Kindle 核心/导入器的 ASan/UBSan 主机测试（含固定版式、冷/热缓存、旧缓存采用、取消及生成 EPUB 完整性）；图片内容探测、弹窗正文几何（普通/高 DPI）、关机壁纸、资源服务、存储、电量稳定、阅读缓存与阅读 UI 回归。使用实际刷机工具 `checkImage` 重新验证冻结 BIN 的机型、版本；摘要和扇区对齐容量检查通过，ZIP 完整性和压缩内 BIN 摘要一致。

压缩包只含应用更新镜像和说明/摘要，不是从零地址写入的整机 Flash 镜像。`approved`、`hardwareAccepted`、`published` 均为 false。最终真机启动、MOBI/图片/弹窗功能检查仍待进行，不能据主机测试提前开放正式更新。

### 随后串口安装验证（同日）

用户随后连接 Pico 并授权继续：通过芯片 ESP32-S3、MAC `14:c1:9f:d1:9f:58` 识别同一测试机，核验 16 MiB Flash、完整分区表及 MD5、有效 OTA 记录、安全启动/加密关闭。将上述同摘要候选写入当前 app0 `0x10000`，设备 MD5 读回一致；启动加载器、分区表、NVS、OTA 启动记录与写前逐字节一致，未操作 SD 卡。已发出硬件复位。

日志确认 `Hardware detect: read_pico`、版本 `1.2.6`、SD 挂载、进入 `InxRecent` 首页；24 秒捕获内没有 panic 或重启循环。串口记录：`.local/pico126-flash.log`、`.local/device-test-20261023.json`、`.local/pico126-boot.log`。

串口安装不计为 OTA 验收；MOBI/AZW3、图片和弹窗的用户功能验收仍待确认，未修改冻结包及其接受标志，未上线正式目录。

## 正式发布记录

上述候选和串口阶段的「待确认/未上线」状态已由本节的最终用户验收与发布覆盖；没有重新编译或替换已验收 BIN。

- 发行记录 `approved=true`、`hardwareAccepted=true`；镜像 SHA-256 仍为 `e87651589d43a466efbd46fe6f2d60fe36eb6740c8c7512ec2143e5ffa5fe1c4`，5,935,904 字节。
- 官网 `chinoryunqin/RickyOS-web:gh-pages`：文件提交 `8d0578e`，目录/更新说明/版本展示提交 `db23f36`。
- GitHub OTA `chinoryunqin/RickyOS-site:gh-pages`：文件提交 `9c682c0`，更新说明/目录提交 `e957801`。
- COS `ota/firmware/RickyOS-1.2.6.bin` 先上传并公开读回核验，再更新 `ota/ota-notes.txt`、最后更新 `ota/ota.json`。
- 官方安装入口：<https://rickyaistudio.com/install.html>。固件直链：<https://rickyaistudio.com/firmware/RickyOS-1.2.6.bin>。
- GitHub 设备更新目录：<https://chinoryunqin.github.io/RickyOS-site/ota.json>；COS 镜像目录与其内容一致。
- 网站发行校验、81 项网站测试和完整构建通过。独立验证工作区曾因缺少仓库外引用的原始 logo / LICENSE 而构建失败；补齐相同素材和许可原文后完整构建通过，没有改网站外观或安装脚本。
- 23 项公网核对全部通过：三处新 BIN 的长度/摘要、三处 Pico OTA 目录和说明、官网发行目录/版本说明，以及 Plus/A4 的原有 OTA 目录/说明。
- Plus/A4 发行条目、OTA 文件、安装组件保持逐字节一致；旧 Pico 1.2.5 文件保留，没有清理历史发行包。
- 官网仍使用最新已上线页面及安装脚本，仅更新 Pico 发行数据/说明和 Pico 版本展示，保留此前移除 CrossMux 安装提示及 Plus 屏幕参数修复。
- 部分主机 Python HTTPS 探测遇到连接重置，使用保持 TLS 校验的 curl 读回并同样校验长度/摘要；未使用 insecure、未跳过发行门槛。

本地完整发布凭据和公网校验在 `/Users/rick/Documents/ChatGPT/rickyos/.local/pico-publish126/`；原始电子书、BIN、串口日志和凭据不提交源码仓库。源码仅推送私有 `RickyOS-dev` 的 `codex/pico-release-1.2.6`，不改 Plus 开发分支或公开固件源码。
