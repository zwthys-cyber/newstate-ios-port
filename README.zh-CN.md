# NEW STATE iOS port

目标：NEW STATE 0.9.98 (804)，com.pubg.newstate，ARM64 / iOS 17.0。

状态：正在适配。Android APK 为 PUBG Mobile 外部工具，原生 ELF 不能直接作为 iOS dylib 使用。功能需要针对上传的 iOS 主程序重新定位与实现。

本仓库仅包含原创移植代码、构建脚本和分析结论；不包含游戏主程序、框架、APK、游戏资源或设备日志。编译与原生测试使用 GitHub 托管 macOS 运行器。CI 通过不代表实机功能已验证。

已核实输入：

- Info.plist：版本 0.9.98，build 804。
- 主程序 Mach-O UUID：AF49432E-9F7F-3D7C-9470-8CF90E3B3378。
- 候选解密主程序的 cryptid 为 0，加密页实际内容已变化；加密页之外的正文与原版一致。
- newstatex 框架 UUID：46CDFE2E-EFEB-3F64-9D89-12454703D415，cryptid 为 0。
- 静态恢复 3,321 个类的注册信息、23,991 条属性注册记录；属性名包含混淆值，布尔类型和内层数组描述符不能直接当作普通字段偏移使用。

当前未宣称透视、自瞄、后坐力修改、反检测或设备兼容性已经完成。
