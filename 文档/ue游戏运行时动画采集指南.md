# Unreal Engine 运行时动画采集指南

从 PBZ V6 提炼的迁移流程。适用于你有权调试的 UE4／UE5 项目、测试构建和支持本地 Mod 的游戏。先验证目标构建实际能提供哪些字段，再选择采集入口。

## 1. 选择采集入口

| 条件 | 先用什么 | 验证结果 |
|---|---|---|
| 有项目源码、编辑器或开发构建 | 用 Animation Insights／Rewind Debugger 看动画轨道。自定义状态在实际写入或求值处补项目日志或 Trace 事件。 | 做一次已知动作，打开 trace，找到目标角色的 Montage、Sequence、Blend 或 Notify 轨道。 |
| 游戏支持标准 Trace，但只有打包客户端 | 验证 `Animation`、`Object`、`Frame` 等频道。 | 确认测试动作确实出现在动画轨道中。非空文件仅表明有事件写入。 |
| 可使用匹配构建的 UE4SS，所需字段可反射 | 以世界时钟逐帧采样，辅以 UFunction 回调，输出 JSONL。 | 先通过空闲、单一动作和动作切换三组预检。 |
| 需要混出实例、输入缓冲写入、实际 Warp 或 IK 求值 | 在项目源码或匹配构建的调试观测面上记录。V6 的原生读取器可作验证流程参考。 | 分字段验收，例如同帧的新旧实例，或同一次 Warp 求值的成对输入输出。 |

