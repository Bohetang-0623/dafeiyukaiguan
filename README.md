# 大肥鱼开关（Dafeiyu Switch）

一个 Windows 小工具：按正确顺序一键开关本机的「大肥鱼」（AstrBot + NapCat），并支持暂停/恢复开机自启。

> 起因：她由「大脑（AstrBot，6185）」和「登 QQ 的程序（NapCat，6199）」两个进程组成，启动要**先大脑、后 NapCat**，关闭要**先 NapCat、后大脑**——顺序错了 QQ 登录态容易失效，下次得重新扫码。这个工具把顺序和等待都替你做了。

当前版本：**v2.2.1**

## 功能

| 按钮 | 行为 |
|---|---|
| **启动她** | 6185 不通就启动大脑并最多等 90 秒；再启动 NapCat 等小号上线；检测到二维码会自动打开扫码页 |
| **关闭她** | 先关 NapCat，隔 2.5 秒再关大脑（可用勾选项把语音、记忆一起关掉） |
| **暂停 / 恢复开机自启** | 把启动文件夹里的自启项移到 `startup-paused`（跨盘移动，可原样复原） |
| **快捷入口** | 打开工作台 / 扫码页 / 日志文件夹 |
| 状态卡 | 每 2.5 秒刷新：大脑 / 小号 / 语音 / 记忆 + 已运行时长、开机自启状态 |
| 操作记录 | 每个动作的实时日志，滚轮平滑滚动 |

命令行 `--selftest`：不开窗口，把状态写到 `docs\switch-selftest.txt`，方便脚本核对。

## 界面

纯 Win32 + GDI+ 自绘（无第三方界面库），带缓动动画：

- 按钮悬停渐变 / 按压缩放回弹 / 点击水波纹
- 状态点颜色插值过渡 + 光晕
- 处理中顶部流光条、日志新行淡入、平滑滚动
- **Per-Monitor V2 DPI 感知**（高分屏不模糊）

## 设计要点

- **端口判断不用系统 TCP 表，而是真去连一次**（`connect` + `select` 700ms 超时）。Windows 的 TCP 表在某些环境下报不出监听状态（同一端口一会儿 `Listen`、一会儿空），按表判断会误报「没开」。
- 小号是否在线，调用同目录工具链的探针脚本（`tools\backend_state.py`）并读它的 JSON，不自己另写一套。
- 关进程用 `taskkill /F /T /IM <镜像名>`，`/T` 连子进程一起收；只按镜像名匹配，不会误杀桌面版 QQ。
- 状态探测在**后台线程**，界面线程只负责绘制，拖动窗口不卡。

## 编译

MinGW-w64（推荐 [w64devkit](https://github.com/skeeto/w64devkit)，解压即用）：

```bat
windres app.rc -o app_res.o --codepage=65001 -I.
g++ main.cpp app_res.o -o 大肥鱼开关.exe -mwindows -O2 -static ^
    -static-libgcc -static-libstdc++ -finput-charset=UTF-8 ^
    -lgdiplus -ldwmapi -lshell32 -lws2_32 -lole32 -lgdi32 -luser32
```

> 别漏 `-lgdiplus`（自绘图形）和 `-ldwmapi`（Win11 圆角窗口）。源码含中文，要 `-finput-charset=UTF-8`。

## 路径约定（要改就改 `main.cpp` 顶部常量）

```cpp
static const wchar_t* DIR        = L"D:\\dafeiyu";
static const wchar_t* BRAIN_CMD  = L"D:\\dafeiyu\\start-astrbot.cmd";
static const wchar_t* NAPCAT_CMD = L"D:\\dafeiyu\\start-napcat.cmd";
static const wchar_t* PAUSE_DIR  = L"D:\\dafeiyu\\startup-paused";
```

## 踩过的坑

1. 子进程重定向的句柄必须用 `SECURITY_ATTRIBUTES{bInheritHandle=TRUE}` 创建，否则输出文件是 0 字节。
2. `CreateProcessW` 的命令行首项必须是 exe 全路径，否则 Python 会把 `-X` 当成程序名。
3. **跨盘移动**（C: 启动文件夹 → D: 暂停目录）必须给 `MoveFileExW` 加 `MOVEFILE_COPY_ALLOWED`，否则 `ERROR_NOT_SAME_DEVICE`。
4. `SolidBrush br(Color(x));` 会被编译器当成函数声明（most vexing parse），写成 `SolidBrush br((Color(x)));`。
5. 自定义结构体别叫 `Status`，会和 GDI+ 的 `Gdiplus::Status` 撞名。
6. 状态探测放界面线程里会卡死拖动（每轮要等 1–2 秒），必须挪到后台线程。

## 图标

鲸鱼形象来自 [Microsoft Fluent Emoji](https://github.com/microsoft/fluentui-emoji)（MIT License），铺在深海军蓝圆角底上。

## 许可

MIT，见 [LICENSE](LICENSE)。
