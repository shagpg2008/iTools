# iTool

基于 wxWidgets 的跨平台多工具容器。当前 Windows 构建默认使用：

- `D:\tools\w64devkit`（x86 / i686）
- `src\third_party\distfiles` 中的 wxWidgets 3.2.11 和 Crypto++ 8.9 源码包
- Windows XP SP3 作为最低运行目标
- Release 构建自动执行 `strip --strip-all` 和 `upx --best --lzma`

## 版本号

CMake 配置时自动执行 `git describe --tags --always --dirty=-dirty` 并生成
`build/<平台>/generated/version.h`，无需手工修改源码头文件。例如：

```text
v1.2.0                 正好位于 v1.2.0 标签
v1.2.0-3-ga3f5c2d     标签之后第 3 次提交
v1.2.0-3-ga3f5c2d-dirty  工作区包含未提交修改
```

版本会显示在“帮助 → 关于”，写入 Windows EXE 和 macOS Bundle 元数据，并用于产物
文件名（例如 `iTool-v1.2.0-3-ga3f5c2d.exe`）。发布版本请
创建形如 `v1.2.0` 的 Git 标签；没有版本标签时显示提交短哈希，数值型平台元数据回退
到 `project(iTool VERSION ...)` 中的版本；源码副本不含 `.git` 元数据时也回退为该版本，
不会生成 `iTool-unknown`。

Release 构建会自动裁剪最终可执行文件：Windows 和 Linux 使用
`strip --strip-all`，macOS 使用 Apple Mach-O 对应的 `strip -x`。

二维码编码使用 Project Nayuki 的 QR Code generator（MIT License），许可证文本保留在
`src/third_party/qrcodegen/qrcodegen.hpp` 和 `qrcodegen.cpp` 中。

## Windows 构建

在资源管理器中双击 `build-windows-x86.cmd`，或者在命令行执行：

```bat
build-windows-x86.cmd
```

脚本会校验源码包、自动构建两个静态库并缓存到
`build\dependencies\windows-x86\install`，然后编译 iTool。源码压缩包与
Linux/macOS 共用，并由 `.gitignore` 排除。

wxWidgets 和 Crypto++ 安装目录可以在构建时覆盖：

```bat
build-windows-x86.cmd -DITOOL_WX_ROOT=D:\path\to\wxWidgets-install -DITOOL_CRYPTOPP_ROOT=D:\path\to\cryptopp-install
```

两个参数均可单独设置。未设置时使用上面的默认目录。Crypto++ 目录应包含
`include\cryptlib.h` 和 `lib\libcryptopp.a`（或平台对应的静态库名称）。

生成文件：

```text
build\windows-x86-release\bin\iTool-<git-describe>.exe
```

应用图标源文件位于 `resources/icons/itool-source.png`，Windows 多尺寸图标位于
`resources/windows/itool.ico`。Windows MinGW Release 构建会固定执行 `strip`，
随后使用 UPX 压缩可执行文件。

也可以手工执行：

```bat
set PATH=D:\tools\w64devkit\bin;%PATH%
cmake --preset windows-xp-x86-release
cmake --build --preset windows-xp-x86-release
```

## Linux 构建

需要 C++ 编译器、CMake、Ninja，以及 wxGTK 的系统开发依赖。wxWidgets 和 Crypto++
不再使用系统安装版本：构建脚本会从本地源码包自动构建并静态链接这两个库。
`libgcc`、`libstdc++`、glibc、GTK、X11/Wayland 及系统库保持动态链接。wxGTK 会跨越
共享库边界创建和退出线程；静态 GNU 运行库会与 GTK 加载的 `libgcc_s.so.1` 形成两套
异常展开运行时，导致线程结束时在 `_Unwind_ForcedUnwind` 中异常退出。
wxWidgets 自带的 TIFF、JPEG、PNG、zlib 和 Expat 使用内置版本静态链接，避免把构建
系统的 `libtiff.so.5` 等版本化依赖带到较新的发行版。
Ubuntu/Debian 可安装：

```sh
sudo apt update
sudo apt install build-essential cmake ninja-build libgtk-3-dev \
  libgl1-mesa-dev libglu1-mesa-dev libexpat1-dev libcurl4-openssl-dev
```

构建前把以下两个文件放到 `src/third_party/distfiles/`（压缩包已被 `.gitignore`
排除，不提交仓库）：

```text
wxWidgets-3.2.11.zip
cryptopp890.zip
```

然后执行：

```sh
sh build-linux.sh
```

发行包的最低 glibc 版本由**构建机器的 glibc**决定，不能通过 CMake 参数降低。
因此应在计划支持的最老发行版容器/虚拟机中编译。例如以 Ubuntu 20.04 为基线时，
产物要求 glibc 2.31 或更高；如果需要更老的系统，就要换用相应的老构建镜像。
不要给桌面程序添加 `-static` 来静态链接 glibc，因为 NSS、DNS、locale、GTK 插件等
运行时功能仍依赖系统动态组件。

