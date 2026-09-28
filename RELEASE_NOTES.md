# iTool Release Notes

## 版本与产物标识

iTool 使用 `git describe --tags --always --dirty=-dirty` 自动生成版本，不需要手工修改
版本头文件。建议正式发布前在干净工作区创建 `vMAJOR.MINOR.PATCH` 标签，例如
`v1.0.0`。标签后的开发构建形如 `v1.0.0-3-g1a2b3c4`，未提交修改会追加
`-dirty`。版本写入“关于”窗口、Windows VERSIONINFO、macOS Bundle 元数据和产物
文件名。

Release 构建产物：

- Windows：`iTool-<git-describe>.exe`
- Linux：`iTool-<git-describe>` 和 `iTool.desktop`
- macOS：`iTool-<git-describe>.app`

wxWidgets 3.2.11 和 Crypto++ 8.9.0 从项目 `src/third_party/distfiles/` 中的源码包构建，
并静态链接进主程序。Linux/macOS 的平台图形库和系统 C/C++ 运行库保持动态链接。

## 本版本主要功能

- Base64：使用 Crypto++ 在文件或 UTF-8 文本与 Base64 之间转换。
- Bitset64：查看和编辑 64 位无符号整数的每一个二进制位。
- 字符串转换：Hex 字节字符串与 7 位 ASCII 文本双向转换。
- CRC 校验器：支持 CRC8/SMBUS；CRC16/MODBUS、CCITT_FALSE、XMODEM、KERMIT、USB；
  CRC24/LTEA、LTEB、BLE；CRC32、CRC32C、CRC32/MPEG2、POSIX、AUTOSAR、BZIP2；
  CRC64/ECMA、XZ、WE 共 18 种预置算法；自定义算法可设置 Width、Init、Poly、XorOut、
  RefIn、RefOut。
- 哈希校验器：支持 MD5、SHA1、SHA2-224/256/384/512、SHA3-224/256/384/512、
  Keccak-224/256/384/512、RIPEMD-128/160/256/320、BLAKE2s-256、BLAKE2b-256/512、
  SM3 共 22 种算法。
- 关键词替换：使用正则表达式批量替换文件名及多编码文本内容。
- 文件夹同步：比较两个目录，双向同步较新的文件和单边存在的文件。
- 代码行统计：按文件统计代码行、注释行、空白行和总行数。
- 文件名补零：为数字开头的文件名补前导零，使字典序与编号顺序一致。
- 通信客户端：Windows/Linux/macOS 均支持串口、TCP 和 UDP；顺序执行多组请求/响应
  测试并统计结果。串口预置波特率最高 921600，也可手动输入 1～4000000。
- 通信服务器：Windows/Linux/macOS 均支持串口、TCP 和 UDP；接收帧、无序匹配并
  发送配置响应。串口预置波特率最高 921600，也可手动输入 1～4000000。
- 协议解析器：根据 C 结构体定义和字节序解析 Hex 数据流或 BIN 文件。
- 时间戳转换：在 64 位 Unix 时间戳和 UTC 日期时间之间转换。
- 条形码编码：生成带校验码的 Code 128-B 条形码。
- 二维码编码：把 UTF-8 文本编码为可复制或保存的 QR Code 图片。

通信客户端和服务器的界面及日志时间戳精确到毫秒。三个平台均支持串口、TCP 和 UDP。
Linux/macOS 串口支持无、RTS/CTS、XON/XOFF 流控；DTR/DSR 不受 POSIX 串口接口支持。
macOS 不支持标记/空格校验；Linux/macOS 不支持 1.5 个停止位，选择不支持的组合时会
显示明确错误。

## Windows 运行要求

### 最低环境

- 操作系统：Windows XP SP3，32 位（x86），以及更新的兼容 Windows。
- CPU：必须支持 SSE2。
- 架构：发布产物是原生 32 位程序，可在支持 Win32 的 32/64 位 Windows 上运行。
- 权限：普通桌面用户权限；写入程序目录中的配置或日志时，目录必须可写。

### 底层库

- wxWidgets 3.2.11、Crypto++ 8.9.0、MinGW GCC 运行库和线程运行库均静态链接。
- 目标机不需要安装 Visual C++ Redistributable、wxWidgets DLL、Crypto++ DLL、
  `libgcc`、`libstdc++` 或 `libwinpthread` DLL。
- 系统仍需提供 XP 自带的 Win32、Winsock、Common Controls、GDI/User/Shell 等系统 DLL。

Release 构建执行 `strip --strip-all`，并在启用 `ITOOL_ENABLE_UPX` 时执行 UPX 压缩。
正式发布前必须在干净的 Windows XP SP3 环境验证启动、网络、文件选择和退出流程。

## Linux 运行要求

### 最低 ABI 基线

官方兼容构建应在 Ubuntu 20.04 x86_64 完成：

- CPU/ABI：x86_64。
- glibc：2.31 或更高。
- GTK：GTK 3.24 系列，运行时 SONAME 为 `libgtk-3.so.0`。
- C++ 运行库：`libstdc++.so.6`。
- GCC 异常展开库：`libgcc_s.so.1`，必须动态链接；不得改为静态 `libgcc`。
- POSIX 线程：Ubuntu 20.04 的独立 libpthread 或 glibc 2.34+ 中合并后的 pthread。