Animation Insights 的轨道随 UE 版本、构建和频道配置而变。自有项目可先开启插件并用 Animation 预设验证。支持 `-tracefile` 的 Windows 构建可以用下面的最小示例，先建输出目录，再使用该构建正常的启动入口。 [Animation Insights](https://dev.epicgames.com/documentation/en-us/unreal-engine/animation-insights-in-unreal-engine) · [Unreal Insights 命令行与频道](https://dev.epicgames.com/documentation/en-us/unreal-engine/unreal-insights-reference-in-unreal-engine-5)

```powershell
& 'D:\TestGame\Binaries\Win64\TestGame.exe' '-trace=Object,Animation,Frame,Bookmark' '-tracefile=D:\TestGame\Saved\Profiling\animation-smoke.utrace'
```

先用一个已知支持的 UE 开发构建验证查看器和输出链路，再查目标进程是否收到参数，以及目标动作的动画事件是否存在。若目标版本支持，可用 `Trace.Status` 辅助查看连接和频道。PBZ V6 实际使用 UE4SS Lua 加匹配构建的原生读取器，输出 JSONL。 [Unreal Trace 指南](https://dev.epicgames.com/documentation/en-us/unreal-engine/trace-in-unreal-engine-5)

## 2. 新游戏适配顺序

1. **固定构建。** 记录游戏版本、构建 ID、UE 版本、架构、实际引擎 EXE 路径及 SHA-256。使用 UE4SS 时再记录适配版本、DLL 哈希和启动参数。给每个目标建立独立工作目录。游戏或加载器更新后重做预检。
2. **确认角色和时钟。** 从正确世界的本地 PlayerController 定位 Pawn、实际显示的 SkeletalMeshComponent 和主 AnimInstance。记录完整路径并核对归属。换角色、读档重建对象、网格或实例变化时重新解析。按需要加 Linked AnimInstance、后处理实例和移动组件。
3. **先跑最小预检。** 在游戏线程读引擎帧号、世界游戏时间、活跃 Montage、Section、位置和速率。验证空闲 → 单一动作 → 动作结束，再加入 Slot、骨骼、移动模式与业务字段。纯 Sequence、BlendSpace、Pose Search 或状态机路径应检查 Insights 或实际 AnimGraph 资产播放器节点。
4. **验证每个事件钩子。** 用已知会发生的动作核对是否触发、对象是否属于主角、参数是否可读。事件附加递增序号。UE4SS 的 `RegisterHook` 对反射 UFunction 的前后时机依路径而异。使用帧回调时检查游戏适配版的 EngineTick 能力。 [UE4SS RegisterHook](https://docs.ue4ss.com/dev/lua-api/global-functions/registerhook.html) · [UE4SS 游戏线程帧回调](https://docs.ue4ss.com/lua-api/global-functions/delayedactions.html)
5. **需要时加原生读取。** 优先使用匹配构建的类型与 API。采用 V6 式布局读取时，把读取器限定在已验证的游戏和加载器构建。先验空列表，再验单实例与公开查询一致，最后验新旧实例同帧并存。布局、线程或读数校验失败时，把状态写入输出，其余可读字段可继续采集。
6. **短测试后再采正式流程。** 每个关键字段先用一次明确的动作确认出值。正式录制覆盖开始、过渡和结束。未触发的分支另测。全骨骼采样可单独做一轮，先比较采样前后的帧耗时。

回调无样本时，先核对对象、动作分支和实际调用路径。PBZ V6 的两个选桩钩子成功注册，正式样本中都没有回调。后续 PBZ 静态报告记录了 `EX_CallMath` 调用路径与 `LGATimeline` 速率曲线，为两个现象提供了线索。换游戏时也应分别注明配置来源、运行时读数和画面推断。 [PBZ 静态逻辑与 V6 核对](./PBZ跳桩完整逻辑导出与V6核对报告.md)

## 3. 字段与采样位置

| 字段 | 采样位置与验收办法 |
|---|---|
| 资产路径、Montage、Section、播放位置 | 从主 AnimInstance 的公开活动查询或 Insights 动画轨道读取。在明确动作中验证出现与推进。标注游标属于 Montage、Sequence 还是状态机。 |
| 新旧实例、真实权重、Blend 时长 | 在明确的更新阶段读完整 Montage 实例列表。记录实例 ID、位置、速率、播放状态、当前与目标权重、Blend 时长、余量和曲线。一次切换里验收新旧实例并存，跟踪旧实例的位置与权重直至移除。 |
| Slot 和逐骨有效贡献 | Slot 接口报告 Slot 级权重。逐资产和逐骨贡献分别在 Slot 求值、归一化、动画图分层与 Blend Profile 等实际阶段取样，并注明作用域。 |
| 通知、同步标记和接触 | 记录实际触发的通知、来源实例及位置。资产标记、运行时触发、胶囊命中、足底接触各留独立字段。落地命中要能关联目标 Actor 或组件与撞击点。 |
| 输入、缓存和选择 | 输入接收、缓冲写入／消费／清空和目标选择分别记录，使用输入序号和请求 ID 串联。用提前输入、覆盖、无输入和不可达目标分别验证。帧间快照有可能漏掉同一回调内的短暂状态。 |
| Root Motion、Warp、实际移动 | 分别记录资产区间提取、运行时提取、Warp 输入与输出、世界空间转换及移动后的胶囊。用同一求值或子步序号配对，写清坐标空间、单位和旋转表示。 |
| 骨骼、镜像、脚锁 | 记录骨骼名或索引和变换空间。精确脚锁在实际执行的节点求值处记录目标、支撑状态、权重和前后姿态。将当前求值结果、缓存姿态和前一帧结果明确标记。 |

公开活跃查询在 V6 中只报告一个当前 Montage。原生实例的 `weight`、Slot 汇总和逐骨有效贡献各有独立含义。即使两个实例播放同一资产，也应按实例 ID 跟踪。 [Epic Montage 实例](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Engine/FAnimMontageInstance) · [Epic 动画代理与 Slot 求值](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Engine/FAnimInstanceProxy)

## 4. 数据契约和完整流程验收

| 输出 | 一行表示什么 | 建议字段 |
|---|---|---|
| `animation-<ID>.jsonl` | 一个游戏帧的采集快照 | `game_frame`、`game_time_s`、`sample_phase`、`pawn`、`anim_instance`、活跃动画和实例列表或读取状态 |
| `animation-<ID>-events.jsonl` | 一次回调、写入或求值事件 | 帧号、事件自身的游戏时间、`event_sequence`、阶段、完整符号、对象归属、参数或结果 |
| `animation-<ID>-pose.jsonl` | 可选的姿态快照 | 帧号、阶段、变换空间、骨骼表、位置、四元数和缩放 |
| 采集清单 | 一次采集的来源 | 游戏和工具构建、动作范围、字段可读状态、采样阶段、输出文件及哈希 |

用 `null` 表示缺失或不可读，用 `[]` 表示成功读取的空列表，`0` 和 `false` 保留实测语义。多世界或多角色时加世界和对象标识。进程内指针只在本次运行内联表。跨采集按角色、资产路径、动作触发点和对应阶段对照。

- 沿用 V6 启停时，用户确认后写唯一 ID 的 `capture.start`，确认状态为 `Capture active: <ID>` 且帧文件有新行，再通知用户操作。结束时向 `capture.stop` 写同一 ID，等 `Capture finished: <ID>` 后导出。标记用无 BOM UTF-8，可用同目录临时文件原子替换。
- 以采集 ID、世界和 `game_frame` 关联帧首快照与回调。同帧事件按 `event_sequence` 排序，保留两个采样阶段各自的时间。换成 Tick 后或并行动画求值阶段时，按实测时机标注。
- 检查逐帧数据有无缺帧、重复项和时间回退。计算 `Δposition / Δgame_time` 时用同一实例、相邻帧和正的游戏时间差，排除跳段、循环与显式重定位。持平时间保留原样。
- 完整路线从开始事件到结束事件，保留区间内的无事件帧和空动画帧。导出逐帧、事件、逐实例、分段和完整路线 CSV。人工阶段标签单独存为配置，事件原始参数可按序号回查。
- 正式交付保留原始输入和导出器。用封存数据重建输出，比较行数、关键帧和哈希。完整动作路线的验收与缓存、接触、候选等专项字段的验收分别进行。

## 5. V6 的哪些文件可以复用

V6 的源码、原始样本与既有运行说明均在 [V6 交接包](../Saved/PBZTraceWork/PBZ-PlumPole-Handoff-20261125-114044-194.zip)。

| 文件 | 可复用的部分 | 换游戏时要适配的部分 |
|---|---|---|
| `tools/main.lua` | 时钟、启停、JSONL、阶段标签与事件序号 | 工作目录、角色和网格识别、硬编码的 PBZ 资产预检、哪些回调附带实例列表 |
| `tools/pole_observer.lua` | 角色归属检查、Transform 与命中结果序列化 | 能力类和属性名、骨骼、Slot、Warp 组件与回调路径；按新游戏实现同一数据契约 |
| `tools/montage_reader.cpp` 与原生 DLL | 完整实例列表、公开 getter 交叉核对、错误状态和测试夹具的设计 | PBZ 对象布局、镜像校验、加载器 Lua 接口和线程条件，须按新构建重新验证 |
| `tools/layout_preflight.lua` 与 `inspect_*.py` | 反射清单、入口验证和静态证据的检查方法 | 类、类型、属性、镜像和依赖 |
| `tools/export_animation_run.py` | 逐帧、事件、实例和分段导出 | 代码内的 PBZ 来源文字与跳桩列，改用新游戏字段和构建信息 |
| `tools/export_full_route.py` | 按阶段配置生成完整时间轴 | 采集 ID、阶段范围、重点回调和输出列。当前实现允许至多两个同时混出的实例，更多时升级速览列或改用长表形式 |

取 V6 包练习一次重建导出。需要 Python 3.10+，命令从解压根目录执行。

```powershell
python .\tools\export_animation_run.py .\data\animation-20261125-114044-194.jsonl
python .\tools\export_full_route.py .\data\animation-20261125-114044-194.jsonl .\reference\V6_ROUTE_PHASES.json
```

预期 **2,221 个逐帧样本、3,988 条事件、581 行 Montage 实例、423 行完整流程**。其中 145 帧有多个 Montage 实例。31468–31475 是旧实例在权重下降时继续推进的实测例子。 [V6 实测报告](../Saved/PBZTraceWork/UE4SSPreflight-26367565/V6/PBZ_PLUMPOLE_V6_REPORT.md)

## 转交给下一任务的提示词

> 请用这份指南为目标 UE 游戏建立可重复的运行时动画采集。先确认目标构建、实际引擎进程、目标角色和动作场景。按所需字段选择标准 Trace、反射采样或经验证的原生读取，先验收空闲、单一动作和一次混合。若需要逐实例数据，务必验证同帧的新旧实例 ID、位置和实际权重。正式启停等我的明确指令。交付原始 JSONL 或 Trace、逐帧和逐事件导出、逐实例表、完整动作路线 CSV、采集工具与字段说明。注明缺失项、推断和采样阶段，以目标游戏的实测字段作为验收依据。
