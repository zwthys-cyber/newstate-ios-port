# 0.9.98（804）适配依据

主程序 UUID 为 AF49432E-9F7F-3D7C-9470-8CF90E3B3378。原版与候选解密版大小均为 137,256,032 字节。原版 LC_ENCRYPTION_INFO_64 的 cryptoff=0x10b2000、cryptsize=0x1000、cryptid=1；候选版 cryptid=0。逐字节比较发现 4,080 字节不同，其中 4,079 字节位于加密页，另一个字节为 cryptid 标记；该页之外正文相同。这支持候选版已执行实际解密，不只是改标记。未通过实机执行验证整份程序。

候选解密版 SHA-256：cf59f3663bba56b035024e71d814ffceecf86d5d99bd90873c2fd82372e3a0c7。

## 数据来源

程序使用 dyld chained fixups，磁盘上很多指针带链信息，不能把 64 位原始值直接当成虚拟地址。分析时恢复 pointer format 2/6 的 rebase，进而定位 UE4 生成的 FClassParams / 属性描述符和类构造登记函数。引擎包含 ++UE4+Release-4.26 字符串。

恢复了 3,321 个类、23,991 条类属性注册记录。属性名部分已混淆；这个计数包括数组内层描述符，不能理解为等量独立可直接读写的字段。FBoolPropertyParams 的布局也不同，不能直接取同一位置为偏移。

以下 RVA 均相对于主程序 __TEXT 基址；ASLR 后以当前镜像头地址为基准。

| 项目 | RVA / 成员偏移 | 静态依据 |
| --- | --- | --- |
| GEngine | 0x81e8ef0 | GetGameUserSettings / GetPlayerController 的原生实现 |
| Engine.GameViewport | +0x960 | Engine 类属性登记 |
| GameViewportClient.World | +0x1f8 | GameViewportClient 类属性登记 |
| World 控制器弱引用数组 | +0x230，元素 24 字节 | GetPlayerController -> 原生数组访问 |
| 对象表 chunks / NumElements | 0x8174520 / 0x8174538 | 弱引用解析原生函数 |
| FUObjectItem | 24 字节，指针 +8，flags +16，serial +20 | 同一原生弱引用解析 |
| UObject Outer / Class | +8 / +0x30，各为 24 字节编码指针 | typed outer 查找、原生类型判断 |
| PlayerController.AcknowledgedPawn | +0x370 | 属性登记，编码指针 |
| PlayerController.PlayerCameraManager | +0x398 | 属性登记，编码指针 |
| Actor.RootComponent | +0x278 | 属性登记与 K2_GetActorLocation 原生 thunk |
| SceneComponent 世界位置 | +0x150 | K2_GetActorLocation 原生 thunk 直接加载 |
| XtrmCharacter.Health / HealthMax | +0x22f8 / +0x6dc | XtrmCharacter 属性登记 |
| Character.CapsuleComponent | +0x350 | Character 属性登记，编码指针 |
| CapsuleComponent.CapsuleHalfHeight | +0x534 | CapsuleComponent 属性登记 |
| ULevel.OwningWorld | +0x178 | GetWorld 相关原生函数 |

对象指针为三段 uint64 编码；弱引用为分别编码的 32 位 index 和 serial。读取代码对照原生 XOR / rotate 操作实现，并核对对象表序列号和销毁标志。读取失败返回空值，不执行原生解码函数。

## 相机路径

PlayerCameraManager 构造函数设置基类虚表 RVA 0x7a9e970；TslPlayerCameraManager 使用 RVA 0x722f0f0。两者的 +0x6a8 槽均指向 RVA 0x54e7390。

该函数复制当前缓存视图到调用者返回缓冲区：位置来自相机 +0x15e0，返回缓冲区旋转 +0x5cc 对应源 +0x1bac，FOV +0x5e0 对应源 +0x1bc0。GetCameraLocation / GetCameraRotation / GetFOVAngle 的原生实现分别取这些返回字段；FOV 还优先使用相机 +0xef4 的正覆盖值。

插件不调用该函数，仅检查该虚表槽仍指向已分析实现，再读取对应字段。虚表实现不匹配、类型不匹配、字段不可读或数值不合理时停止显示。

屏幕投影采用 UE 坐标约定和水平 FOV 数学模型。尚未在设备上核实屏幕方向、视口裁剪、宽高比限制、观察者模式与数值抖动，因此仍属于待验证实现。

## 与原 APK 的关系

新增读取依据：PlayerState 的 GetPlayerName 原生实现 RVA 0x5505f70 在标志 +0x2b8 的最高位未设置时，从 +0x358 / +0x360 复制 UTF-16 FString；置位时走自定义虚拟路径。插件只覆盖前一种情况，并检查长度、容量和末尾空字符。角色 PlayerState 使用 Pawn 的 +0x2c0 编码引用。