`GLIBC_2.31` 是构建系统基线，不代表每次构建必然只引用到该版本。发布前必须从最终
ELF 检查最高实际需求：

```sh
artifact_name=$(cat build/linux-release/generated/artifact-name.txt)
artifact="build/linux-release/bin/$artifact_name"

objdump -T "$artifact" | grep -o 'GLIBC_[0-9.]*' | sort -Vu | tail -1
objdump -T "$artifact" | grep -o 'GLIBCXX_[0-9.]*' | sort -Vu | tail -1
readelf -d "$artifact" | grep NEEDED
ldd "$artifact"
```

发布门槛应为：最高 GLIBC 不超过 `GLIBC_2.31`、所有 `ldd` 项都能解析，并且没有动态
`libwx`、`libcryptopp` 或直接 `libtiff` 依赖。Ubuntu 20.04 的 GCC 9 通常对应最高
`GLIBCXX_3.4.28`，但应以命令输出为准，不应仅凭编译器版本填写发布说明。

### 动态运行库

wxWidgets 和 Crypto++ 静态链接；wxWidgets 内置的 JPEG、PNG、TIFF、zlib 和 Expat
也静态链接。GTK 桌面栈保持动态链接，通常包括以下稳定 SONAME：

- `libgtk-3.so.0`、`libgdk-3.so.0`
- `libglib-2.0.so.0`、`libgobject-2.0.so.0`、`libgio-2.0.so.0`
- `libpango-1.0.so.0`、`libpangocairo-1.0.so.0`
- `libcairo.so.2`、`libcairo-gobject.so.2`
- `libgdk_pixbuf-2.0.so.0`、`libatk-1.0.so.0`
- X11/Wayland 及输入法、字体渲染相关平台库

Ubuntu/Debian 目标机一般安装 GTK3 运行包即可拉入其依赖：

```sh
sudo apt install libgtk-3-0
```

Fedora/RHEL 系目标机使用：

```sh
sudo dnf install gtk3
```

以 Ubuntu 20.04 为基线的产物通常可运行于更新的 Ubuntu、Debian、Fedora，以及
glibc 不低于 2.31 且 GTK3 SONAME 齐全的 RHEL 系发行版。跨发行版兼容属于 ABI
兼容，不等同于已测试；正式支持列表应以实际干净系统测试结果为准。

Linux Release 是非 PIE 的 `ET_EXEC`，构建后执行 `strip --strip-all`。这避免部分
Ubuntu 文件管理器把 PIE 误识别为 shared library。建议通过版本化 ELF 或随附的
`iTool.desktop` 启动。

## macOS 运行要求

### 最低环境

- 最低系统：macOS 11.0 Big Sur，由 `CMAKE_OSX_DEPLOYMENT_TARGET=11.0` 控制。
- 架构：构建脚本默认生成当前机器的原生架构；arm64 与 x86_64 产物不能互相替代。
- 若需 Universal 2，应显式设置 `CMAKE_OSX_ARCHITECTURES="arm64;x86_64"`，并验证所有
  静态依赖均包含两个架构切片。

### 底层库

- wxWidgets 3.2.11 和 Crypto++ 8.9.0 静态链接。
- Cocoa、CoreFoundation、Security、SystemConfiguration、libSystem 等 Apple 平台
  Framework/动态库由操作系统提供。
- 不需要 Homebrew 的 wxWidgets 或 Crypto++ 运行库。
- 最低版本最终由 deployment target、使用的 Xcode SDK 和所有静态库共同决定；应在
  macOS 11 干净系统或虚拟机上完成最低版本验证。

macOS Release 对主 Mach-O 执行 `strip -x`。不建议使用 UPX 压缩 Mach-O；发布流程应为
构建、strip、`codesign`、notarization，最后把 `.app` 封装为 ZIP 或 DMG。未经签名或
公证的下载包可能被 Gatekeeper 阻止，这是发布签名问题，不是缺少运行库。

## 配置、日志与升级

- Linux/macOS：`~/.iTool/iTool.ini`，日志目录 `~/.iTool/logs/`。
- Windows 当前构建：配置文件和 `logs/` 位于可执行文件目录。
- 升级程序不会主动删除配置和日志。跨版本替换前建议备份 `iTool.ini`。
- 通信任务运行时应先停止客户端/服务器，再关闭应用或替换程序文件。

## 发布验证清单

1. 在干净、无修改的工作区创建并检出 `vMAJOR.MINOR.PATCH` 标签。
2. 分别运行 Windows、Ubuntu 20.04 和目标 macOS 架构的 Release 构建脚本。
3. 确认文件名、关于窗口和平台元数据中的版本一致，且不包含 `-dirty`。
4. 执行全部 CTest；验证 Base64、CRC、Hash、协议、时间、条码和二维码基本用例。
5. 验证通信客户端完成任务、通信服务器停止以及应用退出时不崩溃。
6. Linux 执行 GLIBC/GLIBCXX/NEEDED/`ldd` 检查，并在最低支持发行版启动测试。
7. Windows 检查 PE 导入表，并在 XP SP3 32 位系统运行测试。
8. macOS 检查 `otool -L`、架构切片、代码签名和 Gatekeeper/notarization 状态。
9. 保存未压缩产物及调试符号；对外发布经过 strip/压缩/签名的版本。
