# NSP 外置显示 0.2.3

0.2.3 针对 0.2.2 在运行时确认 persona/no-sandbox/platform 权限存在、辅助程序存在且有执行位，但 `posix_spawn` 仍返回 `EPERM` 的反馈：控制程序也改为明确的无容器配置，不再携带字符串形式的 container-required。沿用独立的无容器 `NSPExternalHUD`；任务读取和窗口权限仅授予 HUD。该调整仍需实机确认，不能由 CI 认定 EPERM 已解决。

HUD 启动返回 EPERM 后，后台自动启动一次不加载 UIKit、不接触游戏的 `NSPSpawnProbe`，分别使用普通用户和 root persona；结果记录在 `launch.json` 的 `spawn_probes` 中。探测程序只把自身 PID/UID/EUID 写到本工具目录后立即退出，不注册后台服务。普通启动也失败、仅 root 启动失败，或仅 HUD 失败，可据此分开分析。没有把探测成功误报为悬浮层成功。

`launch.json` 记录父进程运行时实际读取到的权限、系统版本、辅助程序存在性/文件权限/属主。HUD 入口检查实际 euid 为 0 才继续初始化，并报告 UID 和自己的运行时权限。日志只查询本工具自身权限，不枚举其他应用的权限。沿用持久化启动记录与错误码/退出原因导出。

独立 IPA，通过 TrollStore 安装，目标 iPhone 15 Pro Max / iOS 17.0 / NEW STATE 0.9.98（804）。不修改游戏 IPA，不向游戏注入 dylib，不改写游戏内存。该版本是新的外置架构，尚未实机验证；编译成功不证明设备会授予读取权限，也不证明窗口在其他应用前台可见。

## 已实现的代码

- 原生控制界面、独立悬浮进程、启动/停止按钮及状态导出。
- 尝试通过 `task_read_for_pid` 取得只读端口，以 `vm_read_overwrite` 读取目标进程；不请求控制端口、不附加调试器。
- 远程 dyld 列表读取、主程序架构和 UUID 校验，匹配后复用原有对象、相机和只读显示逻辑。
- 角色（含队友）、血量、姓名、距离、胶囊方框、连线、中心准星、实验性骨架及部分载具/物资位置。
- 权限、版本、进程或系统窗口接口失败时报告实际阶段；没有游戏数据时不生成模拟人物。

尚未实现：队伍区分、自瞄、后坐力修改、去雾，以及开发者模式或其他安全检测屏蔽。没有“免检测”或“免封”承诺。

## 安装与设备测试

1. 在 TrollFools 移除此前测试库，确认原版游戏能正常启动。游戏文件保持原样，开发者模式保持关闭。
2. 用 TrollStore 安装 **NSPExternal.ipa**。普通全能签分发签名不能保留本包所需权限，不适用于此独立外置版。
3. 打开“NSP 外置显示”，保持“开启游戏读取”关闭，点击“启动悬浮层”，返回桌面检查 NSP 状态和准星。窗口方向不合适时在主界面切换横屏左/右或竖屏。
4. 悬浮层可见后，启动游戏，再回控制界面开启游戏读取，随后回到游戏。骨架先保持关闭。
5. 若悬浮层启动失败，等约 3 秒让自动探测完成，再用“导出状态”提供 `launch.json`、`status.json` 和 `settings.json`；存在时也会导出 `spawn-check-mobile.json` 和 `spawn-check-root.json`。不要为了测试切换开发者模式；本包不执行该开关操作。
6. “停止悬浮层”通过本工具自己的通知结束辅助进程，不终止游戏。辅助进程不注册开机启动，重启后需手动启动。

HUD 的状态/准星会出现在其他应用之上，停止前会持续显示；玩家标记仅在读取成功且相机有效时绘制。方向选择为手动设置，当前未实现自动跟随游戏界面方向。跨应用窗口注册调用返回不等于设备实际显示成功，需要目视确认。

状态数据改存本工具专用目录 `/var/mobile/Library/Application Support/org.newstateport.External/`，由控制程序创建，不再依赖应用数据容器。旧容器文件不删除、不迁移，新版首次使用会恢复默认设置（读取关闭）；请从新版界面导出，避免把旧版容器里的日志当作本次结果。导出只含版本、阶段、PID、UID、自身权限查询、系统版本、辅助程序权限、错误码、退出状态、计数及设置，不含账号、玩家姓名或坐标。HUD 在画面中显示姓名属于用户开启的显示功能，不进入导出文件。

## 权限与来源

本包只请求任务读取端口权限 `com.apple.system-task-ports.read`，没有 `task_for_pid-allow`、`get-task-allow`、`com.apple.system-task-ports.control` 或调试器权限。Apple 的 iOS 17 内核代码明确区分任务端口种类：开发者模式关闭时限制外国控制端口，而读取端口仍需通过其他访问检查。因此只读路径值得验证，但这不证明任意 TrollStore App 都能读取本游戏。

- Apple XNU iOS 17 task port conversion: https://github.com/apple-oss-distributions/xnu/blob/xnu-10002.1.13/osfmk/kern/ipc_tt.c
- Apple task read port syscall: https://github.com/apple-oss-distributions/xnu/blob/xnu-10002.1.13/bsd/vm/vm_unix.c
- TrollStore 对相关权限的开发者模式要求检查：https://github.com/opa334/TrollStore/blob/main/RootHelper/main.m
- TrollStore 的应用容器与 root 辅助进程说明：https://github.com/opa334/TrollStore#unsandboxing
- HUD 启动和窗口托管方式参考 Lessica/TrollSpeed（MIT）；完整许可见 ThirdPartyNotices.txt。

没有实现内核修改、签名校验关闭或保护事件屏蔽。原版游戏可能拒绝某些环境或外部访问；本包不保证改变其安全判定。

编译、主机测试、设备目标签名与 IPA 检查只在 GitHub Actions 的 macOS 运行器执行。公开源码及产物不含游戏文件、设备日志或个人证书。TrollStore 安装过程仍需重新处理本工具自身签名；CI 的临时签名仅用于可核对的构建产物。