后坐力调查：XtrmWeapon_Gun 的 RecoilScale 属性偏移为 +0xc00，属性类型登记到 Vector2D（8 字节，X/Y 两个浮点）。只证明存在此字段，不证明单独改写能产生预期效果，因此尚未接入写入开关。

骨骼调查：GetBoneName 的原生实现 RVA 0x50eab78 使用 SkinnedMeshComponent +0x600 的 mesh 引用和 mesh +0x1b0 / +0x1b8 的 12 字节骨骼记录数组。每条记录为 FName 的两个 uint32 及 int32 父索引。

SkeletalMeshComponent 构造设置虚表 RVA 0x7c6b8e0。其 +0x490 槽对应 GetSocketTransform（RVA 0x50ec878），后者调用 RVA 0x50e86b8 的骨骼变换实现。当前 component-space pose 从组件 +0x678 起的 16 字节数组头中选择，选择索引位于 +0x6e8，记录步长 48。Transform 的 quaternion/translation/scale 分别位于 +0/+0x10/+0x1c，末尾存在填充。

若组件 +0x5e8 的 24 字节弱引用能解析到有效 leader，原生代码使用组件 +0x710 的 int32 重映射表，从 leader 的 pose 缓冲取姿态；组件到世界变换仍使用调用方组件 +0x140。只读实现遵循上述分支，检查注册标志、边界、缓冲头和选中索引稳定性，并以世界变换的四元数和缩放投影骨骼平移值。未调用游戏的 GetSocketTransform 或 GetBoneTransform。效果仍需设备验证。

写入功能的调查结果仅用于后续适配，不表示未实现功能已经存在于当前 dylib。骨架目前已接入实验性只读显示。

原 APK 的资源名为 External Pubg Cheat | Modyolo.com，包名 google.suport.misaki，针对 PUBG Mobile。重新实现的是显示类别和配置概念，不是把 ELF 二进制转换为 Mach-O，也没有沿用 Android 内存偏移。

本阶段没有移植原 APK 的删除脚本、iptables 修改脚本或加密 ZIP 内容。未完成的游戏写入功能明确列在 README 和菜单中；没有用可切换的空开关冒充已实现功能。

## 0.3.1 队伍、类型、死亡和快速发现依据

以下均针对同一主程序 UUID；仅记录读取依据，不包含原始设备数据。

- `TeamMemberInfo` 结构注册参数 VA `0x107586a30` 指定大小 8，两个 int32 属性 `TeamNumber` / `MyNumber` 分别位于 +0 / +4（描述符 VA `0x1075869d0` / `0x1075869f8`）。XtrmCharacter.MemberInfo 位于 +0xc04，XtrmPlayerState.MemberInfo 位于 +0xba4。原生 RVA `0x224d564` 比较两个角色 +0xc04；RVA `0x1c314e4` 附近验证 state 的 TeamNumber/MyNumber 不为 -1 后比较。当前实现要求两者非负，允许队号 0；本地角色不就绪时可从控制器的 XtrmPlayerState 回退读取。只有确认同队才默认过滤，不从名字/距离猜队伍。
- PlayerState.bIsABot 的生成设置函数 RVA `0x5997b50` 读取 +0x2b8 并 OR `0x08`；读取相同位，且先验证 PlayerState 类型。读取或类型验证失败时标记未知，不能因为未读到 bot 标志就认定真人。此位在当前游戏模式中是否完整反映人机身份仍需对局确认。
- XtrmCharacter.IsDying 的 exec 注册包装 RVA `0x35f6d10` 调用 RVA `0x22d5f68`，原生函数读取角色 +0x930 的 bit 0。每轮读取该位，置位即清除可绘制标记；不会用 Health==0 代替死亡，避免误删倒地角色。不要混用 TeamMemberState.GetIsDying 的 +0x334 字段，它属于另一个类。
- World.GameState 为 +0x148 普通对象指针，GameStateBase 单例 RVA `0x809ae70`；PlayerArray 为 +0x2c8 的对象指针数组；PlayerState.PawnPrivate 为 +0x368 普通对象指针。快速路径只读这些登记字段，验证 actor.PlayerState 的反向引用，再核对全局对象表中的对象指针、serial 和销毁标志。
- 原生 RVA `0x22d5fbc` 附近等有效性检查读取 UObject +0x20 的三个 uint32，以 key `0xb5c76d21` 及 ROR7 解码后索引 GUObjectArray。快速路径复用相同解码，但只有索引在对象数范围内且表项反查等于目标 actor 才接纳；不以解码值本身当作可信指针。

快速路径每轮最多检查 32 个玩家状态，不阻塞位置更新等待整个全局对象表扫描；全局扫描仍用于缺少 PlayerArray/PawnPrivate 的角色、载具和物资。诊断仅导出本地队伍是否就绪、过滤/分类计数、玩家数组可用性与本轮发现数量，不导出队号、昵称或玩家坐标。上述为静态依据和合成内存测试范围，尚不能声称真机类型判断或实际延迟已验证。
