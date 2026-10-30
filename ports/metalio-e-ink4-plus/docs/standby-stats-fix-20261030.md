# 待机设置与统计触控修正（2026-10-30）

基于 `plus-v1.2.11` / `9302f06e` 的独立分支 `codex/plus-reader-bindings-20261030`，保留本分支新增的查看目录、加入书签绑定，不合入注解等其他开发分支。

## 行为

- 暂停锁屏右滑解锁：移除系统手势页入口和待机解锁输入路径。待机仍可通过原有实体按键退出，普通页面的右滑返回不变。
- `standbySwipeUnlock` 仅作为兼容字段保留。加载旧设置时无条件置 0；旧值开启或类型非法时调用既有 `requestResave()` 流程保存关闭值；序列化固定输出 0。产品布局约束和旧二进制设置加载也将其固定为 0。
- 移除该字段对 `StandbyActivity::tryLightSleep` 的阻拦。原开启值曾让 Plus 直接跳过轻睡；不声称由此已测得具体功耗改善。
- 自动待机、待机后自动关机集中到「设置 → 显示与待机」。壁纸页只保留外观配置，两个时间控件移出。
- 沿用 `sleepTimeoutMinutes` 与 `rickyAutoOffIndex`，原值和时间语义保持：闲置 1–30 分钟／从不待机；待机后从不／1／3／6／12／24 小时关机。自动待机复用现有分钟选择器，可保留以前设置的非预设分钟数。
- 阅读统计页每个 provider 行填写独立 `actionValue`。此前 ListItem 默认为 0，导致三行命中均打开每日目标；现在每日目标打开选项，启用成就与成就弹窗各自切换对应开关。

## 依据

- `upstream/src/CrossPointSettings.cpp`：加载、序列化、产品固定设置约束。
- `upstream/src/activities/apps/standby/StandbyActivity.cpp`：轻睡门槛与待机输入。
- `upstream/src/activities/settings/SettingsActivity.cpp`：此前产品分类过滤掉 `sleepTimeoutMinutes`。
- `upstream/src/activities/apps/standby/RickyStandbySettingsActivity.cpp`：此前两个时间控件位于壁纸页底部。
- `upstream/src/activities/settings/ReadingStatsSettingsActivity.cpp`：行 provider 缺少 actionValue。
- `upstream/freeink-sdk/libs/ui/FreeInkUI/include/components/lists/list.h`：ListItem 的 actionValue 默认为 0，list() 按该值注册点击。

以上路径相对 `firmware/rickyos-plus-1.2/`。

## 资源

没有新任务、帧缓冲或常驻活动。新增关机时间目录项使用 `SettingInfo::StaticEnum`，六个 StrId 标签为编译期静态数组，不创建 enumValues 堆向量。通用设置目录及当前分类的既有容器各增加至多一个 `sizeof(SettingInfo)` 的项，供持久化和设备页共用；不另建页面或复制时长状态。壁纸移除原时长行及 24 字节文本缓冲。未改变书籍二进制缓存格式。

## 验证

- `test_plus_standby_swipe_unlock.py`：真实 ArduinoJson 与生产迁移片段覆盖旧开启值、布尔、负值、越界、字符串；关闭后再次加载无需迁移。生产待机输入覆盖残留开启值、全局手势组合、实体 Back 和触摸返回隔离。
- `test_plus_standby_sleep.py`、`test_plus_static_sleep.py`：生产睡眠逻辑、各外围设备门槛、计时唤醒和残留旧开关值不再阻止轻睡。
- `test_power_settings_location.py`：执行生产目录项和分类过滤器，两个时间项均保留在 Display 分类；键、字段、范围、小时选项与已保存数值不变；壁纸不再注册时间控件。
- `test_stats_settings_touch.py`：生产行 provider、实际 FreeInkUI 列表／命中路由、生产选择处理器；横竖屏三行的左、中、右位置分别触发正确设置，空白不弹每日目标。
- 原有待机 Home、日历方向、阅读九宫格、五键绑定和书签／目录动作回归通过。
- 固件版本 `1.2.11-standby-fix` 完整构建通过，沿用验收显示参数和 IDF `9a97f6c5`。镜像身份见 `standby-stats-fix-build-20261030.json`。

## 真机验收

1. 升级前曾开启右滑解锁的设备，升级后入口消失；进入待机右滑不能退出，实体键仍可退出；重启后保持关闭。
2. 显示与待机中修改两个时间，返回、重启后数值保留；壁纸页没有重复时间项。
3. 阅读统计中点击成就两行的文字、空白与开关，只有对应开关改变；每日目标只从第一行打开。

构建及主机测试不替代真机功耗和交互验收。未提交、未推送、未发布；刷机状态以构建记录为准。

## 安装记录

已在同一台 ESP32-S31 Plus 的 app0（0x10000）安装本镜像，写入摘要校验通过，应用槽以外 Flash 与备份逐字节一致。显式重启后串口确认 1.2.11-standby-fix、匹配 ELF 95978997、IDF 9a97f6c5，并进入 InxRecent；首帧和区域刷新 result=0x0，35 秒采集窗口未见崩溃。旧开关迁移、两个时间设置入口与统计触控的实际交互及功耗仍待用户验收。
