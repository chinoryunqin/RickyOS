# RickyOS 在线刷机站

独立的静态网站，面向 MindReset 小纸 Read Pico（RDP-G01-W）。不编入设备固件，
不增加 ESP32 的 RAM / Flash 占用。参考网站用于流程研究，未复制其代码或镜像。

## 本地运行

使用 Node.js 22.12+（或受 Vite 支持的新版 Node）和 npm，在本目录执行：

```sh
npm ci
npm test
npm run build
npm run preview
```

产品首页 <http://127.0.0.1:8766/>；安装工作台 <http://127.0.0.1:8766/install.html>；
流程演示 <http://127.0.0.1:8766/install.html?demo=1>。
开发时可改用 `npm run dev`。原生固件 UI 预览仍在 8765，不冲突。
演示模式不会创建串口适配器，也不会读取、下载或写入真实设备数据。

## 当前能力与验收边界

- 已选用 A 产品首页 + B 独立安装工作台，FAQ 和开源/隐私说明随页保留。
  首页只加载品牌展示脚本，不加载串口控制器；安装阶段按真实流程推进，不能任意点击跳过。
  旧 `/#install` 和 `/?demo=1` 自动跳到新的安装页，URL 全部相对路径，适配 Pages 子目录。
- 首页和侧栏使用真实原生模拟器截图并明确标注，不伪装实机或 AI 重绘。品牌 logo 来自已选定资源。
  `design-preview/` 仅保留设计对照，不属于发行入口；不导入其模拟按钮或脚本。
- 用户主动授权 Web Serial；检测 S3 / 16 MB，安全状态不明确则停止。
- RAM stub 来自固定依赖 esptool-js 0.7.0，运行前检查安全状态。
- 完整读取 16 MB，以 64 KiB 分块；消费并校验每次 READ_FLASH 的末尾 MD5 包，
  再核对完整 Flash 的设备 MD5，生成本地下载。不上传备份或设备日志。
- 下载后必须重新选择已保存的文件，SHA-256 匹配才能获得本次会话安装资格。
- 一个安装入口，备份后自动识别路径，不要求用户先刷 CrossMux 或选择模式。
- 兼容更新必须匹配六分区完整布局、分区表 MD5、有效 OTA 序号/CRC、
  当前应用的 S3 头与 Read Pico board tag。通用 S3 芯片识别不是实体机型证明，
  必须由用户同时确认设备型号。
- 原厂识别采用厂商当前四分区 / 4 MB 应用 / FAT 内部存储布局及 `Read_Pico`
  应用描述。安装还须匹配发行包中经过实机验收的三项原厂指纹：分区表 4 KiB、
  启动区域 `[0, 0x8000)` 和整个原厂应用分区 `[0x10000, 0x410000)` 的 SHA-256。
  旧文档的 2 MB 布局不作为实际设备事实；未知布局、未知版本只允许备份。
- 固件必须经过正式发行门槛、SHA-256 / 大小 / 品牌 / 版本 / 机型检查。
  写入前复核整个设备、分区和 OTA 记录，避免备份后内容变化。
- 已有兼容系统只改写现有活动应用槽，不写 bootloader / 分区表 / otadata。
- 原厂首次安装将应用按扇区补齐写入 `0x10000`，再用有界 64 KiB 全 FF 写入
  清理剩余内部应用/存储区域（不是整片擦除）；每次写入都核对设备 MD5。
  然后写入已验证的 bootloader、boot_app0，核对完整暂存结果；分区表最后写入。
  固定保留 `[0x9000, 0xe000)` 的 NVS 字节；原厂设置是否可沿用不作承诺。
  最终完整 Flash MD5 匹配预计镜像后才重启。失败时保持 BOOT，由维护者恢复。
  首次安装不是断电原子事务，写入后断电可能无法启动，必须保管原厂备份。
