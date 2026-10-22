# Pico 词典与缺字字库资源接入 · 2026-10-22

## 范围

在本地 `codex/pico-library-battery-trial` 的书库、电量、精排整合版本上接入
自有资源。参考 Plus `codex/resource-service-migration` 已准备的共享补丁，
按 Pico 当前源码适配；没有混入 Plus 的驱动、网络或电量实现，也没有改变
另一工作树的源文件。此前刷入 Pico 的整合包不包含此迁移。

- 新增 `src/network/RickyReaderResources.h`，保存版本化目录及来源检查。
- 词典目录使用 `/dictionaries/m1/cos.json`，中文界面优先显示 `nameZh`。
- 自动缺字字体使用 `/font-fallback/m1-b4/cos.json`。
- 普通字体管理继续使用 `RickyFontCatalog` 的原有线路；不改壁纸或固件 OTA。
- 两个新目录和资源均在已有 RickyOS COS 源，不隐式回退到 CrossMux 资源服务。
- 没有新增设备端堆分配或后台任务，沿用已有索引限长、流式下载、CRC、取消及安装恢复。

本轮是资源来源迁移，不是新增查词引擎。在线目录为已确认的 ECDICT、FreeDict、
Open English WordNet；保留已有 ID/revision、`.crossmux-resource` 安装标记、
手动导入和目录冲突保护，不删除用户已有词典。
缺字字库沿用原 NotoSansSC cpfont v4 的 8/10/12/14/16/18 pt 六档；
本轮没有补齐 20–32 pt，也不承诺覆盖所有 Unicode 字符。

## 本地验证

- `test_rickyos_reader_resources.py`：3 项通过，包含实际 C++ 路由常量与
  默认/关闭/自定义服务配置、短字符串/伪装域名、目录接线和旧安装兼容断言。
  路由 C++ 检查带 ASan/UBSan；字符串接线检查不是完整下载 Activity 的执行测试。
- 普通字体目录：10 项通过；COS 资源脚本：11 项通过。
- `DictionaryResourceTest`：3 项通过，覆盖资源命名、安装标记和状态判定。
- Read Pico 完整固件编译通过：5,933,040 字节，未超过 6.25 MiB 应用分区。
  BIN SHA256 `22135fe4917ef7780a07ed02e8f088ced90495b677084ea0b989e96224944de4`；
  ELF SHA256 `87044127e6870712c43d88e10c94cd15001bea675ca28cb0c2866108c9428b2f`。
- 开发完成时没有刷入、提交、推送或发布；后续授权刷入记录见下节。
  本轮没有向资源服务上传或改写任何对象。

资源端校验共 21 个对象（19 个资源及 2 份目录），完整长度和 SHA256 校验通过。
初次批量访问出现 HTTP 000，独立 HTTPS 读取也发生过连接重置；单文件复核成功，
最后以间隔请求补齐尚未通过的对象。先前已完整验证的对象没有重复计数。
这只能证明本轮文件与目录内容一致，不证明网络稳定性或具体失败原因。
原始失败日志及最终结果保存在 `.local/pico-storage-crash/`，
完整对象列表见 `owned-resources-verification.json`。

## 待验收

Pico 真机需验证三套列表的获取、下载安装和查词、
旧卡词典识别、取消/断网后的重试、默认字体下受支持字号的缺字补全，以及普通字体
管理和精排阅读的回归。编译、主机测试或电脑访问 COS 均不等于这些操作已验收。

## 用户授权刷入

随后用户明确要求刷入测试词典下载和查词。设备身份为已知 Read Pico，
MAC `14:c1:9f:d1:9f:58`；写前确认应用仍匹配上一份整合包 SHA256 `710094c3…`。
只写 app0（`0x10000`），上述新 BIN 的设备摘要校验通过，app0 之外的 Flash
摘要未变化，显式 USB 复位并正常恢复 `EpubReader`。没有操作 SD 卡。
本地证据：`.local/pico-storage-crash/install-owned-resources-round1/installation.json`
及同目录 `boot.log`。用户的词典下载/安装/查词验收仍待反馈，未发布。

刷入过程中用户另报关机壁纸无法自定义。检查发现现有图片选择只更新
`sleepScreen` 和 `rickyStandbyFace`，并不改 `rickyPowerOffScreen`；关机页只有
跟随待机、默认、空白，没有独立选图。现有用法是待机选择图片后将关机画面
设为跟随待机。此次资源迁移包没有修改该行为，尚未据此实施新的壁纸功能。
