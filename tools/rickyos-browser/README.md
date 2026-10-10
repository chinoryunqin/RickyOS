# RickyOS 本地浏览器预览

浏览器显示 `simulator_rickyos` 的真实原生帧缓冲，并把点击、拖动和按键交给其现有 SDL 输入循环。
不是另写的一套 HTML 固件界面，也不是纯 WebAssembly。当前桥接支持 macOS；需要本机服务保持运行。
没有串口操作，不会连接、读取或刷写设备。

## 使用

在仓库根目录激活 `.venv`，运行 `python tools/rickyos-browser/server.py --open`。
然后打开 http://127.0.0.1:8765/。终端保持打开，Ctrl+C 停止。
服务仅绑定 `127.0.0.1`，不向局域网提供访问。

- 点击屏幕操作；按住拖动进行滑动，完整手势在松开后发送。
- 左侧模拟设备按钮，键盘 Esc/Enter/方向键/P/S 也可使用。
- 休眠后点击“电源 / 唤醒”。
- “保存原始截图”下载无损、原生分辨率 PNG。
- “重新启动”重启模拟器，保留演示 SD 卡和个人设置。
- 修改固件源代码后点“更新预览 · 重新编译”：只编译 `simulator_rickyos`，成功后自动重启；失败保留旧预览。

直接启动（在仓库根目录）：

```sh
python tools/rickyos-browser/server.py --open
```

原生模拟器尚未编译时，先执行：

```sh
pio run -e simulator_rickyos
```

需要已有的 SDL2 开发环境（`sdl2-config`）和 macOS C++ 编译器。
编译按钮优先使用本仓库 `.venv/bin/pio`，其次使用 PATH 中的 `pio`；
不会要求其他开发者拥有原作者的工作区目录。依赖安装见根目录 README。
支持 `--port 8765`、`--state-dir /absolute/path/to/private-demo` 和 `--open`。
不同进程不可共用同一份演示 SD 卡。

## 独立数据与边界

演示 SD 卡在仓库的 `.cache/rickyos-browser/sd/`，首次创建三本测试 TXT，不读取真实设备或备份。
可自行把测试 EPUB/TXT 放入此目录的 `books/`，图片放入 `images/`，再在书库刷新或储存中打开。
个人资料、阅读记录、书库缓存均只影响这份独立数据。重启不会清空；不要把私密书籍或账户配置公开分享。
首次已存在 `settings.json` 时不会重置该卡。

布局、导航、读取书籍与个人资料使用固件代码。设备 PSRAM/堆大小、真实字体文件、联网、
墨水屏刷新/残影、功耗与驱动行为并不等价于实机；不能据此确认硬件内存故障已修复。
浏览器轮询约 160ms，短暂过渡帧可能被跳过，无法用来精确校验开机动画时序。

原生模拟器固有的本机传输服务使用预览端口 +10/+11，请勿同时另启冲突端口的原生模拟器。
日记在 `.cache/rickyos-browser/simulator.log`，编译日志在 `build.log`。
桥接 dylib 只注入由本服务启动的进程；不修改固件、模拟器依赖缓存或已打包的 0.7 镜像。

## 验证

```sh
python -m unittest discover -s scripts/tests -p test_rickyos_browser.py -v
```

1.1.5 原生输入与 Markdown 集成回归（先编译 `simulator_rickyos`）：

```sh
python tools/rickyos-browser/check_middle_key.py --output build/qa-1.1.5
python tools/rickyos-browser/check_markdown.py --output build/qa-1.1.5
```

这两项使用独立临时演示 SD 卡，输出日志／截图到指定目录，不更改已运行预览的
数据，不读取 USB。Markdown 升级项构造文档化的 MD2 旧缓存与阅读位置，
验证新分页包含目标内容、没有退回第 0 页；它不是原厂迁移或真实设备验收。

表格专项使用本地 MD，验证转换出的表格、连续翻页至结束面板、重开及改变行距
触发重排，并核对 ATX 目录层级／文字／锚点及实际选项跳转。
可选传入之前保存的真实 MD3／MD4 原生程序，验证旧版→MD5 升级：

```sh
python tools/rickyos-browser/check_markdown_tables.py --sample /absolute/path/sample.md --output build/table-qa
python tools/rickyos-browser/check_markdown_tables.py --sample /absolute/path/sample.md --baseline /absolute/path/old-md3-simulator --output build/table-qa
```

只在私有临时 SD 中复制样本，不访问设备或上传文件。输出包含样本文字，须保持
本地，不提交或发布。内置 high-DPI 字体 ID 固定，所以这项检查的是行距重排，
不能替代 SD 字体／字号变更的真机验证。
