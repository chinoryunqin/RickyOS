# 独立网页设计提案

仅比较视觉方向，不修改 8766 的现有刷机站，也不包含在当前 Vite 发行入口中。
`concepts.js` 不导入刷机逻辑、不调用 Web Serial、不请求后端；所有状态都是设计示例。

在仓库根目录运行：

```sh
python3 -m http.server 8767 --bind 127.0.0.1 --directory tools/rickyos-flasher/design-preview
```

- 对照页：http://127.0.0.1:8767/
- A 产品官网：http://127.0.0.1:8767/a.html
- B 安装工作台：http://127.0.0.1:8767/b.html

`assets/home.png` 是本机 RickyOS 原生模拟器的帧缓冲截图，不是 AI 重绘或实机照片。
`assets/logo.png` 复制已选定的品牌资源，不作变形修改。截图包含演示阅读数据。
这些不是公开发布素材；选择方向后再接入真实安装状态、发布门禁与正式发行内容。

已检查两版 1280 px 桌面和 390 px 窄屏，无横向溢出；A 面板展开、B 勾选和阶段切换
通过浏览器操作检查；JavaScript 语法检查通过。本轮未连接设备、未刷写、未发布网站。
