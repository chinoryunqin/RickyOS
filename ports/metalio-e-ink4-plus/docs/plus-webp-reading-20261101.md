# Plus WebP 阅读支持（2026-11-01，未发布）

## 需求与范围

用户报告《超神机械师》封面不显示，核对包内有 6 张 WebP（含封面及人物图），
均为静态 RGB，尺寸 864×1152、864×1884、800×1867/2125。
原 OPF 的 cover id/href 正确，固件 CoverImageType 与 ImageDecoderFactory 仅 JPEG/PNG。
用户明确要求系统支持 WebP，不能以离线改书替代。

本轮仅启用 Plus 构建的 RICKYOS_WEBP=1；公共接口以宏隔离，Pico 尚未移植/编译/刷机。
支持静态有损 VP8、无损 VP8L、透明通道；动画 WebP 当前拒绝播放，不阻止整章文本阅读。

## 实现

- 使用官方 libwebp 1.6.0（BSD-3-Clause + PATENTS），provenance.json 记录归档与逐文件摘要。
  只链接 decoder、必要 scalar DSP/util；无 encoder/mux/demux/动画播放器、SIMD 或线程。
- WebpRaster 采用 WebPIDecode + WebPIAppend，每次 4 KiB 输入，直接缩放到所需尺寸的
  caller-owned RGBA 输出，不先申请整个原分辨率 RGBA。无缩放时关闭 rescaler，避免多余重采样。
- 通过现有 DirectPixelWriter / NativeGrayscale16、灰度转换和 PixelCache 实现正文渲染；
  缩放、透明合成、4级缓存和16级原生输出均可使用，不改现有 JPEG/PNG 解码器。
- EPUB 封面及缩略图直接从 WebP 解码生成顶向下 8bit 灰度 BMP，写入原有缓存路径，
  无临时 PNG 重编码，不修改书籍。仅 cover.webp 提取临时文件，所有出口删除。
- ImageDimsProbe 识别 RIFF/WEBP + VP8/VP8L/VP8X；支持错误扩展名内容嗅探。
  元数据声明和 guide SVG 封面 href 允许 WebP，正文 SVG image 使用既有解析路径。

## 资源与错误边界

组件 utils.c 的唯一改动将 WebPSafeMalloc/Calloc/Free 接到独立的 PSRAM-only 分配器；
仍保留官方整数/乘法溢出验证。无内部内存 fallback，单次会话上限 8 MiB，并始终留
512 KiB PSRAM 空闲；不修改其他模块原有预留。原子会话占用避免两个 WebP 解码并发
争用同一预算，不阻塞等待，不持有全局图像缓冲。所有路径使用 RAII 清理。
RGBA 及输入块也计入预算。像素缓存自身按原有24 KiB上限工作，不计入 WebP 预算，
正文页/原生帧/字体内存均为读者既有对象，也不包含在所报告 WebP 峰值中。
压缩文件上限16 MiB，源图上限原有8MP/32767维度，解码输出上限2MP；超限/损坏/不支持
或内存不足返回图像失败，取消与写入失败删除不完整像素缓存，释放后可以重试。
每次增量输入和输出行周期 yield，取消在同步调用边界检查；单次 WebPIAppend 内不能强行中断。
这仍需要真机验证大型无损图、耗时及取消响应，不能用主机测试推算 S31 速度。

## 缓存迁移

Plus section 125/157 → 127/155（仅WebP开关启用）；无二进制布局新增字段。
图片提取/像素缓存前缀 img2_ → img_webp1_，避免先前被跳过的 WebP 导致后续图片编号
变化后误用旧 JPEG/PNG 文件。按书/章正常打开惰性重建，不全盘扫描，不删除源书或进度。
旧“封面无法解码”的0字节标记会重试一次；如果新能力仍无法生成，用 .webp-v1 sidecar
标记已尝试，继续既有 coverless 行为，避免每次进首页都解析一本无封面书。

## 验证

tests/test_plus_webp.py 编译实际 libwebp 源码、生产 WebpRaster、正文/BMP转换器、
ImageDimsProbe、ImageDecoderFactory、PixelCache、DirectPixelWriter；只替换硬件/SD/渲染器。
生成 fixture（有损、无损、透明、tiny、动画、损坏）和可选的用户本机6张原图不入仓库。
ASan/UBSan 通过：与独立 codec fixture RGBA 对照、透明覆盖、16级输出、2bit缓存、所有方向、
BMP readback、缓存写失败/取消、26个实际分配失败位置、512KiB预留、busy/失败后重试与零留存。
原书6图缩到64×48时解码峰值约153–178 KiB，684×1216封面峰值约3.33–3.36 MiB；
主机结果不含现有阅读帧和字体，也不是设备总功耗/速度结论。

目标 GCC（实际参数，关闭LTO仅为生成栈统计）检查已完成；WebP核心入口304B、绘制240B。
较大的 palette 工具函数是否链接及实际调用栈还需结合链接结果核对，不能以单函数栈相加
代替渲染任务剩余栈实测。本轮不扩大任务栈。

本机证据：output/book-diagnostics/webp-20261101。最终构建/整体验证/刷机结果待补充。


## 最终构建及回归

