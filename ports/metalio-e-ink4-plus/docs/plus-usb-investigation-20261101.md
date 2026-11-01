# Plus USB 拖拷调查（2026-11-01，按用户要求暂缓）

当前设备为1.2.13-bt10。用户反馈Finder从电脑拖入设备时一直卡住。
本轮没有改动USB固件，没有放大任何内存，也没有宣称修复。

同次连接实测：4KiB读写/同步成功；16MiB写入并fullsync约5.694s，立即回读一致
（该次回读不能当冷读速度）；200个8KiB小文件载荷全部核对一致，清理仅删除自建测试文件。
8MiB新文件卸载再挂载后回读SHA256一致，读取约0.673s。
原始记录见output/device-tests/plus-page-turner-bt10-20261101/usb-host-baseline.jsonl。
首次批量测试脚本错误地遍历macOS自动删除的AppleDouble文件而报ENOENT，已修正清理
方式并核对余下199份数据；这不是设备载荷校验失败。

用户当时拖入的《火与剑》1,768,639B，与Downloads源文件SHA256完全一致。
Finder样本显示目录同步线程位于getattrlistbulk，复制helper等待；不能据此断定根因
是Finder或卡，也不能把“数据已完整”写成“UI拖拷已修复”。
发现一个先前的日文长文件名可列出但stat返回ENOENT（NFC/NFD也失败），关系未证实。
随后diskutil verifyVolume调用fsck_msdos -n exit0，未修复/格式化卡，不能称卡已损坏。

源检查：USB已在入口停止BLE，MSC用固定4KiB内部DMA端点缓冲，SDMMC复用预先申请的
DMA bounce缓冲；数据路径没有逐次累积申请。这不能排除并发访问、驱动或特定会话问题。
native USB backend与retry回归通过，不替代硬件组合验证。

用户要求“如果能暂时正常用先这样，准备发布版本”。因此暂停进一步USB实验，发布说明
不写USB修复；发布最终整包仍须做基本传书检查。详情JSON：usb-investigation-status.json。
