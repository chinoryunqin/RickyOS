# 字体与待机图片的国内下载源（腾讯云 COS · 广州）

设备在国内下载字体和待机图片时，先读 COS 存储桶，再依次退到 jsDelivr、Fastly、GitHub。
海外用户顺序相反，COS 排在最后。每份清单都把其他线路列为备用，单个文件失败时会换线路重试。

| 线路 | 清单 | 文件 |
| --- | --- | --- |
| COS（广州） | `<桶域名>/<kind>/cos.json` | `<桶域名>/<kind>/<tag>/<文件>` |
| jsDelivr / Fastly | `…/gh/<仓库>@latest/mirror.json` | `…/gh/<仓库>@<tag>/<kind>/<文件>` |
| GitHub | `…/releases/latest/download/<kind>.json` | `…/releases/download/<tag>/<文件>` |

`<kind>` 是 `fonts` 或 `wallpapers`。COS 前面没有 CDN 缓存，清单读到的总是最新版本；
发布后的版本目录不可覆盖，正在下载的设备不会读到半新半旧的文件。

## 一次性设置

存储桶：`rickyos-1309217881`，广州（`ap-guangzhou`），
域名 `https://rickyos-1309217881.cos.ap-guangzhou.myqcloud.com`，已写进 `src/network/RickyCos.h`。

1. 存储桶访问权限设为「公有读私有写」。
2. 新建一个只能写这个桶的子账号密钥（CAM），不要用主账号密钥。
3. 安装 [coscli](https://github.com/tencentyun/coscli)。

桶里还没有目录时，设备会立刻收到 404 并改读 jsDelivr，不会变慢。要关掉 COS 线路，
在 platformio.ini 里加 `-DRICKYOS_COS_BASE=\"\"`。

默认域名支持 HTTPS，不需要备案。以后想换自定义域名并加 CDN 加速，域名要先做 ICP 备案；
固件改 `RickyCos.h` 里的域名，脚本里的 `--cos-base` 同步换掉。

## 每次发布

先按原流程构建，加上 `--cos-base`（产出多一份 `cos.json`，其他清单的备用列表也会带上 COS）：

```bash
python3 scripts/build_rickyos_font_catalog.py --output out/fonts-v1.1.0 --tag v1.1.0 \
  --cos-base https://rickyos-1309217881.cos.ap-guangzhou.myqcloud.com
python3 scripts/build_rickyos_wallpapers.py --sources pics --output out/wallpapers-v1.4.0 --tag v1.4.0 \
  --slogan-font SmileySans-Oblique.otf --cos-base https://rickyos-1309217881.cos.ap-guangzhou.myqcloud.com
```

照旧上传 GitHub release。然后发布到 COS（密钥只放环境变量，不写进命令或仓库）：

```bash
export COS_SECRET_ID=… COS_SECRET_KEY=… COS_BUCKET=rickyos-1309217881 COS_REGION=ap-guangzhou
python3 scripts/publish_rickyos_assets_cos.py --kind fonts --output out/fonts-v1.1.0 --tag v1.1.0 --dry-run
python3 scripts/publish_rickyos_assets_cos.py --kind fonts --output out/fonts-v1.1.0 --tag v1.1.0
```

脚本先上传文件（禁止覆盖，长缓存），最后才上传 `cos.json`（不缓存），并检查 `cos.json`
指向的桶、版本和参数一致。发布后可在浏览器打开 `<桶域名>/fonts/cos.json` 核对。

## 费用与验证

- 费用主要是外网下行流量，按量约每 GB 几毛钱。一套字体（12–32 档）约 30 MB。
- 设备串口日志里每个请求都有一行 `[HTTP] … B in … ms (… KB/s), status …: https://<域名>`，
  可以看出时间花在哪条线路上。