- 两种路径均不擦除全片、不操作 SD，也不写 PMU / VCOM。额外缓冲仅在电脑浏览器，
  不增加 ESP 内存占用：预计镜像和提交前镜像各 16 MB，擦除块固定 64 KiB。
- 写入失败后不自动重启，重用安装资格被禁止。拔线/断连需重新开始。
- 日志只保留最近 300 条并遮蔽 MAC，不持久化设备身份，不读取任何私人目录。

**当前 `public/releases.json` 为空，真实连接、备份和刷写均不开放。**
公开网站只展示系统和流程演示，勾选机型也不会弹出 USB 授权窗口。
必须先通过 `checkRelease` 检查，才能解除空目录的连接限制；不允许将开发候选作为正式镜像上架。
具体镜像大小与槽位余量以最终发行构建为准，仍须满足至少 512 KiB 保留门槛。
46 项自动测试中，28 项使用内存设备替身覆盖拒绝路径、首次迁移顺序与写入范围，
5 项验证页面与链接，4 项验证连接门禁，5 项验证预览发布范围，4 项验证 OTA 目录；
不证明 USB 实机可用。
真实串口、BOOT 进入、断线/重试、低速读取、摘要协议和重启时机仍需独立实测。
完整 Flash 备份不含 SD 卡；本网站暂不提供整片备份恢复。
首次迁移代码已实现，但没有已验收的原厂指纹/正式安装包，目前真实安装不可用。

## 固件上架契约

先完成硬件验收与发行审核，再由维护者添加唯一正式镜像和目录记录：

```json
{
  "schema": 1,
  "product": "RickyOS",
  "releases": [{
    "version": "1.6.5-rickyos-pico.13",
    "approved": true,
    "hardwareAccepted": true,
    "mode": "auto-install",
    "board": "readpico",
    "chipId": 9,
    "flashBytes": 16777216,
    "file": "firmware/RickyOS-13.bin",
    "bytes": 0,
    "sha256": "填写实测镜像 SHA-256",
    "fullInstall": {
      "approved": true,
      "hardwareAccepted": true,
      "segments": [
        {"role": "bootloader", "offset": 0, "file": "firmware/bootloader.bin", "bytes": 0, "sha256": "实测值"},
        {"role": "partitions", "offset": 32768, "file": "firmware/partitions.bin", "bytes": 3072, "sha256": "实测值"},
        {"role": "boot_app0", "offset": 57344, "file": "firmware/boot_app0.bin", "bytes": 8192, "sha256": "实测值"}
      ],
      "factorySources": [{
        "id": "original-release-id",
        "layout": "mindreset-factory-4m-v1",
        "hardwareAccepted": true,
        "tableSha256": "已验收原厂分区表摘要",
        "bootloaderSha256": "已验收原厂启动区域摘要",
        "appSha256": "已验收原厂应用分区摘要"
      }]
    }
  }]
}
```

示例的大小/哈希是占位，不能构建。正式版本不能带 `-dev` 后缀，4 字节对齐，
擦写到 4 KiB 扇区边界后须留下至少 512 KiB（最大镜像 6,029,312 字节）。
镜像缺少 Read Pico tag、存在其他机型 tag、品牌/版本不匹配、开发版或未验收时拒绝。
`npm run build` 在打包前检查目录和镜像；`public` 出现任何未登记文件或符号链接也会失败。
固件 `.bin`、`dist/`、`node_modules/` 均忽略；不要 force-add 私有构建和备份。
校验值防止误传/损坏，不是独立签名验证；托管仓库和部署账户必须保持受控。
三个启动组件必须来自同一次目标构建与对应 Arduino 框架，不从其他机型借用。
构建同时核对各组件摘要、S3 启动镜像头、完整目标分区和首槽 OTA 启动记录。
分区表产物允许标准 3072 字节或已补齐的 4096 字节；写入/校验统一补 FF 到 4 KiB。
保留 `app-upgrade` 旧契约仅用于应用更新兼容，不为用户显示另一入口。

