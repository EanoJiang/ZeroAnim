# PBZ 跳桩完整逻辑静态导出与 V6 实测核对报告

## 1. 导出概览与数据来源

基于 [PBZ 主资源包与跳桩衔接调查.md](file:///D:/UE58Projects/ZeroAnim/文档/PBZ%20主资源包与跳桩衔接调查.md) 中的信息，本次直接使用 CUE4Parse（开启 `ReadScriptData = true` 反编译 Blueprint Kismet 字节码）配合 `ProjectPBZ-5.7.4-302256-full.usmap`（UE 5.7）从原版发行包 `E:\SteamLibrary\steamapps\common\PBZ\ProjectPBZ\Content\Paks\ProjectPBZ-Windows.utoc/.ucas` 静态导出了全部跳桩核心非美术资产（JSON + 字节码 + `LGATimeline` 轨道与 `FloatCurves`），并与 V6 运行时采集包 [PBZ-PlumPole-Handoff-20261125-114044-194.zip](file:///D:/UE58Projects/ZeroAnim/Saved/PBZTraceWork/PBZ-PlumPole-Handoff-20261125-114044-194.zip) 进行了逐字段、逐帧、逐事件核对。

- **静态导出目录**：[StaticPoleExport](file:///D:/UE58Projects/ZeroAnim/Saved/PBZTraceWork/StaticPoleExport/)（[manifest.json](file:///D:/UE58Projects/ZeroAnim/Saved/PBZTraceWork/StaticPoleExport/manifest.json)、[index_focused.txt](file:///D:/UE58Projects/ZeroAnim/Saved/PBZTraceWork/StaticPoleExport/index_focused.txt)）
- **核心导出资产（节选）**：
  - 能力蓝图（37 函数字节码 + 14 条 `LGATimeline`）：[GA_StandingOnPole.json](file:///D:/UE58Projects/ZeroAnim/Saved/PBZTraceWork/StaticPoleExport/json/Game_RenewalContent_Actors_SpecialMove_GA_GA_StandingOnPole.json)
  - 入桩区域触发体：[BP_LionDance_Area.json](file:///D:/UE58Projects/ZeroAnim/Saved/PBZTraceWork/StaticPoleExport/json/Game_PBZContent_Blueprint_TriggerVolume_BP_LionDance_Area.json)
  - 木桩基类 / 出口桩 / 变体 / 防误跳保护体：[BP_LionDancing_Pole_Base.json](file:///D:/UE58Projects/ZeroAnim/Saved/PBZTraceWork/StaticPoleExport/json/Game_RenewalContent_Actors_SpecialMove_Volume_BP_LionDancing_Pole_Base.json)、[BP_LionDancing_Pole_Exit.json](file:///D:/UE58Projects/ZeroAnim/Saved/PBZTraceWork/StaticPoleExport/json/Game_RenewalContent_Actors_SpecialMove_Volume_BP_LionDancing_Pole_Exit.json)、[BP_PoleJumpOutProtectVolume.json](file:///D:/UE58Projects/ZeroAnim/Saved/PBZTraceWork/StaticPoleExport/json/Game_PBZContent_AI_BP_PoleJumpOutProtectVolume.json)
  - 辅助 Timeline Action：[GAA_MoveMode.json](file:///D:/UE58Projects/ZeroAnim/Saved/PBZTraceWork/StaticPoleExport/json/MagCombatAbilities_Actions_GAA_MoveMode.json)、[GAA_SetCameraAdditiveParameterDistance.json](file:///D:/UE58Projects/ZeroAnim/Saved/PBZTraceWork/StaticPoleExport/json/MagCombatAbilities_Actions_GAA_SetCameraAdditiveParameterDistance.json)、[GAA_SetCameraAdditiveParameterSocketOffset.json](file:///D:/UE58Projects/ZeroAnim/Saved/PBZTraceWork/StaticPoleExport/json/MagCombatAbilities_Actions_GAA_SetCameraAdditiveParameterSocketOffset.json)
  - 全部 12 个跳桩 `AnimMontage`（含常规与狮头 `B` 版）及角色动画蓝图：[PBZ_ABP_Hun_Base.json](file:///D:/UE58Projects/ZeroAnim/Saved/PBZTraceWork/StaticPoleExport/json/HuaShira_Characters_Hero_Hero_Hun_Animation_PBZ_ABP_Hun_Base.json)、[ABP_Hun_PostProcess.json](file:///D:/UE58Projects/ZeroAnim/Saved/PBZTraceWork/StaticPoleExport/json/HuaShira_Characters_Hero_Hero_Hun_Rig_ABP_Hun_PostProcess.json)

---

## 2. 静态导出 vs V6 实测：不一致与差异汇总（重点）

总体结论：**核心动画资产、混合参数、起播时间、变速节奏、飞行模式与 Warp 窗口高度吻合**；但静态字节码暴露出 **7 处 V6 未能观察到、存在表层误读或存在精确数值偏差的关键差异**：

| # | 核对项 | V6 实测记录 / 报告结论 | 静态导出完整真相 | 性质与成因 |
|---|---|---|---|---|
| **1** | **下桩目标（`f=31773`）** | 报告将其归类为“**同桩请求**”，`current_standing_pole` 与 `last_requested_target_pole` 均停留在末根普通桩（`dist2d=0, dz=0`），只观测到 `Land` 落点低 `110.69 cm` | 实际目标是场景中放置的隐藏出口桩 **`BP_LionDancing_Pole_Exit_C`**（`bHidden=true`，`SM_WoodPost_001` 下沉 `-500 cm` 使 `StandPoint` 贴地），水平距离 **`154.75 cm`**、高度差 **`-110.69 cm`** | **V6 表层误读**：`MoveToPole` 检测到目标类为 `BP_LionDancing_Pole_Exit_C` 时走独立分支，**直接跳过更新 `LastRequestedTargetPole` 与 `CurrentStandingPole`**，导致状态快照保留旧指针 |
| **2** | **播放速率 `PlayRate` 与混出掉速机制** | 全部 581 条原生实例行的 `play_rate` 字段恒为 **`1.0`**，但实测 `dpos/dt` 在 `1.025–2.87×` 变化；混出旧实例掉回 **`1.000 s/s`**，而下桩 Inst 24 尾段却维持 **`1.025 s/s`** | `AnimMontage` 自身 `PlayRate` 确实恒为 `1.0`，实际由 `LGATimeline` 的 **`FloatCurves["Rate"]`** 曲线外部驱动推进；切换 Timeline 后旧 Timeline 终止驱动，故旧实例退回原生 `1.000 s/s`；Inst 24 未被打断，`JumpToPlatform_L` 存活至 `1.9167 s`，故尾段保持曲线末值 **`1.025×`** | **机制补全**：解释了 V6 `play_rate=1.0` 与实际速率不一致的根本原因，实测曲线逐帧吻合度达 `0.0017–0.0094×` 平均误差 |
| **3** | **SkewWarp 窗口结束时刻** | V6 实测结束时刻：`Dodge` `1.092892 s`、`Jump_L/JumpToPlatform_L` 段 1 `0.499159 s` / 段 2 `1.249176 s`、`Jump_R` 段 1 `0.499159 s` / 段 2 `1.249177 s` | 静态 Timeline 轨道 `StartTime + duration`：`Dodge` `1.092992 s`、`Jump_L/JumpToPlatform_L` 段 1 `0.499259 s` / 段 2 `1.249276 s`、`Jump_R` 段 1 `0.499259 s` / 段 2 `1.249277 s` | **系统性 `-0.0001 s`（0.1 ms）偏差**：`LGAAction_MotionWarping` 生成运行时 `SkewWarp` Modifier 时统一对 `EndTime` 减去 `KINDA_SMALL_NUMBER`（`0.0001 s`） |
| **4** | **`Apex` Warp 目标** | V6 报告正文与 `warp_land_targets_json` 仅提及 **`Land`** 目标 | `CalcWarpingTarget` 每次同步注册 **`Apex`** 与 **`Land`** 两个目标（复核 V6 `motion_warping_snapshot_json` 原始快照确认 `Apex` 确实存在且符合静态公式），但全部 14 条 Timeline 均**只消费 `Land`** | **V6 报表过滤省略**：`Apex` 为未消费的冗余目标，复刻时无需处理 |
| **5** | **`Buffered` 恒为 `false` 与选桩钩子 0 回调** | V6 2,221 帧 `Buffered` 全 `false`；已挂钩的 `PickPoleJumpTarget` / `SplitActorsByPlayerCanJump` 返回 0 条事件 | `Buffered` **仅由 `OnDash`（闪避/戴狮头跳）和攻击输入在 `Action.Phase.Act` 期置位**，方向键 `OnMove` 从不写 `Buffered`（仅在 `Combo` 开启首帧直接命中）；静态库函数走 `EX_CallMath` 绕过 UE4SS `ProcessEvent` 钩子 | **V6 采样路径与钩子盲区**：静态字节码完整补出了 `OnMove`（`350 cm / 90° / 最近`）、`OnDash`（`500 cm / 90° / 最远`）与入桩 `JumpAction`（`500 cm / 90° / 最近`）的精确参数 |
| **6** | **V6 单次路线未触发的 5 条分支** | V6 仅观测到：站桩/入桩 `Dash`（`0.0 s` 起）、站桩下桩 `JumpToPlatform_L`（`0.20 s` 起、自然播完到 `1.9162 s`） | 静态导出补全：① **连跳直出 `Dash` 起播 `0.20 s`**；② **连跳直出 `JumpToPlatform_L/R` 起播 `0.32 s`**；③ **第二套下桩 `JumpOff`**（按 `IA_Jump` 或 `Abs(DeltaZ) >= 200 cm`，播 `JumpOut_R_Montage` + `GravityScale=0.5`）；④ **`CancelByMoveInput` 提前打断**（`JumpToPlatform_L/R` 在 `1.2621 / 1.2636 s`、`JumpOff` 在 `1.5255 s`）；⑤ **跳攻 `JumpOutAttack`、受击 `Hit_L/R`、破防 `Break` 与狮头 Boss 变体** | **V6 路线覆盖盲区**：复刻完整能力必须补齐上述分支 |
| **7** | **`Jump_R` 长度瑕疵 & 脚锁/LegIK 语义** | V6 观测到 `OnLanded` 命中点比 `StandPoint` 低 `5.57–5.58 cm`，报告标注“脚部接触与脚锁仍需确认”；Inst 22 实测播到 `2.1666 s` | ① `GA_StandingOnPole` 激活期强制授予 **`CharacterState.Animation.DisableLegIK`**，在 `PBZ_ABP_Hun_Base` / `ABP_Hun_PostProcess` 中直接**关闭 LegIK / FootPlacement**，桩上不做脚锁；② 静态 `Jump_R` Timeline 的 `SequenceLength` 误写为 **`2.0 s`**（`Jump_L` 为 `2.1667 s`），但 `1.6401 s` 即切 `Idle_L`，之后 Montage 脱离 Timeline 自然播完至 `2.1666 s`，与 V6 一致 | **原版机制确认 + 资产 authoring 细节** |

---

## 3. 静态导出 vs V6 实测：高精度吻合项逐条验证

### 3.1 Montage 资产与混合配置（100% 吻合）

所有 12 个跳桩相关 `AnimMontage` 资产内部均含 **0 条 Montage Notify**，且 V6 采到的 6 个资产与静态导出完全一致：

| Montage 资产 | SequenceLength | BlendIn（时间 / 曲线） | BlendOut（时间 / 曲线） | AutoBlendOut | Slot | Notifies |
|---|---:|---|---|---:|---|---:|
| `Hun_LionHead_Dodge_F_R_Montage` | `1.966667 s` | `0.25 s` / `HermiteCubic(2)`（默认） | `0.25 s` / `HermiteCubic(2)`（默认） | `true` | `DefaultSlot` | 0 |
| `Hun_LionHead_Battle_Walk_Fwd_L_Montage` | `2.166667 s` | `0.15 s` / `CircularIn(8)` | `0.25 s` / `HermiteCubic(2)`（默认） | `true` | `DefaultSlot` | 0 |
| `Hun_LionHead_Battle_Walk_Fwd_R_Montage` | `2.166667 s` | `0.15 s` / `CircularIn(8)` | `0.25 s` / `HermiteCubic(2)`（默认） | `true` | `DefaultSlot` | 0 |
| `Hun_LionHead_JumpOut_L_02_Montage` | `1.916667 s` | `0.15 s` / `CircularIn(8)` | `0.30 s` / `CircularIn(8)` | `true` | `DefaultSlot` | 0 |
| `Hun_LionHead_JumpOut_R_02_Montage` | `1.916667 s` | `0.15 s` / `CircularIn(8)` | `0.30 s` / `CircularIn(8)` | `true` | `DefaultSlot` | 0 |
| `Hun_LionHead_JumpOut_R_Montage`（`JumpOff`/`Break`） | `2.666667 s` | `0.25 s` / `HermiteCubic(2)`（默认） | `0.25 s` / `HermiteCubic(2)`（默认） | `true` | `DefaultSlot` | 0 |
| `Hun_LionHead_Battle_Idle_L_Montage` | `6.700000 s` | `0.50 s` / `HermiteCubic(2)` | `0.25 s` / `HermiteCubic(2)`（默认） | `false` | `DefaultSlot` | 0 |
| `Hun_LionHead_Battle_Idle_R_Montage` | `6.600000 s` | `0.50 s` / `HermiteCubic(2)` | `0.25 s` / `HermiteCubic(2)`（默认） | `false` | `DefaultSlot` | 0 |

### 3.2 `LGATimeline` `"Rate"` 曲线逐帧核对（全路线 11 个实例）

用静态导出的 UE `FRichCurve`（Hermite Cubic / Linear）在中点求值并与 V6 `montage-instances.csv` 全部活跃帧 `dpos/dt` 比对：

| V6 实例 | 对应 Timeline | 活跃步数 | 区间中点平均绝对误差 | 最大绝对误差 | 混出阶段 `dpos/dt` 实测 | 核对结论 |
|---|---|---:|---:|---:|---|---|
| Inst 13 | `Dash`（地面入桩） | 47 | `0.00194×` | `0.00257×` | 立即降为 `1.000 s/s`（8 步） | 精确吻合 |
| Inst 14 | `Jump_L`（连跳） | 24 | `0.00936×` | `0.01901×` | 立即降为 `1.000 s/s`（8 步） | 精确吻合 |
| Inst 15 | `Jump_R`（连跳） | 24 | `0.00927×` | `0.01891×` | 立即降为 `1.000 s/s`（7 步） | 精确吻合 |
| Inst 17 | `Jump_L`（停桩） | 41 | `0.00582×` | `0.01987×` | 首步 `1.025`，余下 29 步 `1.000` | 精确吻合 |
| Inst 18 | `Idle_R` | — | 无曲线（恒定 `1.0×`） | — | `1.000 s/s`（15 步） | 精确吻合 |
| Inst 19 | `Dash`（远跨 `441.9 cm`） | 60 | `0.00171×` | `0.00252×` | 首步 `1.025`，余下 34 步 `1.000` | 精确吻合 |
| Inst 20 | `Idle_L` | — | 无曲线（恒定 `1.0×`） | — | `1.000 s/s`（6 步） | 精确吻合 |
| Inst 21 | `Jump_L`（站桩起跳） | 21 | `0.00922×` | `0.02068×` | 立即降为 `1.000 s/s`（7 步） | 精确吻合 |
| Inst 22 | `Jump_R`（连跳后停桩） | 43 | `0.00589×` | `0.01999×` | 首步 `1.025`，余下 `1.000` 至片尾 `2.1666 s` | 精确吻合 |
| Inst 23 | `Idle_L` | — | 无曲线（恒定 `1.0×`） | — | `1.000 s/s`（9 步） | 精确吻合 |
| Inst 24 | `JumpToPlatform_L`（下桩） | 49 | `0.00641×` | `0.01937×` | 全部 18 步维持 **`1.025 s/s`** 至 `1.9162 s` | 精确吻合 |

### 3.3 阶段切换、`MovementMode` 与 `CalcWarpingTarget` 逐事件核对

- **`Action.Phase.Combo` 连跳放行点**（静态 vs V6 实测触发帧）：
  - `Dash` 静态 `1.108522 s` → V6 `f=31466`（`1.1074 s` 未触发）→ `f=31467`（`1.1273 s` 触发 `MoveToPole`）
  - `Jump_L` 静态 `1.200703 s` → V6 `f=31491`（`1.1960 s`）→ `f=31492`（`1.2183 s` 触发）；`f=31702`（`1.1938 s`）→ `f=31703`（`1.2187 s` 触发）
  - `Jump_R` 静态 `1.200127 s` → V6 `f=31516`（`1.1988 s`）→ `f=31517`（`1.2246 s` 触发）
- **`JumpTimeline -> Idle_L/R` 自动收势点**（静态 vs V6 实测）：
  - `Dash -> Idle_L` 静态 `1.325300 s` → V6 `f=31652`（`1.3096 s`）→ `f=31653`（`1.3267 s` 切 `Idle_L@0.0`）
  - `Jump_L -> Idle_R` 静态 `1.640069 s` → V6 `f=31559`（`1.6275 s`）→ `f=31560`（`1.6446 s` 切 `Idle_R@0.0`）
  - `Jump_R -> Idle_L` 静态 `1.640069 s` → V6 `f=31747`（`1.6381 s`）→ `f=31748`（`1.6552 s` 切 `Idle_L@0.0`）
- **`GAA_MoveMode_C` 移动模式切换**：静态默认值 `New Movement Mode = MOVE_Flying (5)`、`EndMovementMode = MOVE_Falling (3)`、`IgnoreChannels = [ECC_WorldDynamic]`；V6 全部 8 跳均在静态窗口边界（`Dash` `0.2356..1.0448 s`、`Jump_L/R/JumpToPlatform_L` `0.5136..1.2498 s`）精准切换 `1 -> 5 -> 3 -> 1`。
- **350 cm 分界与方向键回退远跨**：V6 `f=31591` 目标距 `441.90 cm`（`> 350 cm, <= 500 cm`），事件栈严格为 `IA_KeyboardMoveInput -> OnMove -> OnDash -> MoveToPole`（未按 `IA_Dodge`），验证静态 `OnMove` 的 350 cm 未命中回退 `OnDash` 逻辑。
- **`0.001 s` 定时刷新 Warp 目标**：V6 事件表中 `CalcWarpingTarget` 高达 `3,622` 次（单帧 ~16–20 次），完美印证静态 `MoveToPole` 的 `K2_SetTimerDelegate(CustomEvent, 0.001, true)` 直到 `Combo | Cancel` 清除。

---

## 4. 原版《影之刃零》跳桩完整逻辑规格书（可直接用于 1:1 复刻）

### 4.1 场景与入桩机制（`BP_LionDance_Area` + `BP_LionDancing_Pole_Base/Exit`）

1. **水边探索桩与舞狮 Boss 桩共用同一套系统**：由 `CharacterState.HasTempWeapon.HouHouHouHead` 区分常规探索与 Boss 戴狮头状态（切换 `Hun_LionHead_*` 与 `Hun_LionHead_B_*` 动画、攻击吐火球与抢生菜）。
2. **区域激活与入桩**：
   - `BP_LionDance_Area` 为样条触发体（`BP_BaseSplineTriggerVolume` 子类），重叠注册所有 `BP_LionDancing_Pole_Base`（含子类）。
   - 玩家进入触发体：`EnableInput(PC)` 并 `K2_GiveAbility(GA_StandingOnPole_C)`；离开则 `DisableInput` + `ClearAbility`。
   - 区域绑定 **`IA_Jump` (`Triggered`) -> `JumpAction()`**：
     - 已有 `Ability.SpecialMove.Pole` 则返回。
     - 方向优先取 `GetLastMovementInputVector()`，近零时回退 `GetActorForwardVector()`。
     - 候选池取可用木桩并**剔除 `BP_LionDancing_Pole_Exit_C`**。
     - 调用 `PBZAbilityBlueprintLibrary::GetNearestActor(PlayerLoc, FacingDir, ViewAngle=90.0°, MaxRange=500.0 cm, bUse2dDistance=true, Candidates, nullptr)`。
     - 命中后向角色发送 GameplayEvent `Ability.SpecialMove.Pole.Input`（`Instigator = Area`, `Target = NearestPole`）激活 `GA_StandingOnPole`。
   - **关于交互键 UI**：静态全包检索确认 `BP_LionDance_Area` 与 `BP_LionDancing_Pole_Base` **本身未挂载 `W_InteractionUI_SpecialMove`**（该控件用于飞檐走壁/梯子/滑索等 `BP_SpecialMove_*` 体积）。
3. **木桩与出口桩结构**：
   - `BP_LionDancing_Pole_Base`：`StandPoint` 与 `TopHeight` 挂在 `SM_WoodPost_001` 下，`RelativeLocation = (0, 0, 500.1268)`，`StandPoint` 带 ComponentTag `"WarpTarget"`；默认 `Activated=true, PlayerCanJump=true, AllowJumpOut=true`。
   - `BP_LionDancing_Pole_Exit`：`bHidden=true`，`SM_WoodPost_001` `RelativeLocation = (0, 0, -500.0)`，因此 `StandPoint` 位于 Actor 原点上方 `0.1268 cm`（贴地）；作为普通候选参与 `OnMove`/`OnDash` 选桩。
   - `BP_PoleJumpOutProtectVolume`：赋予 `CharacterState.LionDancingPoleProtect`，禁止在该体积内通过按 `IA_Jump` 或攻击直接跳下（如水面上方）。

### 4.2 `GA_StandingOnPole` 状态机、标签、相机与输入规则

1. **激活时配置（`K2_ActivateAbilityFromEvent`）**：
   - **Owned Tags**：`Ability.SpecialMove.Pole`、`CharacterState.AbilityState.Dash.Forbid`、`CharacterState.Forbid.Jump`、`CharacterState.Animation.DisableLegIK`、`CharacterState.Stand.NonKnockMonster.Strong`、`CharacterState.Stand.IgnoreAllHits`、`CharacterState.Stand.IgnoreBreak`。
   - **Blocked Abilities**：`Ability.Action.Stance`、`Ability.Action.Hun.ShachiAbility`、`Ability.Action.Hun.Weapon.Switch`、`Ability.Action.Hun.UseItem`、`Ability.Action.Hun.AirDash`。
   - **移动驱动覆写**：`ExplicitWalkSpeed=0, MaxAcceleration=0, BrakingFrictionFactor=2, GroundFriction=8, BrakingDecelerationWalking=2000, RotationRate=500`，并激活 `CharacterState.Native.Attitude.ForceJog`。
   - **相机叠加（`NBCameraComponent`）**：
     - 激活：`StandingOnPoleDistance = +300.0 cm`（`BlendTime = 0.5 s`），`StandingOnPoleSocketOffset = (0.0, +100.0, 0.0)`（`BlendTime = 0.5 s`）。
     - 恢复：在 `JumpToPlatform_L/R` 与 `JumpOff` 的 **`0.8987 s`** 单帧触发淡回 `0 / (0,0,0)`（`0.5 s`），`K2_OnEndAbility` 再兜底重置为 `0 / (0,0,0)`（`0.5 s`）。
   - **首跳**：直接调用 `MoveToPole(EventData.Target, Dash, -1.0)`。
2. **输入绑定与决策**：
   - **左右脚判定 `LeftOrRight()`**：当前正在播放 `[Idle_L, Jump_R, Dash, Attack_L, Hit_R]` 中任意一条 -> 返回 `true`（出左脚 `Jump_L` / `JumpToPlatform_L`），否则返回 `false`（出右脚 `Jump_R` / `JumpToPlatform_R`）。
   - **`CanMove()`**：不在 `[JumpOff, JumpToPlatform_R, JumpToPlatform_L, JumpOutAttack]`，且满足以下之一：`Idle_L/R`、拥有 `Action.Phase.Cancel`、或（拥有 `Action.Phase.Cancel|Combo` 且不在 `Hit_L/R`）。
   - **`CanDash()`**：不在 `[JumpOff, JumpToPlatform_R, JumpToPlatform_L, JumpOutAttack]`，且满足 `Idle_L/R` 或拥有 `Action.Phase.Cancel|Combo`。
   - **方向移动 `OnMove(ActionValue)`**（绑定 `IA_KeyboardMoveInput` / `IA_JoystickMoveInput`）：
     - 将 2D 输入按 `ControlRotation` 旋转为世界方向。
     - 调用 `PickPoleJumpTarget(ActorLoc, WorldDir, ViewAngle=90.0°, MaxRange=350.0, bUse2dDistance=true, Candidates, bPreferFarthest=false, CabbageMaxRange=500.0)`。
     - 命中：若目标 `CabbageActivated` -> `MoveToPole(Target, Dash, IsPlaying(Dash|Jump_R|Jump_L) ? 0.20 : -1.0)`；否则 -> `MoveToPole(Target, LeftOrRight() ? Jump_L : Jump_R, IsPlaying(Jump_R|Jump_L|Dash) ? 0.32 : -1.0)`。
     - 未命中 -> **回退调用 `OnDash(WorldDir)`**。
   - **远跨 / 闪避 `OnDash(PreferredDirection)`**（绑定 `IA_Dodge`、戴狮头按 `IA_Jump`、`OnMove` 回退）：
     - 若处于 `CharacterState.LionDancing.PreJump`（允许提前取消前摇，释放旧 `LastRequestedTargetPole` 占位）或 `CanDash()` 成立：方向按 `PreferredDirection -> LastInputVector -> ControlForward` 取值，调用 `PickPoleJumpTarget(..., ViewAngle=90.0°, MaxRange=500.0, bUse2dDistance=true, Candidates, bPreferFarthest=true, CabbageMaxRange=500.0)`，命中后 `MoveToPole(Target, Dash, IsPlaying(Dash|Jump_R|Jump_L) ? 0.20 : -1.0)`。
     - 若不可立即执行且处于 `Action.Phase.Act`：置位 `Buffered = true, BufferAttack = false`（等 `Action.Phase.Combo` 标签添加时自动消费并调用 `OnDash(0,0,0)`）。
   - **桩上按 `IA_Jump`**：未戴狮头且 `CurrentStandingPole.AllowJumpOut && !HasTag(LionDancingPoleProtect) && CanMove()` -> 直接播放 **`JumpOff`**（`-1.0`）。
   - **桩上按攻击键（`IA_AttackL/H / Shachi`）**：未戴狮头且允许跳出且（`PreJumpOut | PreJump | CanMove`）-> 播放 **`JumpOutAttack`**；戴狮头 -> 播放 `Attack_L/R`（支持 `Act` 期 `Buffered=true, BufferAttack=true`）。
   - **`MoveToPole(TargetPole, InTimeline, StartTimeSeconds)`**：
     - 先清除旧的 `Timer`。
     - 若 `TargetPole` 类为 **`BP_LionDancing_Pole_Exit_C`**：调用 `CalcWarpingTarget(TargetPole)`（计算 `DeltaZ = Target.StandPoint.Z - LastTargetLocation.Z`），若 `Abs(DeltaZ) < 200.0` 则按 `LeftOrRight()` 播 `JumpToPlatform_L / R`，否则播 `JumpOff`（`StartTimeSeconds` 透传！）；Timeline 完成/取消时调用 `K2_EndAbility()`。
     - 若为普通桩：启动 `0.001 s` 循环 `Timer`（反复 `CalcWarpingTarget` 直到 `Combo | Cancel`）、更新 `LastRequestedTargetPole` 占位标志、播放 `InTimeline`；Timeline 结束/被下一跳打断时（非 `SelfInterrupted`）将 `CurrentStandingPole` 更新为该 Task 绑定的目标桩。
3. **`CalcWarpingTarget` 公式**：
   - `Start = Avatar.GetActorLocation()`，`Target = TargetPole.StandPoint.WorldLocation`，`Rot = Normal2D(Target - Start).ToRotator()`。
   - 注册 `'Land'`：`Location = Target, Rotation = Rot`。
   - 注册 `'Apex'`（未消费）：`HalfBase = Start + 0.65 * (Target - Start)`，`Apex = HalfBase + (0, 0, max((Target.Z + 100.0) - HalfBase.Z, 150.0))`。

### 4.3 核心 8 条探索向 `LGATimeline` 精确参数表

| Timeline | Montage（常规版） | 默认起点 / 连跳起点 | `Rate` 曲线关键帧（时间 → 倍率） | `GAA_MoveMode`（Flying→Falling） | `SkewWarp(Land)` 窗口（静态起止；运行时 End `-0.0001 s`） | 关键节点（`Act` / `Combo` / `Cancel` / 后续） |
|---|---|---|---|---|---|---|
| `Dash` | `Dodge_F_R_Montage` (`1.967 s`) | `0.00 s` / `0.20 s` | `0.0s→1.60` (Cubic), `1.3s→1.025` | `0.2356 .. 1.0448 s` | `0.2433 .. 1.0930 s`（平移+旋转，`RotMult=0.3, MaxRot=90°/s`） | `Act 0.4516` / `Combo 1.1085` / `Phase清 1.3108` / **`→Idle_L 1.3253`** |
| `Jump_L` | `Battle_Walk_Fwd_L_Montage` (`2.167 s`) | `0.20 s` / `0.32 s` | `0.0s→2.00`, `0.3s→2.25(t=1.75)`, `0.5s→2.875(t=-0.417)`, `0.9s→2.00(t=-1.85)`, `1.5s→1.025` | `0.5136 .. 1.2498 s` | ① `0.0000 .. 0.4993 s`（仅旋转，Linear）<br>② `0.5136 .. 1.2493 s`（平移+旋转，`CircularIn, RotMult=0.5`） | `PreJump 0.1941..0.4998` / `Act 0.5133` / `Combo 1.2007` / **`→Idle_R 1.6401`** |
| `Jump_R` | `Battle_Walk_Fwd_R_Montage` (`2.167 s`，Timeline 长 `2.0 s`) | `0.20 s` / `0.32 s` | 同 `Jump_L` | `0.5136 .. 1.2498 s` | ① `0.0000 .. 0.4993 s`（仅旋转）<br>② `0.5136 .. 1.2493 s`（平移+旋转，`CircularIn, RotMult=0.5`） | `PreJump 0.1941..0.4998` / `Act 0.5133` / `Combo 1.2001` / **`→Idle_L 1.6401`** |
| `Idle_L` / `Idle_R` | `Battle_Idle_L/R_Montage` (`6.70 / 6.60 s`) | `0.00 s`（`bLooping=true`） | 无（恒定 `1.0×`） | 无 | 无 | 循环站桩（Timeline 长 `6.2 / 6.1 s`） |
| `JumpToPlatform_L` | `JumpOut_L_02_Montage` (`1.917 s`) | `0.20 s` / `0.32 s`（`bStopAbility=true`） | 同 `Jump_L` | `0.5136 .. 1.2498 s` | ① `0.0000 .. 0.4993 s`（仅旋转）<br>② `0.5136 .. 1.2493 s`（平移+旋转） | `Cam复原 0.8987` / `Combo 1.1522` / `Cancel 1.2589` / **`CancelByMoveInput 1.2621..1.9167`** |
| `JumpToPlatform_R` | `JumpOut_R_02_Montage` (`1.917 s`) | `0.20 s` / `0.32 s`（`bStopAbility=true`） | 同 `Jump_L` | `0.5136 .. 1.2498 s` | 同 `JumpToPlatform_L` | `Cam复原 0.8987` / `Combo 1.1522` / `Cancel 1.2606` / **`CancelByMoveInput 1.2636..1.9167`** |
| `JumpOff` | `JumpOut_R_Montage` (`2.667 s`) | `0.00 s`（`bStopAbility=true`） | `0.3s→1.30`, `0.9s→1.00(t=-0.3)`, `1.3s→1.00` | 无（用 `GravityScale=0.5` `0.3058..1.0446 s`） | 无（用 `RM_Input OffsetYaw=180°, Rate=720°/s`） | `Cam复原 0.8987` / `Combo 1.3205` / `Cancel 1.5196` / **`CancelByMoveInput 1.5255..2.6667`** |

---

## 5. 附：当前 `ZeroAnim` 工程与原版静态规格的待对齐点（供后续决定）

本次仅完成静态导出与 V6 核对，**未改动 `ZeroAnim` 工程代码与资产**。若后续需要按原版静态规格进一步 1:1 同步，可优先对齐以下 6 项：

1. **入桩与远跨选桩范围**：原版入桩 `500 cm / 90°`（最近），远跨 `500 cm / 90°`（**优先最远 `bPreferFarthest=true`**），近跨 `350 cm / 90°`（最近，未命中自动回退远跨）。
2. **连跳起播偏移补全**：从 `[Dash, Jump_R, Jump_L]` 直接接远跨 `Dash` 时从 **`0.20 s`** 起播；直接接下桩 `JumpToPlatform_L/R` 时从 **`0.32 s`** 起播。
3. **精确 `Rate` 曲线与混出掉回 `1.0×`**：使用原版 5 键 Cubic / 2 键 `Rate` 曲线驱动活跃动作，并在被打断混出时停止外部倍率驱动（恢复 `1.0×`）。
4. **相机叠加参数精确值**：入桩 `+300 cm` 臂长、`SocketOffset (0, +100, 0)`、`BlendTime 0.5 s`，下桩在 `0.8987 s` 提前淡回。
5. **双路径下桩与落地移动取消**：区分出口点 `<200 cm`（`JumpToPlatform` + `1.262 s` 移动取消）与主动按跳跃键 / `>=200 cm`（`JumpOff` + `0.5` 重力缩放 + `1.525 s` 移动取消），并支持防误跳保护区。
6. **桩上禁用 LegIK**：在 `ABP_PlumPoleManny` 中绑定桩上状态直接关闭脚部 IK，避免与 `StandPoint` 对齐冲突。
