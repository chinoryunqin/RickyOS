# RickyOS 1.2 · Metalio E-Ink4-Plus

设备专用分支：`chinoryunqin/RickyOS-dev:codex/metalio-e-ink4-plus`。

适用设备：**Metalio E-Ink4-Plus V1.5 / E0470A14-AF**，ESP32-S31、4.7 寸并行墨水屏、FT6336U、冷暖双路前光。普通 E-Ink4 / ESP32-S3 / 3.97 寸 SPI 设备不使用此工程。

本目录是本分支的 Plus 开发入口。仓库根部的通用 RickyOS 工程保留为来源基线，不能作为这台设备的构建或刷机入口。

## 当前基线与设备状态

- RickyOS 来源：`codex/rickyos-1.2-design` 的 `24c05ff7ffb06254879d9928d8531db6839e7ad1`。
- SDK 来源：`96de1be6ce08eb732909e6e8149af8f892b9a2c5`；来源清单与 45 项 Plus 修改独立记录，不使用原工作区 SDK 的未提交改动。
- 已构建并安装：控制中心音量版，5,186,400 字节；SHA256 `78b8ab9b6f8174a0013ee104e3097371210fc21ccb5e2fcb64dfccc67ea70b4e`。
- 当前代码包含显示、触控及盖板键、SD、前光、电量、RTC、网络迁移修改，以及本轮音量输出和试听。
- 扬声器实际出声／音量曲线、待机关灯的物理验收待完成；网络故障仍待排查。麦克风、蓝牙配对、震动、自动旋转、USB MSC 未全部接入。
- 远程设计分支已有后续更新。本设备分支先保存上述已上机基线，未自动合并更新。

## 还原和验证源码

从本目录运行：

```sh
python3 scripts/restore_rickyos_plus_sources.py
python3 scripts/check_rickyos_plus_sources.py
python3 tests/test_plus_audio.py
python3 tests/test_plus_frontlight_panel.py
python3 tests/test_plus_standby_frontlight.py
```

还原脚本读取固定 Git 提交、逐文件核对 SHA256，再应用 `firmware/rickyos-plus-1.2/upstream-plus.patch`，重新核对全部 2,758 个文件。它不会使用其他设备的工作区修改；已存在的源码只校验，不覆盖。也可通过 `--source-repo` 和 `--sdk-repo` 使用本地 Git 对象进行离线还原。

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
