// 大肥鱼开关 · Win32 小工具
// 一键按正确顺序开关本机的「大肥鱼」：先起大脑（AstrBot），等它起来再起登 QQ 的程序（NapCat）；
// 关的时候反着来：先关 NapCat，再关大脑——这样 QQ 的登录态更容易保住，下次不用重新扫码。
// 另带「暂停/恢复开机自启」。
//
// 编译（MinGW-w64）：
//   g++ main.cpp -o 大肥鱼开关.exe -mwindows -O2 -static -static-libgcc -static-libstdc++ \
//       -finput-charset=UTF-8 -lshell32 -lws2_32 -lole32
//
// 命令行： 大肥鱼开关.exe --selftest    只把状态写进日志文件后退出（不弹窗，方便脚本核对）

#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <shellapi.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#include <string>
#include <vector>

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ws2_32.lib")

// ---------------- 固定路径（要搬家就改这里） ----------------
static const wchar_t* DIR = L"D:\\dafeiyu";
static const wchar_t* BRAIN_CMD = L"D:\\dafeiyu\\start-astrbot.cmd";
static const wchar_t* NAPCAT_CMD = L"D:\\dafeiyu\\start-napcat.cmd";
static const wchar_t* STATE_PY = L"D:\\dafeiyu\\tools\\backend_state.py";
static const wchar_t* QR_PAGE = L"D:\\dafeiyu\\扫码登录.html";
static const wchar_t* NAPCAT_LOG =
    L"D:\\dafeiyu\\NapCat\\NapCat.52230.Shell\\napcat-run.log";
static const wchar_t* PYBIN =
    L"D:\\dafeiyu\\uv\\python\\cpython-3.12-windows-x86_64-none\\python.exe";
static const wchar_t* PAUSE_DIR = L"D:\\dafeiyu\\startup-paused";
static const wchar_t* SELFTEST_LOG = L"D:\\dafeiyu\\docs\\switch-selftest.txt";

// 开机自启里属于「她」的项；暂停自启时挪走、恢复时挪回
static const wchar_t* STARTUP_ITEMS[] = {
    L"大肥鱼-AstrBot.lnk",
    L"大肥鱼-NapCat.lnk",
    L"大肥鱼-掉线提醒.lnk",
    L"大肥鱼邮件提醒.vbs",
    L"启动Breeze语音.lnk",
    L"Ollama.lnk",
};

// ---------------- 控件 ID ----------------
enum {
    ID_BTN_START = 1001,
    ID_BTN_STOP,
    ID_BTN_PAUSE,
    ID_BTN_RESUME,
    ID_BTN_REFRESH,
    ID_CHK_ALL,
    ID_TXT_STATUS,
    ID_EDIT_LOG,
    WM_APP_LOG = WM_APP + 1,
    WM_APP_BUSY = WM_APP + 2,
};

static HWND g_hwnd = nullptr;
static HWND g_status = nullptr;
static HWND g_log = nullptr;
static HWND g_chk_all = nullptr;
static HWND g_btnStart = nullptr;
static HWND g_btnStop = nullptr;
static HWND g_btnPause = nullptr;
static HWND g_btnResume = nullptr;
static volatile LONG g_busy = 0;

// ---------------- 小工具 ----------------
static std::wstring StartupDir() {
    wchar_t buf[MAX_PATH] = {0};
    DWORD n = GetEnvironmentVariableW(L"APPDATA", buf, MAX_PATH);
    std::wstring p = (n > 0 && n < MAX_PATH) ? std::wstring(buf, n) : std::wstring();
    p += L"\\Microsoft\\Windows\\Start Menu\\Programs\\Startup";
    return p;
}

static std::wstring ExeDir() {
    wchar_t buf[MAX_PATH] = {0};
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring p(buf);
    size_t i = p.find_last_of(L'\\');
    return i == std::wstring::npos ? p : p.substr(0, i);
}

static std::wstring Join(const std::wstring& a, const std::wstring& b) {
    if (a.empty()) return b;
    std::wstring r = a;
    if (r.back() != L'\\') r += L'\\';
    return r + b;
}

static bool FileExists(const std::wstring& p) {
    DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES;
}

static void Log(const std::wstring& line) {
    if (!g_hwnd) return;
    std::wstring* heap = new std::wstring(line + L"\r\n");
    PostMessageW(g_hwnd, WM_APP_LOG, 0, reinterpret_cast<LPARAM>(heap));
}