补丁 IDF 9a97f6c54ec638111ce55cd36581b3c192f15207 构建通过，源码校验为2842文件/
316项适配（含4个新增WebP文件），正常省电构建无BLE自动诊断和性能锁。
实际ELF保留WebP解码入口，未链接 GetColorPalette / PaletteSortModifiedZeng 等较大工具函数。
目标栈统计中实际解码辅助 VP8LBuildHuffmanTable 1056B，WebpRaster 304B，未扩大渲染栈。
内存包装分配明确用 heap_caps_aligned_alloc(max_align_t) 保证头部和codec对齐，元数据计入
8MiB会话预算；26个失败位置与所有原书图片针对性测试重新通过。

完整 port 回归退出0（有6项条件跳过）；更新了 CMake 依赖列表的过严字符串测试，
仍检查BT依赖存在。此后只补充分配对齐，相关真实WebP完整测试重新执行通过，未重复无关波形测试。
缓存迁移测试执行实际 BookCoverLoader 的检查/生成函数，旧空标记重试后正常封面被复用，
新失败标记落 .webp-v1 后不反复重建。Plus127/155实际缓存头与旧125/157拒绝均通过。

候选 1.2.13-bt9：6365008 B，SHA256 `448d61c6d9e1c0831cd8f6a2a78b05bf81f162b38013ef0cfc14a3c907079d65`。
本机包与安装记录：output/device-tests/plus-page-turner-bt9-20261101。
仍超过现有OTA上限6029312B，仅用于串口体验，未发布。


已在同一 Plus 串口刷入 bt9，写后摘要通过，非应用区域保持验证通过。
启动版本匹配，正常恢复 EpubReader 并完成页面渲染；日志中的旧 version125 拒绝是预期
惰性重建，不是章节无法打开。观察窗未见 panic/栈溢出。
已请用户看书库封面和人物介绍WebP页，真实WebP光学/速度/前后阅读切换仍待反馈。


## bt10 人物介绍空白修正（2026-11-01）

用户 bt9 反馈人物页空白，未出现卡死/错误。重新检查原书证实：
`OEBPS/Text/Section0001..0004.xhtml` 使用 body.qmp0..qmp3；图片仅在
`OEBPS/Styles/Style.css` 的 background-image 声明，不是 HTML img。
源码依据：CssParser.cpp 原1023行仅提取背景颜色，ChapterHtmlSlimParser.cpp 原1090行
仅从 img/image 的 src/href 创建 ImageBlock。之前的 codec 测试没有覆盖 CSS→页面路径，
因此不能代表这些人物页已兼容。

新增独立32路径/4096字节背景资源池，首次使用才分配4162B，nothrow失败可恢复；
不借用字体池，不放大任何网络/USB/蓝牙预留，池满只标记背景不完整、不停止字体规则。
CSS解析保存本地URL，拒绝远程/跨根路径；级联与 none 清除、inline XHTML相对路径、缓存
保存/载入均覆盖。CSS缓存17，Plus章节129/153；不更改 Page/ImageBlock 二进制布局。
body 背景通过现有懒提取 ImageBlock 放在每页文本之前，完整等比容纳插图，不占文本行高。
每页只新增 ImageBlock/PageImage 元数据，不申请另一帧。缺图保留文字，图片关闭/样式关闭
时不探测。当前为可读性回退，不宣称全浏览器 background cover/position/嵌套背景支持。

新增 test_plus_css_background.py，ASan/UBSan编译生产 CssParser、ChapterHtmlSlimParser、
ImageDimsProbe、ParsedText、BookFonts、WordStore；仅存储/显示度量为host doubles。
使用用户原书的样式及4个人物XHTML，确认都有 PageImage、名字不丢、跨页背景保留、
原书缺失的zhouyis背景不阻塞、关闭图片/书样式时不读取图片。
原生 WebP 实图解码、26个分配失败场景和字体加载回归保持通过。
最终构建/刷机与用户验收信息见设备测试目录（待追加）。


bt10 构建与刷机已完成：patched IDF 9a97f6c5，6,367,776 B，SHA256
`af13070cb6e3c28eab28272e30cd1a01454666945f993635d183dd9c5be044c1`。
完整 run_plus_port_tests.sh exit0（6项条件跳过），源记录2842/317、diff检查通过。
首次构建期间源仍在编辑，末尾源记录校验拒绝；未刷该包。稳定源重新构建全通过，
最终链接无首次混编产生的 CssParser LTO 类型不一致警告。
app0 0x10000 写入与MD5回读通过；bootloader/partition/OTA选择/NVS不变，安装器不访问SD。
启动版本bt10，恢复 EpubReader，实际WebP 800×2125→415×1104成功解码2次，
解码峰值2,013,739与2,005,472B，显示result=0x0；BLE正常启动并重连。
本轮日志 internal free96303/max63488，PSRAM约12.7MB空闲；未见panic/栈溢出。
解码计时不是单独图像上屏速度结论，最终人物页视觉验收仍待用户确认。
包与持续日志：output/device-tests/plus-page-turner-bt10-20261101/。
尚未提交、推送或发布；现有OTA包尺寸门槛仍是独立发布阻塞，不绕过。
