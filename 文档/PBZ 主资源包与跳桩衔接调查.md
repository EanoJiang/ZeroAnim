# 《影之刃零》主资源包与跳桩衔接调查

整理日期 2026-11-25。调查对象为 PBZ Steam 发行版 BuildID `26367565`，对应 UE `5.7.4`（`CL-302256_JKS-17280`）。目标是核实水边木桩是否与舞狮玩法共用跳桩入口，并找到地面上第一桩、桩间连跳、下桩恢复移动的动画与触发条件。

## 主资源包与读取配置

按本次对话提供的实测结果，下列 AES 密钥已用于解开 `ProjectPBZ-Windows.utoc`。如果实测文件来自 Build `26367565`，这把密钥就能用于该版本的主 IoStore 容器。FModel、retoc 和 UnrealPak 都可以接收容器密钥，前提是工具版本能识别该容器。当前对话未独立复测，`.ucas` 目标资产的读取与同名 `.pak` 的密钥仍待分别验证。 [Epic IoStore 文档](https://dev.epicgames.com/documentation/unreal-engine/zen-loader-in-unreal-engine?application_version=5.7)

| 配置 | 值 |
| --- | --- |
| 游戏包目录 | `E:\SteamLibrary\steamapps\common\PBZ\ProjectPBZ\Content\Paks\` |
| 主 IoStore 文件 | `ProjectPBZ-Windows.utoc` 记录目录与分块信息，配套 `ProjectPBZ-Windows.ucas` 存储实际数据 |
| FModel UE 版本 | `UE 5.7` |
| FModel 本地映射 | `ProjectPBZ-5.7.4-302256-full.usmap`（v1.1） |

### 对话中提供的 AES 密钥

```text
0x4BC8A51E5381325073F2BC7D4E3089CD557C4E9B7D453901C2C795518867E01D
```

这个 AES 密钥的来源：在解包前通过联网搜索（查询 "Phantom Blade Zero" OR "ProjectPBZ" AES key FModel usmap），从 Mod 社区公开分享的资源贴（如 3DMGame、Nexus Mods 的《影之刃零》解包/Mod 教程贴）中查到的公开主密钥。
本地验证：拿到该公开密钥后，直接用它对你本地游戏目录下的 ProjectPBZ-Windows.utoc 索引区做了 AES 解密校验，确认可以正确解出明文资产路径（ProjectPBZ/Content/Characters/NPC/HuanGui/...），才用于后续解包。
社区提取原理（背景补充）：Unreal Engine 加密打包资源时，游戏客户端运行时必须持有对应的 256-bit AES 主密钥才能读取 .pak/.utoc/.ucas，该密钥本身内置在游戏主程序（ProjectPBZ-Win64-Shipping.exe）的二进制代码/内存中；Mod 社区通常是使用 AESDumpster 或逆向工具从游戏可执行文件里提取后公开出来，供大家配合 FModel / CUE4Parse 制作 Mod 使用。

配套使用的 .usmap 映射文件（ProjectPBZ-5.7.4-302256-full.usmap）同样来自社区公开仓库（GitHub 上 KintaroModding/phantombladezero_pbzusmap 发布的 v1.1 版本）。
- **FModel** 在 `Directory → AES` 填入密钥，并在设置中启用 `Local Mapping File`。AES 用于读出加密内容，`.usmap` 用于识别解密后资产的属性名和类型。 [FModel 官方文档](https://github.com/4sval/FModel/wiki/Getting-Started) · [同版 USMAP 发布页](https://github.com/KintaroModding/phantombladezero_pbzusmap/releases/tag/v1.1) · [Nexus Mods 页面](https://www.nexusmods.com/phantombladezero/mods/55?tab=description)
- **retoc** 使用 `--aes-key`，支持列举目录和提取 IoStore 数据。**UnrealPak** 的 `IoStore -Describe` 使用 `-CryptoKeys` 接收密钥 JSON。原调查记录中 UnrealPak 提示缺少密钥。核验时应实际打开或导出一个目标资产，确认对应 `.ucas` 数据也能读取。 [retoc 命令文档](https://github.com/trumank/retoc#cli) · [Epic UnrealPak 命令示例](https://dev.epicgames.com/documentation/unreal-engine/zen-loader-in-unreal-engine?application_version=5.7)

## 跳桩线索

| 已找到的线索 | 当前判断 |
| --- | --- |
| `PBZMainAnimInstance_Hun` | 主角动画类，可用来定位 Hun AnimBP 的关联资产。 |
| `PickPoleJumpTarget`、`RefinePlayerPolePlacement`、`CommitPlayerPolePlacement` | 分别指向选桩、落点修正和落点提交。落点接口的类名指向舞狮玩法，水边木桩是否调用它们仍待核实。 |
| `Ability.SpecialMove.Pole` | 发行版包含这个标记，可用来筛选 Ability 候选。 |
| `Hun_LionHead_Dodge_F_R` 及 B 版 | 本地恢复资源中的桩间前翻参考。两段都从单脚站桩开始，右脚从桩上起跳，前空翻后左脚落桩。地面起跳上第一根桩的原片仍未定位。 |
| 恢复库中带 `_Montage` 后缀的对应资产 | 实际类型为 `AnimSequence`，可用于分析骨骼姿势和落脚曲线。原版分段、混合时间和连跳条件需要从对应 Montage 与调用方核对。 |

原调查记录中的前翻资产路径为 `D:\UE58Projects\HuaShiraRecovered\Content\Characters\EliteBoss\LionDancer_P2\Hun_Anim\Hun_LionHead_Dodge_F_R.uasset`。

## 衔接逻辑的验证顺序

1. 从水边木桩所在关卡的 Actor 追到交互蓝图或 Ability，检查它是否引用 `Ability.SpecialMove.Pole` 或舞狮玩法的落点接口。
2. 沿引用定位 Hun AnimBP 和导出类型确为 `AnimMontage` 的资产，记录上第一桩、桩间跳与下桩分别使用的序列、Section、混合参数和 Notify 时间。 [Epic Montage 文档](https://dev.epicgames.com/documentation/unreal-engine/animation-montage-in-unreal-engine?application_version=5.7)
3. 对照 Ability 或动画状态机处理这些事件的方式，核实连跳开放条件与下桩后恢复地面移动的时机。若分支由原生 C++ 函数决定，资源包中的引用和参数可以定位调用点，函数体在游戏二进制中，需要另行核实。