发行前可检查依赖和实际符号版本：

```sh
artifact_name=$(cat build/linux-release/generated/artifact-name.txt)
ldd "build/linux-release/bin/$artifact_name"
objdump -T "build/linux-release/bin/$artifact_name" | \
  grep -o 'GLIBC_[0-9.]*' | sort -Vu | tail -1
```

首次执行会校验压缩包 SHA256、解压并构建依赖；之后在版本、平台、架构和源码包
没有变化时复用 `build/dependencies/` 中的结果。可设置 `ITOOL_BUILD_JOBS` 控制并行数。
主程序完成后，脚本还会检查最终动态依赖；如果仍链接到共享版 wxWidgets 或
Crypto++，或者直接依赖动态 libtiff，构建会直接失败。

依赖构建完成后，主工程对应的手工命令为：

```sh
deps_prefix="$PWD/build/dependencies/linux-$(uname -m)/install"
cmake -S . -B build/linux-release -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DITOOL_STATIC_THIRD_PARTY=ON \
  -DwxWidgets_CONFIG_EXECUTABLE="$deps_prefix/bin/wx-config" \
  -DITOOL_CRYPTOPP_ROOT="$deps_prefix"
cmake --build build/linux-release
ctest --test-dir build/linux-release --output-on-failure
```

生成的 ELF 文件位于 `build/linux-release/bin/iTool-<git-describe>`，可双击且带应用图标的入口是
`build/linux-release/bin/iTool.desktop`。Linux 的裸 ELF 文件本身不能嵌入文件管理器
图标；旁边的 `itool.png` 仅供程序窗口使用，不会改变 ELF 文件图标。
Linux Release 产物使用传统 `ET_EXEC` 格式，避免 Ubuntu 文件管理器把默认 PIE 文件
误识别为“shared library”；命令行运行和 `.desktop` 启动方式不受影响。

首次从部分文件管理器启动 `.desktop` 时，需要右键选择“允许启动/信任并启动”。
正式安装会把启动器和图标放入标准应用菜单位置：

```sh
sudo cmake --install build/linux-release --prefix /usr/local
```

## macOS 构建

先安装 Xcode Command Line Tools、CMake 和 Ninja。与 Linux 相同，脚本使用
`src/third_party/distfiles/` 中固定版本的源码包自动构建静态 wxWidgets 和 Crypto++。

```sh
xcode-select --install
brew install cmake ninja
```

然后执行：

```sh
sh build-macos.sh
```

脚本默认设置 `CMAKE_OSX_DEPLOYMENT_TARGET=11.0`。可以覆盖最低系统版本，例如：

```sh
MACOSX_DEPLOYMENT_TARGET=12.0 sh build-macos.sh
```

该 deployment target 必须不低于所用 Xcode SDK 和所有静态依赖实际支持的版本。
macOS 的 Cocoa、System、libSystem 等平台库仍会动态链接，这是正常且必要的。

依赖构建完成后，主工程对应的手工命令为：

```sh
deps_prefix="$PWD/build/dependencies/macos-$(uname -m)/install"
cmake -S . -B build/macos-release -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DITOOL_STATIC_THIRD_PARTY=ON -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0 \
  -DwxWidgets_CONFIG_EXECUTABLE="$deps_prefix/bin/wx-config" \
  -DITOOL_CRYPTOPP_ROOT="$deps_prefix"
cmake --build build/macos-release
ctest --test-dir build/macos-release --output-on-failure
```

生成文件位于 `build/macos-release/bin/iTool-<git-describe>.app`。Apple Silicon 与 Intel Mac
均默认使用当前机器架构；需要指定架构时，可在配置命令后增加
`-DCMAKE_OSX_ARCHITECTURES=arm64` 或 `-DCMAKE_OSX_ARCHITECTURES=x86_64`。

如果需要强制重建两个第三方库，删除对应平台的缓存目录后重新运行脚本：

```sh
rm -rf build/dependencies/linux-$(uname -m)   # Linux
rm -rf build/dependencies/macos-$(uname -m)   # macOS
```

Windows、Linux、macOS 上的通信客户端和服务器均支持 TCP、UDP、WebSocket（RFC 6455，
ASCII 使用 Text 帧、Hex 使用 Binary 帧，支持分片及 Ping/Pong）和串口。WebSocket 当前为明文 `ws://`，可配置请求路径，
暂不提供 TLS `wss://`。串口波特率
预置到 921600，并允许手动输入 1～4000000；Linux 使用 `termios2/BOTHER`，macOS
使用 `IOSSIOSPEED` 设置非标准波特率。访问串口设备需要当前用户具备相应权限。

