# Plus 控制中心重设计

2026-10-13。依据 upstream/docs/design-system.md（RickyOS 1.2 唯一规范）第 3–11 节、15.4 节，及 docs/contributing/touch-and-ui.md。文石照片仅作为分组参考，没有复制它的圆形快捷键、平板双栏主界面或未接入的 AI／录屏／位置／手写笔组件。

## 结构

- 顶部真实时间／电量，标题「控制中心」，右侧 88×88「收起」；保留状态栏点按、顶边下拉、向上滑与返回键。
- 第一组快捷操作：夜间模式、清刷屏幕、待机、传输。12 px 圆角矩形，2 px 描边，开启／按下为墨底纸色内容；待机 moon、夜间 contrast 的原图标保留。
- 24 px 圆角「前光」卡片包含亮度、冷暖两个独立滑条；亮度范围 1–100，开关独立，开启时开关反白，调节关闭状态的亮度会开灯。冷暖 0–100，值越大越暖。去掉重复操作说明。
- 音量单独卡片，0–100（0 静音），保留 +/-、拖动与试听按钮。
- 阅读时额外显示方向和清刷周期；横屏把快捷操作合并为一行，前光和音量分成三栏。横屏的开灯／试听移至快捷操作，避免三栏滑条因三个方按钮而窄到无法拖动。
- 底部收起把手；无大面积灰底、无抖动、无圆形快捷键外框。卡片边界和点击区域共用 FUI 几何。

## 实现与验证

保留原 HAL 控制、六个动作处理器、持有拖动时 250ms 刷新节流、松手刷新、退出时仅设置发生改变才落盘、待机关灯与音频停止行为。FUI interaction 仍固定 24 槽；未新增逐帧堆分配。移除不再使用的 TileGrid props/items 和时间/统计缓冲；行 props 保留在 activity 对象中，避免大 StyleSet 放在渲染栈。

tests/test_plus_control_center_layout.py 编译实际 buildRickyPanel/addSliderRow/roundActionRow 以及实际 FreeInkUI.cpp，使用记录绘制目标检查 32 组方向、阅读、前光/冷暖和音频组合；检查整张面板高度、24 槽不溢出、每个拖动滑条都存在、所有点按区至少 88×88、任意相邻交互区至少 8 px 且不重叠。ASan/UBSan 通过。

tests/test_plus_frontlight_panel.py 执行实际硬件回调，检查亮度/冷暖/音量边界、重复写入、关闭状态记忆、试听、保存时机与高度；通过。完整 IDF 构建和源码校验结果见 logs/plus-cc-redesign-build.log。

布局示意在 control-center/plus-control-center.svg，数值和图标为示意；固件显示实时数值并使用现有图标。该示意不冒充模拟器截图或实机照片。

物理观感、点击／拖动手感与本轮安装状态，以设备验收及 dist/manifest.json 为准。新代码同步至本地 Plus 分支目录，未提交／推送新的提交。

本轮固件已刷入 ota_1，读回校验通过，原厂槽校验一致；启动日志确认加载 0x580000、新 ELF 75cc96a39… 并进入 InxRecent，首次显示提交成功。记录见 logs/plus-cc-install.log 和 logs/plus-cc-boot.log。实机控制中心观感、点按与拖动等待用户验收。
