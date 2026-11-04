# 普通版 E-Ink 4：显示资源与可选模块架构

范围：普通版 ESP32-S3、SSD1677、800×480；候选版本 `1.2.11-metalio-dev17`。
本轮按用户要求只做架构改造、主机验证和构建；不连接、刷写设备，不确定商业范围或发布频道。
实屏基线仍为已经验收的 dev16，不能把该验收直接扩展为 dev17 的上机验收。

## 显示分工与所有权

| 层 | 职责 | 资源与约束 |
|---|---|---|
| Activity / GUI | 实际像素尺寸下排版、字体与交互 | 不直接调用 SDK，不拥有显示历史 |
| GfxRenderer / HalDisplay | 页面合成、提交与回退 | 保留 192,000 字节 4bpp PSRAM 工作画布，抗锯齿合成精度不变 |
| SDK SSD1677 UI 驱动 | 四灰量化、变化选择、周期清理与 BUSY 状态 | 96,000 字节 2bpp 已提交历史；成功完成后才更新历史 |
| SDK 阅读组合 AA 驱动 | 当前阅读渲染会话的选择平面及物理历史 | 八个 48,000 字节 PSRAM 平面，进入该路径时申请，切走后释放 |
| SSD1677 控制器 | 显示 RAM、波形与物理扫描 | 屏内 RAM 不属于可供应用分配的 ESP32 RAM |

4bpp 画布用于合成；2bpp 历史只保存已经提交的四灰结果，低位像素在先。
比较时，对新画布量化后的值与历史值比较，因此历史压缩不提前量化字体或透明度合成。
192,000 → 96,000 字节不改变显示平面、LUT、驱动时长、电压或五次变化更新后的清理策略。
该历史跨页面存活，继续由 SDK 驱动持有，不能放入任务栈或内部 / DMA 堆。

按以上列出的固定缓冲计算（不含字体、解码、临时绘制和异步影子等其他资源）：

| 场景 | dev16 | dev17 | 减少 |
|---|---:|---:|---:|
| 非阅读界面 | 816,000 B | 336,000 B | 480,000 B PSRAM |
| 已进入阅读组合 AA | 816,000 B | 720,000 B | 96,000 B PSRAM |

这是源码容量及主机分配检查的结果，尚未测量本机可用堆、峰值、碎片或耗时变化。
这些 PSRAM 节省量不等同于内部 RAM、DMA 内存或 Flash 空间增加。

## 阅读资源会话

1. 初始化只报告匹配的控制器及几何能力，不预分配普通版的阅读平面。
2. 明确请求文字 AA 时，先完整申请八个平面，再进行驱动交接。
3. 任一申请失败，释放所有部分申请，当前事务继续使用原有灰阶驱动；后续页面可重新尝试。
4. 同一阅读路径连续绘制复用已分配资源，不逐帧申请和释放。
5. 切回原驱动时，先结束旧工作、关停 / 复位控制器并确认 BUSY 已解除，再释放退出路径的平面。
6. 释放同时失效旧驱动的玻璃历史、待提交平面和代次；下一次进入必须建立自己的状态。

能力查询与实际事务分开：`supportsTextOnlyCombinedBase()` 表示匹配该后端；
`combinesGrayscaleBase()` / `_textAaPending` 跟随本次实际选择。瞬时 OOM 不会永久关闭硬件能力。
旧驱动关停失败但新驱动复位恢复后，同样回收已退出的缓冲；BUSY 未解除时不释放并继续依赖原有恢复路径。
其他板卡保留既有分配策略，本轮不改变其生命周期。

## 可选外设模块

`lib/hal/MetalioFeatures.h` 区分板卡身份和构建选择。`RICKYOS_METALIO_CAMERA`、
`RICKYOS_METALIO_CELLULAR` 仅接受 0 / 1，默认保留普通版当前完整构建。
HAL 实现、应用入口及调试页面工厂使用相同的有效开关；关闭模块使用无任务、无硬件副作用的 HAL 实现。
网络调用仍经过 `HalNetwork`，相机与蜂窝的具体 SDK 保留在 HAL 内部。
构建脚本按相同开关过滤外设 SDK 源文件，并只在启用相机时强制链接其 USB 枚举入口；
核心验证配置还显式排除外设库，避免 LDF 从已安装的依赖或缓存链接中重新发现它们。

