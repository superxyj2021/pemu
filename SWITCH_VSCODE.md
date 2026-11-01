# pEMU Nintendo Switch 开发环境 (VSCode + WSL)

本目录下的 `.vscode/*` 配置让 pEMU 的 Switch homebrew 开发直接在 VSCode 中完成:
编译(参考 `switch-dev.yml` / `switch-release.yml`)、无线运行并查看 printf 输出、以及断点调试。

工作方式: 代码在 WSL 中, VSCode 通过 WSL 插件访问, 所有编译/运行/调试工具都跑在 WSL 内。
本配置已按此前提编写 (所有路径、编译器、工具都是 WSL 内的绝对路径)。

环境实测参数:
- Switch 局域网 IP: **192.168.31.132** (`.vscode/settings.json` 的 `pemu.switchIp`)
- WSL 为 **mirrored 网络模式** (`C:\Users\xuyan\.wslconfig`), 本机局域网 IP: 192.168.31.67
- 大气层 GDB server 端口: **22225**; nxlink netloader 端口: **28280**; stdout 回连端口: **28771**

---

## 1. 环境准备 (一次性)

### 1.1 WSL 内安装宿主编译工具

devkitPro 工具链 (交叉编译器 `aarch64-none-elf-gcc`、`nxlink`、`aarch64-none-elf-gdb`) 已装在
`/opt/devkitpro`, `DEVKITPRO=/opt/devkitpro` 环境变量也已生效。

但和 CI (`switch-dev.yml` 里的 `sudo apt -yq install git build-essential cmake zip`) 一样,
**WSL 宿主还缺 `cmake`、`gcc/g++`(build-essential)、`zip`**, 必须先装:

```bash
sudo apt update
sudo apt install -y build-essential cmake zip
```

> 为什么需要宿主 gcc? `make pfbneo.deps` 会用宿主 `gcc/g++` 生成 m68k/ctv/pgm 等
> 头文件生成器 (见 `src/cores/pfbneo/CMakeLists.txt`); `cmake` 是构建系统本身。

### 1.2 VSCode 插件

- **WSL** 插件 (已有)
- **C/C++** (`ms-vscode.cpptools`, 提供 IntelliSense / 任务)。仓库里 `.vscode/extensions.json`
  已声明。注意: 调试**推荐用终端 gdb** (见 5.4), cppdbg 图形界面因大气层 gdbstub 限制
  附加流程不稳定 (见 FAQ)。

### 1.3 掌机侧 (大气层)

| 用途 | 组件 | 说明 |
|---|---|---|
| 运行 + 查看 printf 输出 | `nxlink` (devkitPro 自带) | 走 WiFi 局域网 (或 USB), 掌机在 hbmenu 界面 |
| 断点调试 (GDB) | **大气层内置 GDB server (dmnt.gen2)** | 无需装 sysmodule; Switch 上开 TCP **22225** |

> 为什么用大气层内置 GDB server 而不是 nxlink / sys-gdbstub?
> - `switch-tools` 的 `nxlink -s` 只是"保持连接并转发 app 的 stdout", **不包含 GDB server** (已核实源码);
> - `sys-gdbstub` (mossvr) 已弃坑, 作者 issue 确认在**新固件 (v19+) 上进程列表为空**, 本机实测确实如此
>   (stub 能连上、`monitor help` 正常, 但 `info os processes` 返回空);
> - 大气层 (dmnt.gen2) 自带完整 GDB server: `vAttach`/断点/单步/寄存器/进程列表全支持,
>   **随 Atmosphere 一起更新, 无兼容性问题** (已核实源码: TCP 端口 22225)。

**启用大气层内置 GDB server (只需一次):**
1. 在 SD 卡的 `atmosphere/config/system_settings.ini` (不存在就新建) 中, 加入:
   ```ini
   [atmosphere]
   enable_standalone_gdbstub = u8!0x1
   ```
   (已核实源码: dmnt.gen2 读的就是这个配置项, 默认 `u8!0x0` 关闭; 开启后走 TCP 模式)
2. **完全关机再开机** (或重启), dmnt 启动 GDB server
3. 验证: 掌机开机后, 在 WSL 里
   ```bash
   timeout 3 bash -c "echo > /dev/tcp/192.168.31.132/22225" && echo "22225 OPEN" || echo "22225 closed"
   ```
   应显示 **OPEN**