static void SetBusy(bool busy) {
    InterlockedExchange(&g_busy, busy ? 1 : 0);
    if (!g_hwnd) return;
    PostMessageW(g_hwnd, WM_APP_BUSY, busy ? 1 : 0, 0);
}

static void SleepMs(int ms) { Sleep(static_cast<DWORD>(ms)); }

// ---------------- 端口探测：真去连一次（这台机器的 TCP 表不可靠） ----------------
static bool PortOpen(int port) {
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) return false;
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<u_short>(port));
    InetPtonW(AF_INET, L"127.0.0.1", &addr.sin_addr);

    u_long nb = 1;
    ioctlsocket(s, FIONBIO, &nb);
    bool ok = false;
    if (connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0) {
        ok = true;
    } else if (WSAGetLastError() == WSAEWOULDBLOCK) {
        fd_set wf, ef;
        FD_ZERO(&wf);
        FD_ZERO(&ef);
        FD_SET(s, &wf);
        FD_SET(s, &ef);
        timeval tv{0, 800 * 1000};  // 800ms 超时
        int r = select(0, nullptr, &wf, &ef, &tv);
        if (r > 0 && FD_ISSET(s, &wf)) {
            int err = 0;
            int len = sizeof(err);
            getsockopt(s, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&err), &len);
            ok = (err == 0);
        }
    }
    closesocket(s);
    return ok;
}

// 用 Skill 自己的探针（已修好端口误判）判断大脑/小号状态
struct BrainInfo {
    bool up = false;
    bool online = false;
    int port = 0;
};