MQTT 客户端兼容 MQTT 3.1.1 Broker：以 Clean Session、QoS 0 连接，订阅响应主题并向
请求主题发布每个测试帧。MQTT 服务器是单连接轻量测试端点，处理 CONNECT、SUBSCRIBE、
PUBLISH、PINGREQ 和 DISCONNECT，并按 Payload 映射发布响应；它不是通用 MQTT Broker。
当前不支持 MQTT TLS、用户名密码、QoS 1/2、保留消息或持久会话。

Linux 和 macOS 的用户数据统一保存在 `~/.iTool/`：配置文件为
`~/.iTool/iTool.ini`，通信日志位于 `~/.iTool/logs/`。Windows 仍在可执行文件目录
保存 `iTool.ini` 和 `logs/`。

## 多国语言

iTool 使用 wxWidgets gettext 目录。当前提供英文、简体中文和繁体中文，首次启动跟随
系统语言；从旧版本升级时保持简体中文。可在“设置 → 语言”中修改，重启后生效。

翻译源文件采用扁平目录：`locale/iTool.pot` 以及 `locale/<语言代码>.po`。
当前提供英语、保加利亚语、日语、捷克语、西班牙语、爱沙尼亚语、克罗地亚语、
意大利语、波兰语、巴西葡萄牙语、俄语、斯洛文尼亚语、土耳其语、越南语、韩语和简繁中文。CMake 构建时会自动生成 gettext
要求的运行时目录和 `iTool.mo`；也可以手动编译单个目录：

```powershell
python tools/compile_mo.py locale/zh_CN.po build/locale/zh_CN/LC_MESSAGES/iTool.mo
```

新增或删除 `ITOOL_TR` 文案后，运行 `python tools/update_translations.py` 更新
模板和三个语言目录。语言菜单由 `SupportedLanguages()` 清单生成，不需要在
界面代码中重复维护菜单项。`python tools/check_translations.py` 会检查缺失、
多余文案以及 `%s`、`%u` 等格式占位符，CI 也会通过 CTest 执行该检查。

新增界面文案使用英文源文案和 `ITOOL_TR("Message")`，并在各 PO 文件中提供
翻译。协议名、算法名、配置键和工具 ID 不应翻译。下拉框状态保存使用索引或
稳定值，不要保存翻译后的显示文本。

## 模块开发

`src/core/IToolModule.h` 是工具模块契约。新增工具时，应将界面、平台无关逻辑和
必要的测试分别放入 `src/tools` 与 `tests`。

“加密/解密”工具基于 Crypto++，支持 AES、SM4、DES、3DES 和 RSA。对称算法提供
GCM、CBC、CTR、ECB 等适用模式，RSA 提供 OAEP-SHA256、OAEP-SHA1 和 PKCS#1 v1.5；
输入输出可独立选择 UTF-8、Hex、Base64 或文件。DES、3DES、ECB 和 PKCS#1 v1.5
在界面中标记为兼容用途；CBC/ECB 可选择 PKCS、Zero、ISO/IEC 7816-4 或 No Padding，
新数据默认推荐 AES-256-GCM。

加密工具支持 AES、SM4、DES/3DES、Blowfish、Twofish、CAST5、IDEA、Serpent、
TEA/XTEA/XXTEA、RC4/5/6、ChaCha20、Salsa20、Camellia、SEED 和 RSA。Crypto++ 8.9
本身不提供 SM2，因此未使用未经验证的自制密码实现代替。

CRC 工具支持 Hex、UTF-8 文本和文件输入，可同时计算18种预置算法，并提供
可编辑 Width、Init、Poly、XorOut、RefIn、RefOut 的自定义行。Windows 文件模式
使用 `CreateFileMapping/MapViewOfFile`，GTK/macOS 使用 `mmap`，以64 MiB 视图分块
遍历，因此32位进程无需把整个大文件读入内存。
增加工具时：

1. 新建 `src/tools/toolXX` 目录。
2. 将与界面无关的逻辑提取为 Service/Model。
3. 使用 `wxPanel` 重写界面。
4. 实现 `IToolModule` 并替换 `MainFrame::RegisterTools()` 中对应占位项。
5. 把新增源文件加入 `CMakeLists.txt`。

不要让业务层包含 MFC 类型、`HWND` 或直接依赖 Windows 控件。

## XP 发布验证

- 必须在干净的 Windows XP SP3 虚拟机或实机上测试。
- 目标机器 CPU 必须支持 SSE2。
- 检查 EXE 导入表，确保没有引用 XP 不存在的系统 API。
- 检查发布目录不依赖 `libgcc`、`libstdc++`、`libwinpthread` 或 wxWidgets DLL。
- 所有新增第三方库都必须单独验证 XP 兼容性。