4. (可选) 移除之前装的 `sys-gdbstub`: 删掉 SD 卡 `atmosphere/contents/4200000000474442/` 目录
5. SD 卡上**预先创建好上传目标目录** `sdmc:/switch/pfbneo/` (hbmenu 不会自动建目录)

### 1.4 Windows 防火墙 (printf 输出的关键!)

app 的 printf 输出链路: **掌机 app → 主动回连电脑的 28771 端口 → nxlink 转发到终端**。
这是"掌机→WSL"的**入站**连接, 会被 Windows 防火墙拦截 (mirrored 模式下 WSL 与 Windows
共享网络栈, 入站由 Windows 防火墙过滤)。**不加规则则永远收不到 printf** (app 回连失败是
静默的, 无任何报错)。

**必须加一次入站规则** (Windows 管理员 PowerShell):
```powershell
netsh advfirewall firewall add rule name="nxlink-28771" dir=in action=allow protocol=TCP localport=28771
```
验证:
```powershell
netsh advfirewall firewall show rule name="nxlink-28771"
```
> 备选方案: 编辑 `C:\Users\xuyan\.wslconfig` 把 `firewall=true` 改为 `firewall=false`,
> 然后 `wsl --shutdown` 重启 WSL (VSCode 需重开)。
> 走 USB (usbipd-win) 时不受此限制, 但本环境以无线为主。

---

## 2. 配置文件说明

| 文件 | 作用 |
|---|---|
| `.vscode/tasks.json` | 构建/运行/查 PID 任务, 命令与 `switch-dev.yml`/`switch-release.yml` 一一对应 |
| `.vscode/launch.json` | 5 个核心的 cppdbg 配置 (含 solib-search-path / targetArchitecture 修复) |
| `.vscode/c_cpp_properties.json` | devkitA64 的 IntelliSense (包含 libnx / portlibs) |
| `.vscode/settings.json` | `pemu.switchIp`=192.168.31.132 / `pemu.switchPort`=22225 |
| `scripts/switch-gdb-pid.sh` | 查掌机上进程 PID (hbloader) |
| `scripts/switch-debug-gdb.sh` | **一键附加调试脚本 (推荐)** |

统一约定: **Debug 与 Release 使用独立构建目录**, 互不覆盖:
- Debug: `cmake-build-switch-debug/`  (编译/运行/调试都走这个)
- Release: `cmake-build-switch-release/`  (只用于打包)
产物: `cmake-build-switch-debug/src/cores/pfbneo/pfbneo.nro` 和同目录下同名 ELF (调试符号)。

---

## 3. 编译 (构建)

打开项目后按 **Ctrl+Shift+B** (或 终端 → 运行生成任务):

- **`pemu-switch: build all (Debug)`** — 默认任务。依次执行 (对应 switch-dev.yml):
  `cmake -DPLATFORM_SWITCH=ON -DCMAKE_BUILD_TYPE=Debug` → `make pfbneo.deps` → `make pfbneo.nro`
- **`pemu-switch: build all (Release)`** — 对应 switch-release.yml, 生成 Release 版 nro
- 单独构建: `pemu-switch: configure (Debug/Release)`、`pemu-switch: build pfbneo (deps + nro)`

> 当前只编译 **pfbneo**。`tasks.json` 只定义了 pfbneo 的任务 (pgen / pnes / psnes / pgba 均未定义),
> 需要时参照 `switch-dev.yml` 添加对应任务并加回 `build all` 的 `dependsOn` 列表;
> `launch.json` 里 5 个核心的调试配置仍全部保留。
> **调试/看输出必须用 Debug 构建**: Release 会 strip 符号 (`-s`)、开高优化, 且 `NDEBUG`
> 会关闭 `initNxLink()` (printf 输出失效)。Release 只用于最终打包。
> 提示: 只改 UI 代码时, 配置过一次后直接跑 `build pfbneo (deps + nro)` 增量编译。
> 每次 `configure` 都会生成 `compile_commands.json` 供 IntelliSense 使用。

**手动等价命令** (与 CI 完全一致):

```bash
cd ~/pemu && mkdir -p cmake-build-switch-debug && cd cmake-build-switch-debug
cmake -G "Unix Makefiles" -DPLATFORM_SWITCH=ON -DCMAKE_BUILD_TYPE=Debug ..
make pfbneo.deps
make pfbneo.nro
```

---

## 4. 运行并查看输出 (nxlink, 无线)

前提: **Debug 构建** (libcross2d 的 `initNxLink()` 只在非 NDEBUG 下启用 `nxlinkStdio()`,
见 `external/libcross2d/source/platforms/switch/nxlink.cpp`)、**Windows 防火墙规则已加** (1.4)。