static BrainInfo ReadBrainState() {
    BrainInfo info;
    std::wstring tmp = Join(ExeDir(), L"dfy-state.json");

    // 句柄必须可继承，否则子进程的 stdout 指过去是空的（踩过：文件 0 字节）
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    HANDLE hOut = CreateFileW(tmp.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &sa, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hOut == INVALID_HANDLE_VALUE) return info;

    // 命令行必须以「exe 全路径」开头，否则 Python 会把 "-X" 当成程序名（踩过）
    std::wstring cmd = L"\"";
    cmd += PYBIN;
    cmd += L"\" -X utf8 \"";
    cmd += STATE_PY;
    cmd += L"\" --dir \"";
    cmd += DIR;
    cmd += L"\"";
    std::vector<wchar_t> buf(cmd.begin(), cmd.end());
    buf.push_back(0);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = hOut;
    si.hStdError = hOut;
    si.hStdInput = nullptr;
    PROCESS_INFORMATION pi{};
    BOOL ok = CreateProcessW(nullptr, buf.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
                             nullptr, nullptr, &si, &pi);
    if (ok) {
        WaitForSingleObject(pi.hProcess, 20000);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
    CloseHandle(hOut);
    if (!ok) return info;

    HANDLE h = CreateFileW(tmp.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return info;
    char data[8192] = {0};
    DWORD got = 0;
    ReadFile(h, data, sizeof(data) - 1, &got, nullptr);
    CloseHandle(h);
    data[got] = 0;
    std::string s(data);
    info.up = s.find("\"state\": \"up\"") != std::string::npos;
    info.online = s.find("\"online\": true") != std::string::npos;
    return info;
}

static bool NapCatLogHasQR() {
    HANDLE h = CreateFileW(NAPCAT_LOG, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{}, pos{};
    GetFileSizeEx(h, &size);
    const long long tail = 4000;
    pos.QuadPart = size.QuadPart > tail ? size.QuadPart - tail : 0;
    SetFilePointerEx(h, pos, nullptr, FILE_BEGIN);
    char buf[4200] = {0};
    DWORD got = 0;
    ReadFile(h, buf, sizeof(buf) - 1, &got, nullptr);
    CloseHandle(h);
    std::string s(buf, got);
    // 日志里出现「二维码已保存」且之后没有「登录成功」
    size_t q = s.find("二维码已保存");
    if (q == std::string::npos) return false;
    size_t ok = s.find("登录成功");
    return ok == std::string::npos || ok < q;
}

static void RunHidden(const std::wstring& cmdline) {
    std::wstring full = L"cmd.exe /c " + cmdline;
    std::vector<wchar_t> buf(full.begin(), full.end());
    buf.push_back(0);
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi{};
    if (CreateProcessW(nullptr, buf.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr,
                       nullptr, &si, &pi)) {
        WaitForSingleObject(pi.hProcess, 20000);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
}

// 起一个 .cmd，给它自己的控制台窗口（跟平时双击一样，能看见日志）
static bool StartConsoleCmd(const std::wstring& cmdPath) {
    if (!FileExists(cmdPath)) {
        Log(L"找不到启动脚本：" + cmdPath);
        return false;
    }
    std::wstring full = L"cmd.exe /c \"" + cmdPath + L"\"";
    std::vector<wchar_t> buf(full.begin(), full.end());
    buf.push_back(0);
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    BOOL ok = CreateProcessW(nullptr, buf.data(), nullptr, nullptr, FALSE, CREATE_NEW_CONSOLE,
                             nullptr, nullptr, &si, &pi);
    if (ok) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        return true;
    }
    Log(L"启动失败（错误码 " + std::to_wstring(GetLastError()) + L"）：" + cmdPath);
    return false;
}

// ---------------- 动作 ----------------
static void DoStart() {
    Log(L"—— 开始启动 ——");
    bool anything = false;
    if (!PortOpen(6185)) {
        Log(L"1/2 启动大脑（AstrBot）……");
        if (StartConsoleCmd(BRAIN_CMD)) {
            anything = true;
            bool up = false;
            for (int i = 0; i < 60; ++i) {  // 最多等 90 秒
                SleepMs(1500);
                if (PortOpen(6185)) {
                    up = true;
                    break;
                }
            }
            Log(up ? L"    大脑起来了。" : L"    大脑 90 秒没起来，看那个黑窗口里的报错。");
        }
    } else {
        Log(L"1/2 大脑已经在跑，跳过。");
    }

    BrainInfo b = ReadBrainState();
    if (!b.online) {
        Log(L"2/2 启动登 QQ 的程序（NapCat）……");
        if (StartConsoleCmd(NAPCAT_CMD)) {
            anything = true;
            bool online = false;
            for (int i = 0; i < 60; ++i) {
                SleepMs(1500);
                BrainInfo x = ReadBrainState();
                if (x.online) {
                    online = true;
                    break;
                }
                if (NapCatLogHasQR()) break;
            }
            if (online) {
                Log(L"    小号上线了，她可以聊天了。");
            } else if (NapCatLogHasQR()) {
                Log(L"    QQ 要重新扫码（快速登录过期）。已给你打开扫码页，扫完就好了。");
                ShellExecuteW(nullptr, L"open", QR_PAGE, nullptr, nullptr, SW_SHOWNORMAL);
            } else {
                Log(L"    还没连上。稍等一会儿点「刷新」，或者打开扫码页看看。");
            }
        }
    } else {
        Log(L"2/2 小号已经在线，跳过。");
    }
    if (!anything) Log(L"没有需要启动的东西。");
    Log(L"—— 完成 ——");
}

static void DoStop() {
    Log(L"—— 开始关闭（先 QQ，后大脑）——");
    Log(L"1/2 关登 QQ 的程序……");
    RunHidden(L"taskkill /F /T /IM NapCatWinBootMain.exe >nul 2>&1");
    SleepMs(2500);
    Log(L"2/2 关大脑……");
    RunHidden(L"taskkill /F /T /IM astrbot.exe >nul 2>&1");

    if (SendMessageW(g_chk_all, BM_GETCHECK, 0, 0) == BST_CHECKED) {
        Log(L"顺带关语音和记忆（省内存/显卡）……");
        RunHidden(L"taskkill /F /IM breeze-server.exe >nul 2>&1");
        RunHidden(L"taskkill /F /IM ollama.exe >nul 2>&1");
    }
    SleepMs(1200);
    BrainInfo b = ReadBrainState();
    Log((!b.up && !b.online) ? L"已关闭。想再开点「启动她」。"
                             : L"还有进程没退干净，再点一次「关闭她」，或用任务管理器看。");
    Log(L"—— 完成 ——");
}

static void DoPauseAutostart() {
    std::wstring sd = StartupDir();
    CreateDirectoryW(PAUSE_DIR, nullptr);
    int moved = 0, found = 0;
    DWORD lastErr = 0;
    for (const wchar_t* name : STARTUP_ITEMS) {
        std::wstring src = Join(sd, name);
        if (!FileExists(src)) continue;
        ++found;
        std::wstring dst = Join(PAUSE_DIR, name);
        // 跨盘移动（C: 的启动文件夹 → D: 的暂停目录）必须带 MOVEFILE_COPY_ALLOWED
        if (MoveFileExW(src.c_str(), dst.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED)) {
            ++moved;
            Log(std::wstring(L"  移走 ") + name);
        } else {
            lastErr = GetLastError();
            Log(std::wstring(L"  移走失败 ") + name + L"（错误码 " + std::to_wstring(lastErr) + L"）");
        }
    }
    if (moved > 0)
        Log(L"已暂停开机自启（移走 " + std::to_wstring(moved) + L" 项）。下次开机她不会自己起来。");
    else if (found > 0)
        Log(L"找到 " + std::to_wstring(found) + L" 项，但一项都没移走（错误码 " +
            std::to_wstring(lastErr) + L"）。多半是杀毒软件拦了，或启动文件夹被占用。");
    else
        Log(L"启动文件夹里没有她的自启项（可能已经暂停了，点「恢复开机自启」能放回去）。");
}

static void DoResumeAutostart() {
    std::wstring sd = StartupDir();
    int moved = 0, found = 0;
    DWORD lastErr = 0;
    for (const wchar_t* name : STARTUP_ITEMS) {
        std::wstring src = Join(PAUSE_DIR, name);
        if (!FileExists(src)) continue;
        ++found;
        std::wstring dst = Join(sd, name);
        if (MoveFileExW(src.c_str(), dst.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED)) {
            ++moved;
            Log(std::wstring(L"  放回 ") + name);
        } else {
            lastErr = GetLastError();
            Log(std::wstring(L"  放回失败 ") + name + L"（错误码 " + std::to_wstring(lastErr) + L"）");
        }
    }
    if (moved > 0)
        Log(L"已恢复开机自启（放回 " + std::to_wstring(moved) + L" 项）。");
    else if (found > 0)
        Log(L"找到 " + std::to_wstring(found) + L" 项，但一项都没放回（错误码 " +
            std::to_wstring(lastErr) + L"）。");
    else
        Log(L"没有可恢复的项（可能本来就没暂停）。");
}

// ---------------- 状态栏 ----------------
static void RefreshStatus() {
    BrainInfo b = ReadBrainState();
    std::wstring s;
    s += b.up ? L"大脑：开着\r\n" : L"大脑：没开\r\n";
    s += b.online ? L"小号：在线（她能聊天）\r\n" : L"小号：没连上（她收不到消息）\r\n";
    s += PortOpen(8137) ? L"语音：开着\r\n" : L"语音：没开（只影响发语音）\r\n";
    s += PortOpen(11434) ? L"记忆：开着" : L"记忆：没开（不影响聊天）";
    SetWindowTextW(g_status, s.c_str());

    bool paused = false;
    for (const wchar_t* name : STARTUP_ITEMS) {
        if (FileExists(Join(PAUSE_DIR, name))) {
            paused = true;
            break;
        }
    }
    SetWindowTextW(g_btnPause, paused ? L"自启：已暂停" : L"暂停开机自启");
    EnableWindow(g_btnPause, TRUE);
}

// ---------------- 线程跑动作 ----------------
typedef void (*ActionFn)();

static DWORD WINAPI ActionThread(LPVOID param) {
    ActionFn fn = reinterpret_cast<ActionFn>(param);
    SetBusy(true);
    fn();
    SetBusy(false);
    if (g_hwnd) PostMessageW(g_hwnd, WM_APP_LOG, 1, 0);
    return 0;
}

static void RunAsync(ActionFn fn) {
    if (InterlockedCompareExchange(&g_busy, 1, 0) != 0) {
        Log(L"正在忙，等这一轮做完再点。");
        return;
    }
    InterlockedExchange(&g_busy, 0);
    HANDLE h = CreateThread(nullptr, 0, ActionThread, reinterpret_cast<LPVOID>(fn), 0, nullptr);
    if (h) CloseHandle(h);
}

// ---------------- 自检模式 ----------------
static int SelfTest() {
    CreateDirectoryW(L"D:\\dafeiyu\\docs", nullptr);
    BrainInfo b = ReadBrainState();
    std::wstring out = L"自检 ";
    out += L"\r\n大脑(6185)=" + std::to_wstring(PortOpen(6185) ? 1 : 0);
    out += L"\r\nstate.up=" + std::to_wstring(b.up ? 1 : 0);
    out += L"\r\nonline=" + std::to_wstring(b.online ? 1 : 0);
    out += L"\r\n语音(8137)=" + std::to_wstring(PortOpen(8137) ? 1 : 0);
    out += L"\r\n记忆(11434)=" + std::to_wstring(PortOpen(11434) ? 1 : 0);
    out += L"\r\n启动项:";
    for (const wchar_t* name : STARTUP_ITEMS) {
        out += L"\r\n  ";
        out += name;
        out += FileExists(Join(StartupDir(), name)) ? L" = 在启动文件夹"
                                                    : L" = 不在（或已暂停）";
    }
    out += L"\r\n暂停目录有: " + std::to_wstring((int)FileExists(PAUSE_DIR));
    HANDLE h = CreateFileW(SELFTEST_LOG, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h != INVALID_HANDLE_VALUE) {
        std::string utf8;
        int need = WideCharToMultiByte(CP_UTF8, 0, out.c_str(), -1, nullptr, 0, nullptr, nullptr);
        utf8.resize(need > 0 ? need : 0);
        if (need > 0)
            WideCharToMultiByte(CP_UTF8, 0, out.c_str(), -1, utf8.data(), need, nullptr, nullptr);
        DWORD w = 0;
        WriteFile(h, utf8.c_str(), static_cast<DWORD>(utf8.size() > 0 ? utf8.size() - 1 : 0), &w,
                  nullptr);
        CloseHandle(h);
    }
    return 0;
}

// ---------------- 窗口 ----------------
static HFONT MakeFont(int size, bool bold) {
    return CreateFontW(size, 0, 0, 0, bold ? FW_BOLD : FW_NORMAL, FALSE, FALSE, FALSE,
                       DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                       CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei");
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        g_hwnd = hwnd;
        HINSTANCE inst = reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(hwnd, GWLP_HINSTANCE));
        HFONT f = MakeFont(16, false);
        HFONT fb = MakeFont(18, true);
        HFONT fs = MakeFont(14, false);

        HWND title = CreateWindowW(L"STATIC", L"大肥鱼开关", WS_CHILD | WS_VISIBLE, 16, 12, 300, 24,
                                   hwnd, nullptr, inst, nullptr);
        SendMessageW(title, WM_SETFONT, reinterpret_cast<WPARAM>(fb), TRUE);

        g_status = CreateWindowW(L"STATIC", L"读状态中……", WS_CHILD | WS_VISIBLE, 20, 44, 400, 80,
                                 hwnd, (HMENU)ID_TXT_STATUS, inst, nullptr);
        SendMessageW(g_status, WM_SETFONT, reinterpret_cast<WPARAM>(f), TRUE);

        g_btnStart = CreateWindowW(L"BUTTON", L"启动她", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                   20, 132, 110, 34, hwnd, (HMENU)ID_BTN_START, inst, nullptr);
        g_btnStop = CreateWindowW(L"BUTTON", L"关闭她", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 140,
                                  132, 110, 34, hwnd, (HMENU)ID_BTN_STOP, inst, nullptr);
        g_btnPause = CreateWindowW(L"BUTTON", L"暂停开机自启", WS_CHILD | WS_VISIBLE, 260, 132, 150,
                                   34, hwnd, (HMENU)ID_BTN_PAUSE, inst, nullptr);
        g_btnResume = CreateWindowW(L"BUTTON", L"刷新状态", WS_CHILD | WS_VISIBLE, 20, 172, 110, 30,
                                    hwnd, (HMENU)ID_BTN_REFRESH, inst, nullptr);

        g_chk_all = CreateWindowW(L"BUTTON", L"关闭时连语音和记忆一起关（省内存/显卡）",
                                  WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 140, 176, 300, 24, hwnd,
                                  (HMENU)ID_CHK_ALL, inst, nullptr);
        SendMessageW(g_chk_all, WM_SETFONT, reinterpret_cast<WPARAM>(fs), TRUE);

        g_log = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL |
                                    ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY,
                                20, 210, 400, 150, hwnd, (HMENU)ID_EDIT_LOG, inst, nullptr);
        SendMessageW(g_log, WM_SETFONT, reinterpret_cast<WPARAM>(fs), TRUE);

        HWND tip = CreateWindowW(L"STATIC",
                                 L"提示：先 QQ 后大脑，登录态才不容易失效；关掉不会丢记忆和聊天记录。",
                                 WS_CHILD | WS_VISIBLE, 20, 368, 420, 22, hwnd, nullptr, inst,
                                 nullptr);
        SendMessageW(tip, WM_SETFONT, reinterpret_cast<WPARAM>(fs), TRUE);

        RefreshStatus();
        SetTimer(hwnd, 1, 3000, nullptr);
        return 0;
    }
    case WM_APP_LOG: {
        if (lp) {
            std::wstring* s = reinterpret_cast<std::wstring*>(lp);
            if (wp == 1) {  // 动作结束标记
                delete s;
                RefreshStatus();
            } else {
                int len = GetWindowTextLengthW(g_log);
                SendMessageW(g_log, EM_SETSEL, len, len);
                SendMessageW(g_log, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(s->c_str()));
                delete s;
            }
        }
        return 0;
    }
    case WM_APP_BUSY: {
        BOOL enable = wp ? FALSE : TRUE;
        EnableWindow(g_btnStart, enable);
        EnableWindow(g_btnStop, enable);
        EnableWindow(g_btnPause, enable);
        EnableWindow(g_btnResume, enable);
        if (wp) SetWindowTextW(g_status, L"正在处理，请稍等……");
        return 0;
    }
    case WM_TIMER:
        if (InterlockedCompareExchange(&g_busy, 0, 0) == 0) RefreshStatus();
        return 0;
    case WM_COMMAND: {
        int id = LOWORD(wp);
        if (id == ID_BTN_START) RunAsync(DoStart);
        else if (id == ID_BTN_STOP) RunAsync(DoStop);
        else if (id == ID_BTN_PAUSE) RunAsync(DoPauseAutostart);
        else if (id == ID_BTN_REFRESH) RefreshStatus();
        return 0;
    }
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// 启动打点：排查「为什么没反应」用；正常运行时也会写，文件很小
static void BootMark(const wchar_t* what) {
    wchar_t tmp[MAX_PATH] = {0};
    GetEnvironmentVariableW(L"TEMP", tmp, MAX_PATH);
    std::wstring path = std::wstring(tmp) + L"\\dfy-switch-boot.txt";
    HANDLE h = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    std::wstring line = what;
    line += L"\r\n";
    std::string utf8;
    int need = WideCharToMultiByte(CP_UTF8, 0, line.c_str(), -1, nullptr, 0, nullptr, nullptr);
    utf8.resize(need > 0 ? need : 0);
    if (need > 0)
        WideCharToMultiByte(CP_UTF8, 0, line.c_str(), -1, utf8.data(), need, nullptr, nullptr);
    DWORD w = 0;
    WriteFile(h, utf8.c_str(), static_cast<DWORD>(utf8.size() ? utf8.size() - 1 : 0), &w, nullptr);
    CloseHandle(h);
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE, LPSTR, int) {
    // Winsock 要在任何端口探测之前初始化（自检模式也用得到）
    WSADATA wsa{};
    WSAStartup(MAKEWORD(2, 2), &wsa);

    BootMark(GetCommandLineW());
    // 自检模式
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    BootMark(argc > 1 ? L"有参数" : L"无参数");
    if (argv) {
        for (int i = 1; i < argc; ++i) {
            if (wcscmp(argv[i], L"--selftest") == 0) {
                LocalFree(argv);
                return SelfTest();
            }
        }
        LocalFree(argv);
    }

    WSADATA wsa2{};  // 已经初始化过，这里不再重复；保留变量名以防后续改动
    (void)wsa2;

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = L"DafeiyuSwitchWnd";
    RegisterClassExW(&wc);

    RECT r{0, 0, 452, 410};
    AdjustWindowRect(&r, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE);
    HWND hwnd = CreateWindowW(L"DafeiyuSwitchWnd", L"大肥鱼开关", WS_OVERLAPPED | WS_CAPTION |
                                  WS_SYSMENU | WS_MINIMIZEBOX,
                              CW_USEDEFAULT, CW_USEDEFAULT, r.right - r.left, r.bottom - r.top,
                              nullptr, nullptr, inst, nullptr);
    if (!hwnd) return 1;
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    WSACleanup();
    return 0;
}
