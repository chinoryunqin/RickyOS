# 普通版 E-Ink 4 同步 RickyOS 1.2.17

`1.2.17-metalio-dev19` 基于普通版 dev18，合入 Plus 正式标签
`plus-v1.2.17` 的应用改动，以及 `bcb37a83` 的阅读统计保存保护。
普通版构建目标使用 ESP32-S3 和 480×800 的 SSD1677 显示配置。

- 新增繁體中文界面。界面字体独立选择，支持 SD 卡上的 TTF／OTF 和
  80%～140% 字号。界面字级按普通版实际像素调整。
- 段间距支持 0%～400%。100% 为原书间距，旧设置可迁移，旧分页缓存会重建。
- 阅读统计支持按字号重排和滚动。历史文件流式读取，保存保留有效备份。
- 储存列表部分可见的末行可以点击，点击范围以可见区域为准。
- 首页和书库在后台生成封面，设置卡片在大字号下为标题留出两行空间。

固件继续使用两个 6,553,600 B 应用槽。原生正文字形和书名字形的索引在
构建时无损压缩，从 312,336 B 降至 26,956 B。开机验证后解压至
261,860 B 的 PSRAM，其中两套字形共用 Unicode 索引。

```sh
python3 scripts/prepare_metalio_sdk.py
pio run -e rickyos_metalio_eink4 -e rickyos_metalio_eink4_core -e simulator_rickyos_metalio_eink4
python3 scripts/tests/test_settings_translations.py --metalio-native
python3 scripts/tests/test_metalio_font_metadata.py
python3 scripts/tests/test_paragraph_spacing.py
python3 scripts/tests/test_rickyos_stats_layout.py
python3 scripts/tests/test_partial_file_row_touch.py
```

真机待验收。重点检查旧 SD 统计恢复、140% 繁体显示和触控、阅读中打开
设置、合盖唤醒及自定义字体在 USB 接管前释放。
