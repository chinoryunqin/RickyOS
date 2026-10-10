# RickyOS 1.1.5

适用于 MindReset 小纸 Read Pico（RDP-G01-W）。

## 安装与更新

- 网页安装：[RickyOS 安装工作台](https://chinoryunqin.github.io/RickyOS-site/install.html)。
  使用电脑 Chrome / Edge，原厂系统、CrossMux 或其他系统均可直接安装，完整备份可选。
- 已装 RickyOS：设置 → 系统 → RickyOS 更新。

## 这一版

- 修复「中间键待机与唤醒」开关不同步：关闭后，中间键不再进入或退出待机。
  侧边电源键仍可正常唤醒，自动待机与独立待机入口不受影响。
- 新增 Markdown（`.md`）阅读，书库、存储和 Wi-Fi 传书共用文件识别。
  支持标题、基础粗体／斜体、列表、引用、行内代码和围栏代码块。
- 支持 Markdown 竖线表格，按列换行；复杂表格在窄屏上纵向显示，保留内容。
- `#` 至 `######` 标题生成六级目录，可点选跳转；排除代码块中的假标题。
- 升级 Markdown 排版时保留原阅读位置，按需重建该书缓存，不清空 SD 卡。
- 修复目录目标尚未完成分页时，跳转错误停留在第一页的问题。

## Markdown 范围

面向设备阅读的语法子集，不是完整 Markdown 编辑器。目录标题另起新页；
表格跨页暂不重复表头。不支持合并单元格、嵌套列表、脚注、图片渲染或语法高亮。
链接显示文字，不打开或下载目标。
详见 [Markdown 阅读说明](../engineering/rickyos-markdown.md)。

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
