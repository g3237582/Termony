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

## 启动崩溃（forkpty / fcntl assert）

P1 在 Mate 80 Pro Max（HarmonyOS NEXT SGT-AL10 7.0.0.105）上安装带完整 HNP 的 HAP 后，应用仍会在页面加载时 CppCrash：

```
Assertion failed: res == 0 (.../terminal.cpp: Fork)
terminal_context::Fork → fcntl(fd, F_SETFL, ...|O_NONBLOCK)
Index.ets → testNapi.run() → Start() → Fork()
```

**根因（代码层，已由本崩溃栈证实）：** `Fork()` 未检查 `forkpty` 的返回值。`forkpty` 失败时 `pid == -1`，`fd` 保持 `-1`（或为无效 fd），父进程仍对它做 `fcntl(..., O_NONBLOCK)`，`assert(res == 0)` 直接杀掉 UI 进程。这与「HNP 是否打进 HAP」无关：HNP 只提供 `bash` 等二进制，不授予 `fork`/`pty` 能力。次要缺陷：子进程 `execl` 失败会落到父进程代码；`Start()` 在已启动时不释放锁。

本机崩溃发生在 assert，**没有留下 `forkpty` 的 errno**。装上本修复后，hilog tag `testTag` 会打出 `forkpty: <errno> (...)`，以及 `openpty` / `posix_spawn` 的后续 errno。

**根因（平台层，与上游/官方文档一致，待 hilog errno 最终确认）：** HarmonyOS NEXT **phone** 普通第三方应用不能用 POSIX `fork` / `forkpty` / `posix_spawn` 拉起任意子进程。证据：

- 上游作者在 [TermonyHQ/Termony#142](https://github.com/TermonyHQ/Termony/issues/142#issuecomment-3621675617) 明确「必须是 2in1 设备」（该 issue 本身发生在 MateBook Pro 上，是 2in1 场景的 bash 路径问题，但也标明 Termony 的目标设备）。
- OpenHarmony / HarmonyOS Native ChildProcess：`OH_Ability_StartNativeChildProcess` 在 API 13 及更早仅 2in1；API 14 起为 2in1 + tablet。其它设备返回 `NCP_ERR_NOT_SUPPORTED`。**Phone 不在支持列表**。且该 API 加载的是应用自己的 `.so` 入口，不能 `execl` HNP 里的 bash。
- NEXT 应用沙箱默认拦截非系统应用的 `fork`（即使声明进程相关权限也对第三方关闭）。

因此 Mate 80 上 `forkpty` 最可能是 `EPERM`/`EACCES`/`ENOSYS`，随后 `fcntl(-1)` assert。备选假设（`forkpty` 成功但 fd 仍无效）也会被同一套错误检查接住，不再崩进程。

当前代码会：

1. 检查 `forkpty` / `fcntl` / `pthread_create` 返回值，**不再 assert 崩 UI**。
2. 把 errno 打到 hilog（tag `testTag`），写进终端缓冲区，并由 `run()` 返回字符串给 ArkTS（Toast）。
3. `forkpty` 失败后尝试 `openpty` + `posix_spawn`（不改父进程 cwd/environ；bash 候选含 HNP 路径和 `/system/bin/sh`）。若平台允许 spawn，则 `pwd`/`echo`/`ls` 可以工作；若同样被拒，日志能区分是 pty 还是 spawn 被拒。

**因此：HNP 安装成功 ≠ 手机上能跑 `pwd`/`echo`/`ls`。** 不要用伪造 `.hnp` 绕过。在系统开放 phone 子进程之前，下一跳是：

- 等华为把 Native ChildProcess 扩到 phone，并且另做「子进程里如何提供交互式 shell / PTY」的设计（官方 ChildProcess 不是 `forkpty+bash`）；或
- 进程内用户态执行（例如把 qemu-user 链成 `.so`）。那是架构级工作，本仓库尚未实现。

### 本修复后如何在设备上验证

这是 **native C++ / ArkTS** 改动，不需要重编 HNP。设备上已装的 `base.hnp` 可以保留。

1. 用当前源码重编 HAP（DevEco 或 `./build-linux.sh`），bundle 仍为 `com.alvin.termony`。
2. 用已对齐该 bundle 的调试证书签名，`hdc install -r` 覆盖安装。
3. 启动应用：**不应再 CppCrash**。若 shell 起不来，终端会显示 `[termony] failed to start shell` 和 errno，并弹出 Toast。
4. 抓日志：`hdc shell hilog | grep testTag`，查找 `forkpty:` / `openpty:` / `posix_spawn:` / `shell started via`。
5. 若日志出现 `shell started via`，在终端里试 `pwd`、`echo ok`、`ls`。若只有失败 notice，则 phone 当前禁止为该应用创建进程，需要上面的下一跳。

## 本阶段不做的事

- 不重写终端、PTY、HNP 加载架构
- 不把 MateBook 专用文案改成手机文案（README 仍描述 Computer 场景）
- 不在本仓库提交预编译 `.hnp`
