# RickyOS 1.2 · Metalio E-Ink4-Plus

设备专用分支：`chinoryunqin/RickyOS-dev:codex/metalio-e-ink4-plus`。

适用设备：**Metalio E-Ink4-Plus V1.5 / E0470A14-AF**，ESP32-S31、4.7 寸并行墨水屏、FT6336U、冷暖双路前光。普通 E-Ink4 / ESP32-S3 / 3.97 寸 SPI 设备不使用此工程。

本目录是本分支的 Plus 开发入口。仓库根部的通用 RickyOS 工程保留为来源基线，不能作为这台设备的构建或刷机入口。

## 当前基线与设备状态

- RickyOS 来源：`codex/rickyos-1.2-design` 的 `73901ae83945d7fdf53d89967dff3750bf8235c0`。
- SDK 来源：`96de1be6ce08eb732909e6e8149af8f892b9a2c5`；来源清单与 67 项 Plus 修改独立记录，不使用原工作区 SDK 的未提交改动。
- 已构建并安装：design 73901ae8 整合版，5,221,216 bytes；SHA256 `2befbc8afcb6ec1905b62da52cbe79b9af0a1fb7f42131849ba463428046508f`。用户确认基础录音回放正常、触控震动正常、Tab 叠影恢复；USB 1 MiB 写入／读回 SHA256、安全弹出和调试口恢复通过。
- 当前代码包含显示、触控及盖板键、SD、前光、电量、RTC、网络迁移修改，以及本轮音量输出和试听。
- 基础录音回放与触控震动已获用户确认。音量曲线、录音时待机保存等场景尚未单独验收；网络排查按用户要求搁置。蓝牙配对和自动旋转尚未接入。
- 选择性接入设计提交 6e1d0ea7 的拼音、局部灰阶和透明待机；USB MSC 已新增 S31 后端并构建，安装／实机状态见 [USB 接入记录](docs/rickyos-1.2-plus-usb.md)。详见 [接入记录](docs/rickyos-1.2-plus-new-features-integration.md)。

## 还原和验证源码

从本目录运行：

```sh
python3 scripts/restore_rickyos_plus_sources.py
python3 scripts/check_rickyos_plus_sources.py
python3 tests/test_plus_audio.py
python3 tests/test_plus_frontlight_panel.py
python3 tests/test_plus_standby_frontlight.py
```

还原脚本读取固定 Git 提交、逐文件核对 SHA256，再应用 `firmware/rickyos-plus-1.2/upstream-plus.patch`，重新核对全部 2,822 个文件。它不会使用其他设备的工作区修改；已存在的源码只校验，不覆盖。也可通过 `--source-repo` 和 `--sdk-repo` 使用本地 Git 对象进行离线还原。

完整应用源码还原到 `firmware/rickyos-plus-1.2/upstream/`；Plus 自有板级组件位于 `firmware/rickyos-plus-1.2/components/`。生成文件、源码还原目录、二进制、设备私有备份和日志均不提交。

## 构建

当前已验证的构建依赖：ESP-IDF v6.1 `fff9895c82d744c7237be8847347bdd1b07c6643`，Arduino-ESP32 4.0.0-RC1 `76f683d935b390f2805301ec10a5562bbbb37811`，mDNS 1.11.2 `4fb97b209fbc695cc3019e0f1ef7937f766e6503`。其他库的版本与逐文件散列见 `dependency-lock.json`。

还原源码后，需要按上述固定版本准备本目录 `.local/arduino-esp32-4.0.0-RC1`、`.local/dependencies/` 和 `tools/esp-idf-v6.1`。当前分支未提供所有第三方依赖的一键下载器；完整构建已在原迁移工作区验证，不宣称全新环境已自动安装验证。

```sh
python3 -m pip install -r scripts/requirements-assets.txt
bash scripts/prepare_rickyos_plus_resources.sh
bash scripts/build_rickyos_plus.sh
bash tests/run_plus_port_tests.sh
```

构建输出 `.local/build-rickyos-plus-1.2/`。不要直接执行 IDF 输出提示里的整包 `idf.py flash`：它会默认写原厂 ota_0。本机受控安装依赖独立保存的原厂全闪存备份与设备身份校验，这些私有恢复材料不上传；本提交也不提供脱离备份的通用刷机脚本。

## 接入状态

- [全部外设与接入计划](docs/rickyos-1.2-plus-peripherals.md)
- [前光与待机行为](docs/rickyos-1.2-plus-frontlight.md)
- [显示、输入、电量和 RTC](docs/rickyos-1.2-plus-display-acceptance.md)
- [尚未解决的网络问题](docs/rickyos-1.2-plus-clock-diagnosis.md)

共享功能更新从设计分支显式引入，并在 Plus 上重新构建和验收；其他设备的引脚、波形、音频、电源配置不合入本目录。

厂商代码保留原版权声明，MIT 许可见 `firmware/rickyos-plus-1.2/components/metalio_board/LICENSE.vendor`。RickyOS 与 SDK 的许可随固定来源还原保留。

本地新增 [控制中心重设计](docs/rickyos-1.2-plus-control-center-design.md)：依据设计规范，矩形快捷键、前光／音量分组、横竖屏布局和收起按钮。构建及 32 组真实 FUI 几何测试通过，设备视觉验收待完成。

Plus 按键设置已实现并通过分类／逻辑映射及 JSON 持久化主机测试；本轮完整固件构建通过，实机按键验收待确认。见 [按键自定义](docs/rickyos-1.2-plus-buttons.md)。

按键入口已统一为「设置 → 系统 → 按键」，原有三项按键偏好保留在同一页；不再新增独立入口。

触控震动短脉冲和设置开关已完成，主机测试与完整构建通过；用户已确认正常。见 [触控震动](docs/rickyos-1.2-plus-haptic.md)。

2026-10-13：Plus 专用「应用 → 录音机」已实现麦克风采集、PCM WAV 保存和回放，主机与完整固件构建通过，用户已确认基础录音回放正常；待机保存、分页和故障场景仍待单独验收。见 [实现记录](docs/rickyos-1.2-plus-recorder.md)。

## 本机录音机界面预览

先按上文还原源码并生成资源，再运行 `python3 scripts/prepare_plus_recorder_preview.py`。在生成的 `.local/plus-recorder-simulator/` 目录运行 `pio run -e simulator_rickyos`，随后运行 `python3 tools/rickyos-browser/server.py --port 8783`，打开 http://127.0.0.1:8783/ 的「应用 → 录音机」。需要 macOS、SDL2 和 PlatformIO。

使用实际 RecorderActivity 的 684×1216 帧缓冲；采集和回放由内存状态模拟，不调用电脑麦克风或扬声器，重启清空模拟录音。不会操作 USB 或修改实机固件。

录音准备状态现由首批 PCM 判定，采集就绪时立即切换状态并短震一次；短震沿用触控震动开关，用户已确认「可以了」。

2026-10-13 已整合并上机 design 73901ae8 的阅读模式、阅读线、字重/TTF、统计月报、下载线路及绘制优化。无损字体压缩保持原 5MiB 槽；详见 [整合与验证记录](docs/rickyos-1.2-plus-design-73901ae8.md)。新功能实机交互待验收；小狗伴读左边框缺失已通过 DMA 完成判断修正，用户确认恢复。