## GitHub Pages 上线

部署的是 `npm run build` 生成的整个 `dist/`，不是固件仓库根目录。
相对资源路径兼容 `https://chinoryunqin.github.io/RickyOS/` 和后续自定义域名。
`dist/third-party-licenses.txt` 自动保留运行时依赖许可证原文。
不要只上传 HTML：S3 stub 和本地 JS/CSS 必须随目录一起部署。
必须同时包含 `index.html`、`install.html`、`licenses.html`、`assets/`、发行目录和许可文本。

源仓库 `chinoryunqin/RickyOS` 继续私有。当前账号不支持私有仓库 Pages，
因此网站使用独立公开仓库 `chinoryunqin/RickyOS-site`，只提交审核后的站点成品。
网站地址：<https://chinoryunqin.github.io/RickyOS-site/>。
公开仓库不包含设备固件源码、固件二进制、原始肖像照片、私人书籍或备份；
网页代码、品牌展示素材与许可说明本身属于公开网站内容。

维护者更新网站时，先运行 `npm ci`、`npm test`、`npm run build` 和
`node scripts/check-preview.js dist`，随后只将 `dist/` 的完整成品复制到公开仓库的
`gh-pages` 分支。Pages 仅发布该分支根目录，不触发私有仓库的固件发行工作流。
在公开仓库放置 `.nojekyll`，避免 Jekyll 改动静态成品；使用普通 fast-forward 推送，
不要 force push。公开仓库历史中只能出现经过核查的网站成品。

正式固件发布需单独授权并完成以下实机验收。当前的预览检查脚本故意拒绝非空发行目录
和固件文件；不能跳过此检查把开发候选发布到预览站。

`dist/ota.json` 在每次构建时由同一 `public/releases.json` 自动生成，不单独维护。
设备只读取自己的正式频道，空目录明确返回 `status: no_update`；禁止回退到 CrossMux。
以后上架正式固件时，网页与设备必须使用完全相同的版本、镜像大小和 SHA-256。
更换网站域名须同时更新设备的 `RickyOtaPolicy.h` 和信任根配置，不能仅改网页地址。

## 实机验收清单

1. 先只检查与备份，核对 16 MB 文件 SHA-256 / 设备摘要；中途断线必须禁写。
2. 已授权升级时，使用经过审核的正式包；两种活动槽和不同旧版 OTA 状态分别检查。
3. 串口噪声、坏目录、错误机型、容量、加密、安全启动、改动备份必须拒绝。
4. 写入中断不重启；恢复路径由维护者使用已验证的独立工具，不是网页任意地址写入。
5. 成功后确认新首页、阅读进度、SD 文件、字体、待机、唤醒；不能仅看进度条判定成功。
6. 在可恢复的原厂测试机验收首次安装：记录三项指纹、组件来源；确认 NVS 字节不变、
   SD 未操作、旧内部 FAT 数据清理、新文件系统初始化、重启和 PMU 只读 VCOM 正常。
7. 原厂首次安装在应用、内部空间、启动组件、分区提交各阶段模拟断线；禁止自动重启，
   由维护者验证完整备份可恢复后，才能标记该原厂版本 `hardwareAccepted`。

协议参考：[Espressif esptool-js](https://github.com/espressif/esptool-js)、
[串口协议 Reading Flash](https://docs.espressif.com/projects/esptool/en/latest/esp32s3/advanced-topics/serial-protocol.html)。
上游分区与发行依据见 `../../docs/engineering/{read-pico,firmware-release}.md`。
原厂当前布局以 [MindReset 分区源码](https://github.com/MindReset/read_pico_firmware/blob/main/partitions_16M.csv)
和 [Read_Pico 项目名](https://github.com/MindReset/read_pico_firmware/blob/main/CMakeLists.txt) 为准，
而非旧工程文档的 2 MB 推断；本次没有改写原厂机或将参考源码指纹当作实机验收。