1. **掌机**: 进入 **hbmenu** → 按 **Y** 激活 netloader (屏幕出现 netloader 状态, 端口 28280
   开放; 离开 hbmenu 或传输失败后需重新按 Y; 屏幕有错误对话框时先按 B 关掉)
2. **VSCode**: 终端 → 运行任务 → **`pemu-switch: run pfbneo wireless (nxlink -a -s)`**
   (USB 版用 `run pfbneo (nxlink -s)`; 两个任务都用 `--path pfbneo/pfbneo.nro`)
3. nro 上传到掌机 **`sdmc:/switch/pfbneo/pfbneo.nro`** 并自动启动, **printf 输出实时显示
   在该任务终端** (nxlink 显示 `starting server` / `server active ...` 后, app 回连即转发)

> **`--path pfbneo/pfbneo.nro` 为什么这么写**: hbmenu 会把路径自动拼到它的根目录
> `sdmc:/switch` 后面 (已核实 v3.6.1 源码), 所以 `pfbneo/pfbneo.nro` →
> `sdmc:/switch/pfbneo/pfbneo.nro`。**不要**写成 `switch/pfbneo/...` (会变成
> `sdmc:/switch/switch/...`); 也不要依赖尾斜杠自动拼名 (hbmenu 只认 `.nro` 结尾,
> 否则报 `File-extension/filename not recognized`)。

> **为什么命令是 `nxlink -s`**: 源码确认不带 `-s` 时 nxlink 上传完就退出, 没人监听 app
> 回连的 stdout 端口 (28771); `-s` 才会保持监听并转发 stdout/stderr。

> **注意清理旧 nxlink 进程**: 之前跑过的 `nxlink -s` 任务如果没关, 会一直占着 28771 端口,
> 导致新任务无法绑定、或输出流向旧终端。可用 `ps aux | grep nxlink` / `kill <pid>` 清理。

---

## 5. 断点调试 (大气层 GDB server)

### 5.1 前置

- 已按 1.3 启用 `enable_standalone_gdbstub` 并重启掌机
- **调试走 WiFi/局域网, 不需要 USB** —— dmnt 在掌机监听 TCP 22225, WSL 直连
- `.vscode/settings.json`: `"pemu.switchIp": "192.168.31.132"`, `"pemu.switchPort": "22225"`
- app 已在掌机上运行 (第 4 步启动)

### 5.2 查 PID (附加目标 = hbloader)

- **VSCode 任务**: `pemu-switch: list switch processes`
- 或终端: `~/pemu/scripts/switch-gdb-pid.sh 192.168.31.132 hbloader`

> **关键**: 从 hbmenu 启动的 homebrew **直接跑在 hbloader 进程里** (NRO 映射进 hbloader
> 执行, 进程列表里不会出现 "pfbneo"), 附加目标是列表最后的 **`hbloader`** 进程。
> hbloader 的 PID 每次启动 app 都会变 (如 135→139→141)。

### 5.3 符号重定位 (断点命中的关键)

实测结论: NRO 被 ASLR 加载到高地址, 而 gdb 从 ELF 算出的地址未重定位 → 断点落在
不存在的内存 → stub 拒绝 → **空心红点永不命中**。解法 (已内置在脚本/launch.json):

1. 建符号链接, 让 gdb 能在宿主机找到 ELF 并按真实基址重定位:
   ```bash
   ln -sf /home/superxyj/pemu/cmake-build-switch-debug/src/cores/pfbneo/pfbneo \
          /home/superxyj/pemu/cmake-build-switch-debug/src/cores/pfbneo/pfbneo.elf
   ```
   (已建好; 重新 configure 清空构建目录后需重建)
2. gdb 侧: `set solib-search-path <目录>` + 附加后 `file` (清未重定位符号) + `sharedlibrary`
   (按真实地址重载)。断点即只解析到真实地址 (实测命中 `UiMain::onUpdate`,
   地址 = 基址 0x122e151000 + 偏移 0x128f6c8)。

### 5.4 推荐的调试方式: 一键脚本 (终端 gdb)

> 图形界面 (F5/cppdbg) 因大气层 gdbstub 不支持 qOffsets/异步暂停, 附加流程不稳定;
> **推荐用终端 gdb**, 功能完全相同, 实测稳定。

**用法 (每次调试):**
```bash
# 自动: 查 PID + 附加 + 符号重定位, 进入 (gdb) 提示符
~/pemu/scripts/switch-debug-gdb.sh 192.168.31.132
```