- `rickyos_metalio_eink4`：默认完整开发候选，仍包含现有外设工作，不能据编译判断相机已适配。
- `rickyos_metalio_eink4_core`：架构验证配置，使用同一板卡、字体、显示和阅读代码，
  不依赖相机 / 4G 的 SDK 包，也不强制链接相机枚举入口。它不是商业功能范围或发布频道。

两个构建均只生成本地产物，不能自动上传、发布或刷机。

## 可重复检查

```sh
python3 scripts/prepare_metalio_sdk.py
python3 test/metalio_architecture/run.py
python3 freeink-sdk/libs/display/FreeInkDisplay/test/host/test_metalio_ui_retained.py
python3 freeink-sdk/libs/display/FreeInkDisplay/test/host/test_ssd1677_text_route.py
python3 freeink-sdk/libs/display/FreeInkDisplay/test/host/test_ssd1677.py
python3 scripts/tests/test_native_grayscale.py
pio run -e rickyos_metalio_eink4
pio run -e rickyos_metalio_eink4_core
```

旧 UI 波形差分对照使用已恢复 dev16 补丁的 SDK 检出：

```sh
python3 test/metalio_architecture/compare_ui_trace.py --baseline <dev16-sdk-checkout>
```

dev16 的 SDK 基线为 `96de1be6ce08eb732909e6e8149af8f892b9a2c5`，
补丁位于 RickyOS 提交 `ab54131347a6b27d4d7d46060dcc0dc3fc4b3d9b` 的
`patches/metalio-eink4/freeink-sdk.patch`。本轮差分指令流 20,725 字节完全相同，SHA-256
`df86f042a730aec7e2063e08791557f94b08cd9a4f74bd4ab119452d68596886`。

主机检查包括：16 种四灰转换、2bpp 容量与边界、相同量化值不刷新、周期 / 强制清理、
连续 12 次阅读进出、关停失败后的资源交接、八个申请位置逐个 OOM 后回退与重新进入、
原阅读 / 图像 / 休眠 / BUSY 路由及禁用模块的真实 HAL 无外设 SDK 链接。
主机和构建证据保存在 `artifacts/metalio-architecture-20261104`；硬件验收按用户安排另做。

## 本轮构建结果

两种配置使用隔离的 Metalio PlatformIO 包目录完成编译，未上传固件。

| 配置 | 应用 bin | 6,553,600 B 槽剩余 | 静态 RAM |
|---|---:|---:|---:|
| 默认完整候选 | 6,382,272 B | 171,328 B | 124,724 B |
| 核心架构验证 | 6,256,416 B | 297,184 B | 123,148 B |

核心版最终 map 没有 `MetalioCamera`、`MetalioModem`、`UartUhci`、`IotEth` 对象，
ELF 没有对应驱动的已定义符号或自定义 `enum_install`；完整版保留这些模块。
这证明依赖边界生效，不代表产品决定移除外设。

本轮主机测试通过，包括 ASan / UBSan 资源和故障路径检查；SDK 补丁的 102 个路径
在干净的固定基线检出中完成恢复及重复校验。全仓 CI 检查因既有未初始化子模块被阻塞，
没有将其记作通过，也没有初始化或重置现有 SDK。

机器可读记录为 `docs/metalio-eink4-architecture-validation-20261104.json`。
本地候选包位于 `output/firmware/metalio-eink4-1.2.11-dev17`（主工作区目录下），
包含构建哈希与未上机状态；源码包保留 SDK 补丁、测试和字体输入。

## 后续硬件与发行工作

连接设备后再测实际内部堆 / DMA / PSRAM 峰值和最大块、长时间切换与阅读、休眠恢复、
4G 和相机资源竞争。商业功能范围与正式发行策略另定。
既有 RickyOS 在线更新契约及应用槽保留空间仍需单独满足，不能以降低 PSRAM 占用替代。
