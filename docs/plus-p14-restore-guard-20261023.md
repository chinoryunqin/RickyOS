# Plus 按键恢复失败时阻止 P14：离线修复记录

日期：2026-10-23。实际工作树 `/Users/rick/rickyos/wt-plus-battery`，电量分支。未操作设备、未刷机、未改充电参数、电量模型或保护阈值，未提交或发布。

## 修复范围

依据：[此前P14离线审计](plus-p14-reset-offline-audit-20261023.md)。原按键恢复忽略4次写入和2次输入读取的错误，方向/输出回读检查又在恢复电轨之后；调用返回不能证明恢复成功。

- 新增板级 `ioexp_restore_keys_checked()`，保持原厂的输出预装载、方向恢复、输入读取顺序。任何事务出错都保留失败结果，即使后续回读碰巧相同也不能放行。随后按寄存器对读取输出、极性及方向，验证真实值。
- 静态待机调用方接收这个结果；P14/P5恢复入口同时要求该结果、本轮成功标记及写电轨前的新回读一致。检查失败时不提交P14写入阶段、不写任何P14/P5恢复数据。
- 旧的无返回值按键/电轨接口也使用成功标记及回读，不能绕过检查。屏蔽按键、切断电轨、按键恢复开始或前置检查失败都会清除标记。
- 失败通过现有 `sleepFault` 锁存，`standbyStaticSleep()`返回失败，后续请求被拒绝；无自动重试。SD句柄/持有状态清理、USB及LCD总线资源恢复仍按已有路径尽力完成，**不能因此承诺电轨拒绝后显示或存储一定可用**。
- 正常路径仍按原厂先P14、后P5；保留static4已有40ms间隔，以隔离本次错误传递修复。该延时此前已失败，不能当作电气修复。

修改位置均相对 `ports/metalio-e-ink4-plus/firmware/rickyos-plus-1.2/`：

| 文件 | 改动 |
| --- | --- |
| `components/metalio_board/display/epd_board_metalio_eink4_plus.c/.h` | checked接口、成功标记、两个电轨入口的前置校验 |
| `components/rickyos_application/port/PlusBoard.cpp` | 传递恢复结果、保留失败及禁止再次睡眠 |

不增加堆分配。链接map中新增 `s_sleep_keys_restored` 为1字节静态BSS，回读使用已有6字节局部数组；其余新增状态为局部布尔值。修改位于Plus专用组件，没有改C3共享应用或SDK。

## 验证

1. 原厂与本地板级函数通过同一实际TCA/Wire封装回放，各128轮正常序列保持一致；另128轮新的checked+staged链通过。原厂同样的方向写入失败会继续恢复电轨，本地修复后拒绝。原厂传输层没有在电脑端执行，不能据此证明旧/新IDF时序等价。
2. 注入4个输出/方向写寄存器报错、2个输入读取报错及3组状态回读报错；模拟ACK成功但方向未生效；将输出/极性/方向的每个寄存器在checked之后改错。全部阻止电轨写入。前置检查失败后即使寄存器恢复，旧接口和显式传true也不能重新放行。
3. 调用方故障注入验证返回Failed、保留电轨未恢复状态、尝试收尾且锁存故障；再次请求不调用睡眠API。正常100轮、按键退出及既有失败回滚检查继续通过。
4. 新审计2项、静态待机6项、既有浅睡4项，共12项主机回归通过，使用ASan/UBSan。另前光面板脚本通过。源码检查通过：2844个固定文件、142项适配。
5. Plus静态诊断配置离线编译/链接通过；保留已有的 `esp_sleep_get_wakeup_cause()` 弃用警告。未运行整套端口回归，未刷机或做新实机验收。

离线构建日志 `.local/build-p14-restore-guard-offline-20261023.log`；产物与源码哈希 `.local/p14-restore-guard-offline-20261023/verification.json`。重用了本地static4编译目录及版本字符串，**产物已不同于安装过的static4，不能凭版本名认定是原试验镜像，也不作为安装授权**。原试验封存与turnC恢复记录另存，设备未改变。

复跑入口：

```sh
RICKYOS_PLUS_OEM_ROOT=/Users/rick/Documents/ChatGPT/rickyos/vendor/metalio-plus-2.0.76 \
  python3 ports/metalio-e-ink4-plus/tests/test_plus_p14_restore_audit.py -v
python3 ports/metalio-e-ink4-plus/tests/test_plus_static_sleep.py -v
python3 ports/metalio-e-ink4-plus/tests/test_plus_standby_sleep.py -v
```

这些检查验证错误处理，**没有证明本次电源毛刺复位已经解决**。

