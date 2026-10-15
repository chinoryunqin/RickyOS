# RickyOS 1.2.0 发布物料与检查清单（内部）

只放在私有仓库。公开说明见 `rickyos-1.2.0.md`。

决定（用户，2026-10-14）：版本号 1.2.0；只走网站 + OTA，不在公开 GitHub 仓库打 tag、
不建 Release、不推源码；微信读书偶发卡死不阻塞发布。

## 1. 设备 OTA 说明（`ota-notes.txt` / `releases.json` 的 `notes`）

规则：最多 8 行，每行不超过 96 字节，只用常用字（已对照 `cn_common_chars.txt`）。

```
1.2.0
全新灰度界面，首页、书库和设置重新设计
新增阅读模式：小狗伴读、沉浸、古籍竖排
新增阅读线、阅读统计和每月阅读报告
字号最大 32 号，字重可调，TTF 字体也能加速
新增 USB 传书，连上电脑就能拷书
首页、书库和首次打开大书更快，封面改为灰度
字体和图片下载改走国内线路，更快更稳
移除 OPDS、SD 卡插件和 XTC 格式
```

字节数：57 / 57 / 51 / 59 / 44 / 63 / 54 / 40。

## 2. 群公告

```
RickyOS 1.2.0 发布了 🐶

这一版主要是换了新样子，也更快了：
· 全新灰度界面，首页、书库、设置都重新设计，封面改为灰度
· 阅读模式：原版、小狗伴读、沉浸、古籍竖排
· 阅读线（六种样式）、字重调节，字号最大 32 号
· 阅读统计：爪印连续阅读、月历、徽章、每月月报
· USB 传书：数据线连电脑，SD 卡当 U 盘直接拷书
· TTF / OTF 字体也能加速
· 首页、书库、大书第一次打开都更快
· 字体和图片下载改走国内线路

移除了 OPDS、SD 卡插件和 XTC 格式，用到的朋友请留意。
1.1.5 的 Markdown 排版和中间键待机修复都保留。

更新：设置 → 系统 → RickyOS 更新；或用网页安装工作台。
USB 传书在 Mac 上拷书时，尽量直接拖到磁盘根目录；电脑没反应时按返回键退出。
```

## 3. 发布前要做

- [x] 版本号：`platformio.ini` 的 `[rickyos] version` 改为 `1.2.0`，单独一个提交。
- [ ] 和另一位 agent 对齐冻结点：阅读模式 / 古籍竖排的改动是否都已合入、是否还有在途提交。
- [ ] 真机回归（正式构建，不带诊断宏），至少：
  - [ ] 从 1.1.5 OTA 升级到 1.2.0，设置、阅读进度、书库保留；
  - [ ] 网页完整安装（原厂系统 → 1.2.0）；
  - [ ] 首次开机整理 SD 卡（小狗提示）、首页封面、书库；
  - [ ] 打开大书、阅读模式四种、阅读线、字号/字重、TTF 加速；
  - [ ] Markdown 书（1.1.5 功能）、目录跳转；
  - [ ] 待机/唤醒、中间键开关、侧键；
  - [ ] USB 传书：拷书、弹出、返回键退出；
  - [ ] 微信读书书架、下载（已知偶发卡死，记录但不阻塞）；
  - [ ] 字体下载、图片下载（COS 线路）。
- [ ] 设备上的诊断版固件刷回正式版；清掉测试用的 `usb-test` 文件夹。
- [ ] 许可：`LICENSE.txt` 保留 CrossMux（MIT）、Inter（OFL）等第三方声明；
      修改过的 LGPL 组件（epdiy）按闭源计划在 `RickyOS-oss` 公开，发布前确认已就位。
- [ ] 发布说明里的镜像字节数和 SHA-256 在最终构建后填写。

## 4. 发布步骤（只走网站 + OTA）

按 `HANDOFF.md` 第 4 节，去掉 tag / GitHub Release 两步：

1. `pio run -e rickyos_readpico`（正式构建）。
2. 旧 bin 备份到 `~/rickyos-device-backups/releases/`，新 bin 放进
   `tools/rickyos-flasher/public/firmware`。