进入 gdb 后:
```text
(gdb) break ui_main.cpp:55     # 打断点 (任何 文件:行 都行)
(gdb) continue                 # 运行, 命中即停
(gdb) bt                       # 调用堆栈
(gdb) p this                   # 打印变量 (info locals 看全部局部变量)
(gdb) n / s                    # 单步跳过 / 单步入 (回车=重复上一条)
(gdb) c                        # 继续
(gdb) q                        # 退出 (app 继续在掌机跑)
```

等价的手动流程 (了解原理):
```bash
aarch64-none-elf-gdb cmake-build-switch-debug/src/cores/pfbneo/pfbneo
(gdb) set solib-search-path cmake-build-switch-debug/src/cores/pfbneo
(gdb) target extended-remote 192.168.31.132:22225
(gdb) info os processes          # 找到最后一行的 hbloader pid
(gdb) attach 139
(gdb) file                       # 清掉未重定位符号 (关键)
(gdb) sharedlibrary              # 按真实加载地址重载符号 (关键)
(gdb) break pemu_main.cpp:100
(gdb) continue
```

---

## 6. IntelliSense (代码提示/跳转)

`.vscode/c_cpp_properties.json` 已指向 devkitA64 交叉编译器、`__SWITCH__` 宏、
libnx/portlibs 头文件。**先跑一次 configure 任务**生成 `compile_commands.json` 后,
C/C++ 扩展会自动用每个编译单元的精确参数解析。若提示 `switch.h` 找不到, 说明没
configure 或构建目录不叫 `cmake-build-switch-debug`。

---

## 7. 常见问题

| 现象 | 原因 / 解决 |
|---|---|
| `cmake: command not found` / `gcc: not found` | 未装宿主工具: `sudo apt install -y build-essential cmake zip` (1.1) |
| 上传 `Connection to ... failed` / 28280 关闭 | 掌机不在 hbmenu 或 netloader 未激活: 进 hbmenu 按 **Y**; 有错误对话框先按 B |
| 掌机提示 `File-extension/filename not recognized` | `--path` 必须以 `.nro` 结尾 (如 `pfbneo/pfbneo.nro`), 不要尾斜杠自动拼名 |
| 掌机提示 `open: err=2 No such file or directory` | SD 卡缺目标目录 (`sdmc:/switch/pfbneo/`); hbmenu 不自动建目录 |
| 掌机提示 `error getting name length: err=11` | netloader 被上一次失败连接卡住: 按 B 关对话框, Y 重开, 或退出重进 hbmenu |
| **printf 收不到 / nxlink 终端无输出** | **Windows 防火墙没放行 28771** (1.4); 或旧的 nxlink 进程占着端口 |
| `pfbneo` 不出现在 `info os processes` | 正常: 它跑在 `hbloader` 进程里, 附加目标是 hbloader (5.2) |
| attach 连不上 22225 | `system_settings.ini` 没写对 / 改后没重启掌机 / Switch IP 填错 (1.3) |
| 断点空心 / 断点不停 | 符号未重定位: 检查 `pfbneo.elf` 符号链接 + solib-search-path (5.3) |
| 断点不停 (非空心) | 用了 Release 构建 (无 `-g`/被优化), 用 Debug + 同一构建的 ELF |
| 调试暂停 (F6) 无反应 | 正常: 大气层 gdbstub 不支持异步暂停 (vCtrlC), 只能用断点停 |
| F5 报 `parameter 'arch'` | cppdbg 需要 `"targetArchitecture": "arm64"` (launch.json 已加) |
| F5 报 `Commands are only accepted when the process is stopped` | cppdbg 附加流程与 stub 不兼容, 改用终端 gdb (5.4) |
| 调试会话卡死占着 22225 | Shift+F5 结束会话释放连接; 单连接被占时新连接会被 reset |

---

## 8. 日常流程速查

```
改代码 → Ctrl+Shift+B (Debug 构建, 仅 pfbneo)
      → 掌机: hbmenu + Y 激活 netloader
      → 任务 "run pfbneo wireless" 启动 app, 看 printf 输出
      → 终端: scripts/switch-debug-gdb.sh 192.168.31.132
      → (gdb) break xxx.cpp:NN → continue → bt / p / n / s
```

Release 打包: 任务 "pemu-switch: build all (Release)", nro 在 `cmake-build-switch-release/src/cores/pfbneo/` (Debug 不受影响)。