# RickyOS 1.1.5

适用于 MindReset 小纸 Read Pico（RDP-G01-W）。

## 安装与更新

- 网页安装：[RickyOS 安装工作台](https://chinoryunqin.github.io/RickyOS-site/install.html)。
  使用电脑 Chrome / Edge，原厂系统、CrossMux 或其他系统均可直接安装，完整备份可选。
- 已装 RickyOS：设置 → 系统 → RickyOS 更新。

## 这一版

- 修复中间键待机与唤醒开关不同步的问题：关闭后，中间键不再进入或退出待机，
  侧边电源键仍可唤醒。
- 新增 Markdown 排版阅读，支持标题、粗体、斜体、列表、引用和代码块。
- 新增 Markdown 表格排版，文字自动换行，复杂表格适配窄屏纵向显示。
- 新增六级标题目录，可点击跳转到对应标题。
- 优化 Markdown 阅读进度迁移，修复目录跳转可能停留在第一页的问题。

## Markdown 范围

面向设备阅读的语法子集，不是完整 Markdown 编辑器。目录标题另起新页；
表格跨页暂不重复表头。不支持合并单元格、嵌套列表、脚注、图片渲染或语法高亮。
链接显示文字，不打开或下载目标。
详见 [Markdown 阅读说明](https://github.com/chinoryunqin/RickyOS/blob/v1.1.5/docs/engineering/rickyos-markdown.md)。

## 固件校验

- 应用镜像：`RickyOS-1.1.5.bin`，6,029,312 字节。
- SHA-256：`1b02cfb6dc3c71093a4d0910061bfeaaee5e362c87f58d01bb57dff5d9536b56`。
- 原应用槽和 512 KiB 发布预留保持不变，启动组件与 1.1.4 字节一致。
- 网页发行目录和设备 OTA 目录由同一份元数据生成，使用同一镜像。

## 开源

RickyOS 基于 [CrossMux](https://github.com/0x1abin/crossmux)（MIT）开发。
源码：[chinoryunqin/RickyOS](https://github.com/chinoryunqin/RickyOS)。
许可原文随附件 `LICENSE.txt` 保留；待机时钟数字使用
[Inter](https://github.com/rsms/inter)（SIL Open Font License 1.1）。