3. 更新 `releases.json`（版本、文件名、字节数、sha256、notes、各段文件名）和 `ota-notes.txt`。
4. `tools/rickyos-flasher` 下 `npm test`、`npm run build`。
5. 同步两个网站仓库（2026-10-15 起拆开，见下），提交推送，核对两处线上 `ota.json`：
   - 官网 `chinoryunqin/RickyOS-web`（`rickyaistudio.com`，本地 clone 目录仍叫
     `repository-publish/RickyOS-site`）：放完整 `dist/`。`gh-pages` 上的 `CNAME` 和
     `assets/install-entry-guard.js`（及 `install.html` 里对它的引用）不在网页源码里，
     `rsync --delete` 时必须排除或先补进源码，否则域名和安装页入口检查会被删。
   - 设备 OTA `chinoryunqin/RickyOS-site`（`chinoryunqin.github.io/RickyOS-site/`，**不能绑域名**，
     本地 clone 在 `repository-publish/RickyOS-ota-site`）：只放 `ota.json`、`ota-notes.txt`、
     `firmware/RickyOS-<版本>.bin`，删掉旧版 bin。所有已发固件都写死这个地址，而 OTA 下载
     拒绝跨域跳转；官网绑了域名后 github.io 会 301，所以设备更新必须由这个仓库提供。
   - 核对：github.io 的三个文件直接 200（无跳转），bin 的字节数和 sha256 与 `ota.json` 一致。
5b. OTA 同步到 COS（用户决定：以后固件更新都走 COS，2026-10-14）：1.2.0 起设备先查
   `<COS>/ota/ota.json`（GlobalSign R3 锁定），连不上再查网站；旧设备只查网站，所以两边都要传。
   `python3 scripts/publish_rickyos_assets_cos.py --kind ota --output tools/rickyos-flasher/dist --tag 1.2.0`
   （先 `--dry-run`；脚本核对镜像字节数和 sha256，按 镜像 → 说明 → ota.json 的顺序上传）。
6. 同一天发布下载内容（用户决定：等固件 1.2.0 一起发，2026-10-14）：
   - 字体 v1.1.0：新增 RickyBookSong（霞鹜新致宋，IPA）、ZhuqueFangsong（朱雀仿宋，OFL）、
     RickyRound（思源柔黑，OFL）；全部 7 款补齐 24/28/30，共 12–22 双数号 + 24–32。
     构建产物在 `perf/fonts-release-v1.1.0`，旧的 24 个文件与线上 v1.0.2 逐字节一致。
   - 壁纸 v1.4.0：原 17 张待机图原样沿用（含「叼书小狗」，用户决定保留）+ 16 张透明壁纸
     （manifest 的 `stickers` 列表，1.2.0 存到 `/images/透明壁纸`；旧固件只读 `items`）。
     构建产物在 `perf/wallpapers/release-v1.4.0`。
   - COS：凭据在 `~/.config/rickyos/cos.env`（先只核对格式，别直接 source），用
     `scripts/publish_rickyos_assets_cos.py --kind fonts|wallpapers --output <目录> --tag <版本>`，
     先 `--dry-run`；它先传版本目录、最后传 `cos.json`。
   - GitHub 备用线路：`chinoryunqin/RickyOS-fonts` 发 v1.1.0、`chinoryunqin/RickyOS-wallpapers` 发 v1.4.0
     （文件 + `fonts.json`/`wallpapers.json`，仓库里更新 `mirror.json` 给 jsDelivr）。
   - 发布说明写一句：已下载过的字体，到「字体管理 → 在线下载字体」再点一次即可补下新字号。

每一步对外操作（推网站、改 OTA、传 COS、发 GitHub Release）之前先问用户。
网站和 COS 的 OTA 要同一次发布里一起更新，先网站后 COS 都可以，但不要只更新一边。

## 5. 已知问题（不阻塞）

- 微信读书：偶发不开 WiFi 后卡死，复现时 USB 串口也不出现，还没拿到日志。
- USB 传书：Mac 访达大量生成预览时磁盘会被系统断开（全速 USB 的速度限制），按返回键可退出。
