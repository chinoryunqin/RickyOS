# 耗时分布测量（2026-10-19）

目的：在 Read Pico、EEGO A4、Metalio E-Ink4 Plus 上，用同一套操作测出时间花在哪里（输入排队、绘制、面板刷新、章节加载、联网），作为后续去框架和换显示驱动的基线。

## 测量版固件

正式版不带任何计时代码。测量版只多出 `[PERF]` 日志行（`lib/Logging/RickyPerf.h`）：

| 设备 | 构建 |
| --- | --- |
| Read Pico | `pio run -e rickyos_readpico_perf` |
| EEGO A4 | `pio run -e rickyos_eego_a4_perf` |
| Plus | `RICKYOS_PERF=1 bash scripts/build_rickyos_plus.sh`（ports/metalio-e-ink4-plus，单独构建目录） |

测量完刷回正式版。

## 日志行

| 事件 | 字段 | 含义 |
| --- | --- | --- |
| `boot` | `setup` / `first_frame` | 上电到 setup 结束 / 到第一帧画完（毫秒，从启动算起） |
| `frame <页面>` | `wait` `draw` | 第一次请求重绘到开始画的排队时间；页面绘制耗时（A4 的面板刷新是同步的，包含在内） |
| `open <页面>` | `ms` | 切换页面（进入、返回）到新页面第一帧画完 |
| `disp` | `fast/half/full ms`、`gray16 ms`、`gray ms`、`wait ms` | 每次推屏耗时；Pico 的 16 级提交是 `gray16`，等面板空闲是 `wait` |
| `page hit/miss` | `prewarm` `draw` `status` `commit`/`display` `total` | 翻一页：缓存命中与否、预热、排版绘制、状态栏、推屏、总计 |
| `chapter cached/uncached` | `load` | 打开章节时读排版缓存的耗时 |
| `http` | `kb` `ms` `kbps` | 每次下载的大小、耗时、速率 |
| `wifi` | `connect` | 选择网络后到连上的耗时 |

## 操作脚本（每台约 5 分钟）

1. 开始抓日志：`python3 tools/rickyos-perf/perf.py capture --port <串口> --seconds 420 --out <设备>.log`
2. 长按关机再开机，等首页出现。
3. 首页 → 书库 → 应用 → 我的 → 首页，来回三遍。
4. 打开同一本中文 EPUB（三台用同一个文件），向后翻 30 页、向前翻 10 页。
5. 打开阅读菜单再关掉，退出到首页。
6. 设置 → 字体，打开在线字体列表（触发联网和下载）。
7. 进入待机再唤醒一次。

汇总：`python3 tools/rickyos-perf/perf.py summary pico.log a4.log plus.log --csv perf.csv`