## 整板资料查找

截至2026-10-23，在本地原厂2.0.76目录、[厂商Plus公开仓库](https://github.com/CloudZao/Metalio-E-INK4-Plus)、其完整main文件树、分支与发布资产中，未找到整板原理图。公开main固定为 `b400605e9a6a276a2c39864c0cff63798c78e8d8`，文件树未截断；仅发现屏幕及TCA9555的两份PDF，只有main分支、无Releases。厂商账户可见的另外三个产品仓库也不是单独的Plus硬件资料仓库。该范围不代表厂商没有未公开或另册资料。

[厂商README的资料章节](https://github.com/CloudZao/Metalio-E-INK4-Plus#8-schematics-and-docs)只列软件资料，并将另册原理图指向硬件团队；没有可下载的整板图链接。另检索厂商官网和公开硬件平台，未找到匹配Plus板版的图纸。检索元数据保存在 `.local/p14-public-hardware-search-20261023.json`。

仍需硬件团队提供这台板版的电池→系统供电、P14负载开关、P13关机控制、P12触摸供电及I2C上拉供电网络，才能核实P14是否影响TCA/主控依赖。没有向厂商发送消息。

## CrossMux Metalio E-Ink 4 对照

用户提供的参考已核对到固定源码：CrossMux `eaf5c73a71aaa03e372b6bc274923c2cc621a033`，SDK子模块 `9aa7e95e02c6374a23a562c883eb302552ff24f1`。读取了适配文档、HAL电源代码及SDK `MetalioEInk4Board.h`，仅存只读参考于 `.local/crossmux-power-reference-20261023/`，没有合并或替换本地依赖。

| 参考点 | 源码依据 | 对Plus的意义 |
| --- | --- | --- |
| 带锁读取真实输出再修改单个位 | [SDK setOutput 44–55行](https://github.com/0x1abin/freeink-sdk/blob/9aa7e95e02c6374a23a562c883eb302552ff24f1/libs/hardware/BoardConfig/include/MetalioEInk4Board.h#L44-L55) | 值得参考其所有权和保留其他位的方式；Plus还需单独审计缓存所有者，不能直接将16位整口写替换进来 |
| 初始化遇到I2C错误返回失败，成功后才置ready | [SDK ensureBooted 58–92行](https://github.com/0x1abin/freeink-sdk/blob/9aa7e95e02c6374a23a562c883eb302552ff24f1/libs/hardware/BoardConfig/include/MetalioEInk4Board.h#L58-L92) | 与本次“失败不得被当成功”的修复方向一致；初始化与反复睡眠恢复的状态机不同 |
| 先收尾屏幕/存储，再请求板级关机，失败有深睡回退 | [SDK powerOff](https://github.com/0x1abin/freeink-sdk/blob/9aa7e95e02c6374a23a562c883eb302552ff24f1/libs/hardware/BoardConfig/include/MetalioEInk4Board.h#L125-L143)、[HAL电源入口](https://github.com/0x1abin/crossmux/blob/eaf5c73a71aaa03e372b6bc274923c2cc621a033/lib/hal/HalPowerManager.cpp#L278-L286) | 可参考分阶段收尾；Plus的P17唤醒/关机通路不同，不能直接复制S3回退 |
| 当前Metalio没有进入该HAL的通用计时器轻睡眠通路 | [standbyRetention与资格检查](https://github.com/0x1abin/crossmux/blob/eaf5c73a71aaa03e372b6bc274923c2cc621a033/lib/hal/HalPowerManager.cpp#L47-L59)、[canStandbyLightSleep](https://github.com/0x1abin/crossmux/blob/eaf5c73a71aaa03e372b6bc274923c2cc621a033/lib/hal/HalPowerManager.cpp#L320-L331) | retention只给X3/X4，其他profile为未验证；因此它不能作为Plus反复计时器浅睡已可用的证据 |

硬件差异不止CPU：[该项目文档](https://github.com/0x1abin/crossmux/blob/eaf5c73a71aaa03e372b6bc274923c2cc621a033/docs/engineering/metalio-eink4.md)描述S3/SPI SSD1677，Plus是S31/并行EPDiy+TPS；MAIN/P0.6、触摸复位P11、GPIO3电源键和Plus的P14、P12、P17不同。其当前SDK初始化甚至将P13关机控制预置高，Plus原厂则预置低，不能按“同名产品”直接套整口值。

文档开头明确区分当前适配与历史校准/实机记录，历史结果不能转移到当前代码。这里借鉴的是可核查的实现和失败处理，不据此宣称Plus电气问题已经解决，也未引用或改动它的充电策略。
