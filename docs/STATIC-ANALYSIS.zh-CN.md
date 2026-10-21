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

原 APK 的资源名为 External Pubg Cheat | Modyolo.com，包名 google.suport.misaki，针对 PUBG Mobile。重新实现的是显示类别和配置概念，不是把 ELF 二进制转换为 Mach-O，也没有沿用 Android 内存偏移。

本阶段没有移植原 APK 的删除脚本、iptables 修改脚本或加密 ZIP 内容。未完成的游戏写入功能明确列在 README 和菜单中；没有用可切换的空开关冒充已实现功能。
