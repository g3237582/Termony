# HarmonyOS NEXT 手机（Mate 80）适配说明

本文记录 Termony 在华为 Mate 80（HarmonyOS NEXT / API 5.0.5）上的 P1 适配，目标是让 HAP 能按手机设备类型安装，而不是改写终端架构。

## 设备类型（deviceTypes）

上游 Termony 面向 HarmonyOS Computer / MateBook Pro，`entry` 模块原先只声明 `2in1`。Mate 80 是 **phone**，未声明 `phone` 时无法按手机安装。

当前声明：

- `entry/src/main/module.json5` 的 `deviceTypes`：`["phone", "2in1"]`
- `entry/build-profile.json5` 的 default target `deviceType`：同样为 `["phone", "2in1"]`

保留 `2in1` 是为了兼容平板/二合一；`phone` 是 Mate 80 安装所必需。

未改权限、Ability、`hnpPackages`。内核相关权限（可写代码页、关闭代码页保护）仍按上游声明，供 elf-loader / qemu 等路径使用；是否在手机上实际授权取决于调试签名与系统策略。

## Bundle ID

`AppScope/app.json5` 的 `bundleName` 已从上游 `je.jia.termony` 改为 `com.alvin.termony`。

这是当前设备上调试签名已使用的包名。`push.sh` / `build-linux.sh -p` 会从 `app.json5` 读取 bundleName，无需再改脚本。

应用显示名仍为资源字符串 `Termony`（`AppScope` / `entry` 的 `string.json`），本阶段不改品牌文案。

## HNP 产物：安装 HAP ≠ 可用 shell

`module.json5` 声明了私有包 `base.hnp` 和公共包 `base-public.hnp`，但仓库 **不包含** 这些二进制：

- `entry/hnp/arm64-v8a/` 仅有 `.gitkeep` / `.gitignore`（`*.hnp` 被忽略）
- `entry/hnp/x86_64/` 同样为空

不要伪造 `.hnp`。Mate 80（arm64）在装出可用 shell 之前，必须按上游 README 先构建并打进 HAP：

1. 构建 HNP：`./create-hnp.sh`（macOS）或 `./build-linux.sh -b`（Linux / WSL），也可使用 CI 产物。
2. 将 `entry/hnp/<abi>/*.hnp` zip 进 HAP（`build-linux.sh` 的 `zip -r ... hap hnp`，或 `sign.py` 所描述的手动打包）。
3. 用已与 `com.alvin.termony` 对齐的调试证书签名并 `hdc` 安装。

没有 HNP 时，应用可以装上并打开终端 UI，但不会解压 `/data/app` 下的工具链，也没有 `/data/app/bin` 里的 busybox/bash 等命令。

## 本阶段不做的事

- 不重写终端、PTY、HNP 加载架构
- 不把 MateBook 专用文案改成手机文案（README 仍描述 Computer 场景）
- 不在本仓库提交预编译 `.hnp`
