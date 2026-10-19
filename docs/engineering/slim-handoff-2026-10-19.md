# 交接：框架瘦身第 0、1 阶段（2026-10-19）

用户的要求：先做第 0、1 阶段。第 0 阶段是在三台设备上测出耗时分布，第 1 阶段是删掉没用的上游代码和设备支持。所有回复都用中文。

## 分支和提交（都只在本地，没有推送）

**设计仓库**：工作树 `/Users/rick/rickyos/wt-design`，分支 `codex/rickyos-slim`，从 `codex/rickyos-1.2-design` 的 a1115bc5 拉出。

| 提交 | 内容 |
| --- | --- |
| 45d312f1 | SDK 收进仓库：`freeink-sdk/` 从软链接改成真实目录（上游 96de1be 加上我们自己的改动，不含 lucide） |
| 24387909 | `tools/rickyos-perf/perf.py`：抓串口日志，汇总 `[PERF]` 行 |
| b7d86e21 | EEGO A4（`codex/eego-1.2.2` 9a815947）并入主树。A4 驱动直接放进 SDK 源码；构建时给 SDK 打补丁的脚本（epdiy/tls/sdcard/pmu_keys/eego/prepare_sdk）全部删除，对应测试改成直接检查源码 |
| bdc82ee2 | 删除不发货的设备，见下文 |
| 9ec0ae65 | 测量版构建 `rickyos_readpico_perf` / `rickyos_eego_a4_perf`，加上 `lib/Logging/RickyPerf.h` 和各处计时点，测量步骤写在 `docs/engineering/perf-measurement.md` |

bdc82ee2 删掉的内容：

- 除 Pico 和 A4 以外所有设备的构建环境（`platformio.ini` 从 1173 行减到 663 行）。
- 这些设备的 `#if` 分支，由脚本做部分求值后删除。
- 面板驱动和 LUT。
- 板子库：PaperS3、T5S3、XteinkDetect。
- `BoardConfig.h` 里的配置表（从 2395 行减到 1000 行）。
- nightly 打包脚本，以及相关测试和文档。

SDK 文件从 966 个减到 863 个，SDK 代码约少了 1.7 万行。整体约删了 2.4 万行。

**Plus 仓库**：工作树 `/Users/rick/rickyos/wt-plus`，分支 `plus-work`，WIP 提交 94ab69ff，同样没有推送。

- 已同步到 bdc82ee2，**还没有同步到 9ec0ae65**，所以计时代码还没进 Plus。
- 43 处冲突是手工合并的，提交说明里写了取舍。
- `sync_plus_design.py` 和 `restore_rickyos_plus_sources.py` 已经支持收编后的 SDK。同步时加 `--sdk-repo /Users/rick/rickyos/RickyOS/freeink-sdk` 参数。
- 顶层 `CMakeLists.txt` 支持 `RICKYOS_PERF=1` 环境变量。
- **Plus 还没编译过，也没有上机。**

## 已验证

- **Pico 和 A4 正式版**：删减后都能编译。A4 的目标文件与删减前完全一致；Pico 少了 16 字节（删掉了没人调用的 Xteink 识别代码）。
- **Pico 测量版**：能编译，约 5.79MB，比正式版大 1.4KB。
- **模拟器**：`simulator_rickyos` 和 `simulator_rickyos_eego_a4` 都能编译。这是在 9ec0ae65 之前编的，之后改过 ActivityManager。
- **脚本测试**：`.venv/bin/python -m unittest discover -s scripts/tests` 全部通过。部分 SDK 宿主测试本来就坏，比如 Imu 测试缺 `hasGyroscope`。

结论要如实告诉用户：删的都是不进固件的死代码，**这一阶段不会提速**。它的好处是代码只剩三台设备、SDK 由我们自己维护、构建不再依赖就地打补丁。真正的提速要等测出耗时分布之后再动手。

## 没做完的事（按顺序）

1. **编译结果（已完成）**：9ec0ae65 上四个固件全部编译通过。Pico 正式版 5,791,339 B、测量版 5,792,731 B；A4 正式版 5,992,750 B、测量版 5,994,298 B。日志在本地的 `.local-perf/`。
2. **Plus**：
   - 同步到 9ec0ae65：`cd wt-plus/ports/metalio-e-ink4-plus && python3 scripts/sync_plus_design.py --repo /Users/rick/rickyos/wt-design --to 9ec0ae65 --sdk-repo /Users/rick/rickyos/RickyOS/freeink-sdk`，有冲突就解决，再加 `--resume`。
   - 构建方法见 memory 里的 `rickyos-plus-port.md`：
     - 正式版用 `RICKYOS_IDF_PATH=.local/esp-idf-v6.1-r`。
     - 测量版再加 `RICKYOS_PERF=1`，用单独的 `RICKYOS_BUILD_DIR`。
   - 合并后有两处要在设备上验证：
     - Plus 现在会走严格的 TLS 校验（CA 加载失败就拒绝，并检查域名）。
     - HalGPIO 和 InputManager 的合并结果。
3. **上机实测**：按 `docs/engineering/perf-measurement.md` 做，需要用户插上设备。
   - 刷机前先用 ioreg 按 MAC 确认是哪台设备：Pico 14:C1:9F:D1:9F:58，A4 68:EE:8F:5D:4C:D8，Plus 1C:29:04:D0:2A:50。
   - Pico 和 A4 刷 0x10000。
   - Plus 的分区布局刷之前先读分区表确认。
   - 测完刷回正式版。
4. **出报告**：用 `perf.py summary` 汇总耗时分布，告诉用户时间主要花在哪里，以及下一步怎么改。候选方向：异步显示驱动、ACTIVE 改成编译期常量、去掉 Arduino 层。
5. **待用户决定**：
   - 删除 `.github/workflows`（上游 CrossPoint 的 CI，构建的都是已删设备）。这一步被权限拦了，需要用户点头。
   - 合并或推送 `codex/rickyos-slim` 和 `plus-work`：任何推送都要先问用户。

## 注意

- 磁盘很紧：剩约 11GB。`wt-design/.cache` 是 PlatformIO 编译缓存，可以删超过 2 天的旧文件。
- 改了 `platformio.ini` 之后，PlatformIO 会清空所有编译目录，每个环境都要整体重编，一个约 10 到 15 分钟。
- 运行时设备判断（`isX4Pro()`、`isXteinkDevice()` 这类）还留着。原因是 `BoardConfig::ACTIVE` 是可变变量，判断发生在运行时。把它改成 constexpr 是下一步的小优化。
