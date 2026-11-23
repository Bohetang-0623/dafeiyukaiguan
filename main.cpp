// 大肥鱼开关 v2.2 · Win32 + GDI+ 自绘界面
//
// v2.2（照参考图重做视觉）：
//   - 字号整体加大一档，正文用深灰（不再发白），次要文字也压深
//   - 主色改成柔和的靛蓝渐变；「关闭她」改成白底描边的柔和警示（不再实心大红）
//   - 每张卡片内部加中文小标题 + 英文副标题（参考图的排版习惯）
//   - 卡片间距/行高加大，日志正文加深、加大
//
// 编译（w64devkit / MinGW-w64）：
//   windres app.rc -o app_res.o --codepage=65001 -I.
//   g++ main.cpp app_res.o -o 大肥鱼开关.exe -mwindows -O2 -static \
//       -static-libgcc -static-libstdc++ -finput-charset=UTF-8 \
//       -lgdiplus -ldwmapi -lshell32 -lws2_32 -lole32 -lgdi32 -luser32

#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#include <gdiplus.h>
using namespace Gdiplus;

#include <algorithm>
#include <atomic>
#include <cmath>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

// ---------------- 路径 ----------------
static const wchar_t* DIR = L"D:\\dafeiyu";
static const wchar_t* BRAIN_CMD = L"D:\\dafeiyu\\start-astrbot.cmd";
static const wchar_t* NAPCAT_CMD = L"D:\\dafeiyu\\start-napcat.cmd";
static const wchar_t* STATE_PY = L"D:\\dafeiyu\\tools\\backend_state.py";
static const wchar_t* QR_PAGE = L"D:\\dafeiyu\\扫码登录.html";
static const wchar_t* NAPCAT_LOG = L"D:\\dafeiyu\\NapCat\\NapCat.52230.Shell\\napcat-run.log";
static const wchar_t* PYBIN = L"D:\\dafeiyu\\uv\\python\\cpython-3.12-windows-x86_64-none\\python.exe";
static const wchar_t* PAUSE_DIR = L"D:\\dafeiyu\\startup-paused";
static const wchar_t* SELFTEST_LOG = L"D:\\dafeiyu\\docs\\switch-selftest.txt";
static const wchar_t* WORKBENCH_HTML = L"D:\\dafeiyu\\大肥鱼工作台.html";
static const wchar_t* LOG_DIR = L"D:\\dafeiyu\\logs";
static const wchar_t* APP_VERSION = L"2.2.1";

static const wchar_t* STARTUP_ITEMS[] = {
    L"大肥鱼-AstrBot.lnk", L"大肥鱼-NapCat.lnk", L"大肥鱼-掉线提醒.lnk",
    L"大肥鱼邮件提醒.vbs", L"启动Breeze语音.lnk", L"Ollama.lnk",
};

// ---------------- 配色（柔和版：低饱和主色 + 深正文） ----------------
static const ARGB C_BG = 0xFFF6F7FB;
static const ARGB C_CARD = 0xFFFFFFFF;
static const ARGB C_BORDER = 0xFFE7EAF2;
static const ARGB C_TEXT = 0xFF232A36;      // 正文（深）
static const ARGB C_MUTED = 0xFF5D6675;     // 次要（也压深）
static const ARGB C_SUB = 0xFF8B93A2;       // 更弱（英文副标题/页脚）
static const ARGB C_ACCENT = 0xFF5B7BE8;    // 主色（柔和靛蓝）
static const ARGB C_ACCENT2 = 0xFF7E9AF0;   // 主色渐变末端
static const ARGB C_DANGER = 0xFFB8546A;    // 警示（豆沙红，不刺眼）
static const ARGB C_DANGER_BORDER = 0xFFEBD8DD;
static const ARGB C_SOFT = 0xFFEDF1F9;      // 浅底按钮
static const ARGB C_OK = 0xFF2FA96B;
static const ARGB C_BAD = 0xFFD9534F;
static const ARGB C_UNKNOWN = 0xFF98A1B2;
static const ARGB C_LOG_BG = 0xFFFAFBFD;
static const ARGB C_WHITE = 0xFFFFFFFF;

static int g_dpi = 96;
static float S(float v) { return v * g_dpi / 96.0f; }

static const int WIN_W = 566;
static const int WIN_H = 860;

enum { WM_APP_STATE = WM_APP + 10, WM_APP_LOGLINE, WM_APP_DONE };
enum { ID_START, ID_STOP, ID_AUTO, ID_REFRESH, ID_CHECK, ID_WORKBENCH, ID_QR, ID_LOGDIR, ID_COUNT };

static HWND g_hwnd = nullptr;
static bool g_busy = false;
static std::wstring g_busyText;
static ULONGLONG g_busyStart = 0;
static bool g_includeAll = false;

struct SysStatus {
    bool brain = false;
    bool online = false;
    bool voice = false;
    bool memory = false;
    bool paused = false;
    long long started = 0;
    std::wstring detail;
};
static std::mutex g_mu;
static SysStatus g_st;

static std::mutex g_logMu;
static std::deque<std::wstring> g_log;
static float g_logScrollF = 0;
static int g_logScrollTarget = 0;
static float g_logNewFade = 0;

struct BtnAnim {
    float hover = 0, press = 0;
    float rippleR = 0, rippleA = 0, rippleX = 0, rippleY = 0;
};
static BtnAnim g_anim[ID_COUNT];
static float g_dotT[4] = {0, 0, 0, 0};
static float g_shimmer = 0;
static float g_fadeIn = 0;
static int g_hover = -1, g_press = -1;

struct Layout {
    RECT header, card, btnStart, btnStop, btnAuto, btnRefresh, check, card2, btnWb, btnQr, btnLog,
        logBox, footer, pill;
};
static Layout L;

// ---------------- 小工具 ----------------
static std::wstring Join(const std::wstring& a, const std::wstring& b) {
    if (a.empty()) return b;
    std::wstring r = a;
    if (r.back() != L'\\') r += L'\\';
    return r + b;
}
static bool FileExists(const std::wstring& p) {
    return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES;
}
static std::wstring StartupDir() {
    wchar_t buf[MAX_PATH] = {0};
    DWORD n = GetEnvironmentVariableW(L"APPDATA", buf, MAX_PATH);
    std::wstring p = (n > 0 && n < MAX_PATH) ? std::wstring(buf, n) : std::wstring();
    return p + L"\\Microsoft\\Windows\\Start Menu\\Programs\\Startup";
}
static std::wstring ExeDir() {
    wchar_t buf[MAX_PATH] = {0};
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring p(buf);
    size_t i = p.find_last_of(L'\\');
    return i == std::wstring::npos ? p : p.substr(0, i);
}
static void Trace(const wchar_t* what) {
    CreateDirectoryW(L"D:\\dafeiyu\\logs", nullptr);
    HANDLE h = CreateFileW(L"D:\\dafeiyu\\logs\\switch-v2.log", FILE_APPEND_DATA,
                           FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t line[512];
    swprintf(line, 512, L"%02d-%02d %02d:%02d:%02d %s\r\n", st.wMonth, st.wDay, st.wHour, st.wMinute,
             st.wSecond, what);
    std::string utf8;
    int need = WideCharToMultiByte(CP_UTF8, 0, line, -1, nullptr, 0, nullptr, nullptr);
    utf8.resize(need > 0 ? need : 0);
    if (need > 0) WideCharToMultiByte(CP_UTF8, 0, line, -1, utf8.data(), need, nullptr, nullptr);
    DWORD w = 0;
    WriteFile(h, utf8.c_str(), (DWORD)(utf8.size() ? utf8.size() - 1 : 0), &w, nullptr);
    CloseHandle(h);
}
static LONG WINAPI CrashFilter(EXCEPTION_POINTERS* ep) {
    wchar_t buf[256];
    swprintf(buf, 256, L"崩溃 code=0x%08lX",
             ep && ep->ExceptionRecord ? ep->ExceptionRecord->ExceptionCode : 0);
    Trace(buf);
    return EXCEPTION_EXECUTE_HANDLER;
}
static void PushLog(const std::wstring& line) {
    {
        std::lock_guard<std::mutex> lk(g_logMu);
        g_log.push_back(line);
        while (g_log.size() > 400) g_log.pop_front();
        g_logScrollTarget = 0;
        g_logNewFade = 1.0f;
    }
    if (g_hwnd) PostMessageW(g_hwnd, WM_APP_LOGLINE, 0, 0);
}

// ---------------- 端口/探针 ----------------
static bool PortOpen(int port) {
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) return false;
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons((u_short)port);
    InetPtonW(AF_INET, L"127.0.0.1", &addr.sin_addr);
    u_long nb = 1;
    ioctlsocket(s, FIONBIO, &nb);
    bool ok = false;
    if (connect(s, (sockaddr*)&addr, sizeof(addr)) == 0) ok = true;
    else if (WSAGetLastError() == WSAEWOULDBLOCK) {
        fd_set wf, ef;
        FD_ZERO(&wf);
        FD_ZERO(&ef);
        FD_SET(s, &wf);
        FD_SET(s, &ef);
        timeval tv{0, 700 * 1000};
        if (select(0, nullptr, &wf, &ef, &tv) > 0 && FD_ISSET(s, &wf)) {
            int err = 0, len = sizeof(err);
            getsockopt(s, SOL_SOCKET, SO_ERROR, (char*)&err, &len);
            ok = (err == 0);
        }
    }
    closesocket(s);
    return ok;
}
struct Probe { bool up = false, online = false; long long started = 0; };
static Probe ReadProbe() {
    Probe p;
    std::wstring tmp = Join(ExeDir(), L"dfy-state.json");
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    HANDLE hOut = CreateFileW(tmp.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &sa, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hOut == INVALID_HANDLE_VALUE) return p;
    std::wstring cmd = L"\"" + std::wstring(PYBIN) + L"\" -X utf8 \"" + STATE_PY + L"\" --dir \"" + DIR + L"\"";
    std::vector<wchar_t> buf(cmd.begin(), cmd.end());
    buf.push_back(0);
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = hOut;
    si.hStdError = hOut;
    PROCESS_INFORMATION pi{};
    BOOL ok = CreateProcessW(nullptr, buf.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr,
                             nullptr, &si, &pi);
    if (ok) {
        WaitForSingleObject(pi.hProcess, 20000);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
    CloseHandle(hOut);
    if (!ok) return p;
    HANDLE h = CreateFileW(tmp.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return p;
    char data[8192] = {0};
    DWORD got = 0;
    ReadFile(h, data, sizeof(data) - 1, &got, nullptr);
    CloseHandle(h);
    data[got] = 0;
    std::string s(data);
    p.up = s.find("\"state\": \"up\"") != std::string::npos;
    p.online = s.find("\"online\": true") != std::string::npos;
    size_t k = s.find("\"started\": ");
    if (k != std::string::npos) {
        size_t e = s.find_first_of(",}", k);
        try {
            p.started = std::stoll(s.substr(k + 11, e - k - 11));
        } catch (...) {
        }
    }
    return p;
}
static bool NapCatLogQR() {
    HANDLE h = CreateFileW(NAPCAT_LOG, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{}, pos{};
    GetFileSizeEx(h, &size);
    pos.QuadPart = size.QuadPart > 4000 ? size.QuadPart - 4000 : 0;
    SetFilePointerEx(h, pos, nullptr, FILE_BEGIN);
    char buf[4200] = {0};
    DWORD got = 0;
    ReadFile(h, buf, sizeof(buf) - 1, &got, nullptr);
    CloseHandle(h);
    std::string s(buf, got);
    size_t q = s.find("二维码已保存");
    if (q == std::string::npos) return false;
    size_t ok = s.find("登录成功");
    return ok == std::string::npos || ok < q;
}

// ---------------- 后台探测线程 ----------------
static std::atomic<bool> g_stopProbe{false};
static DWORD WINAPI ProbeThread(LPVOID) {
    int tick = 0;
    while (!g_stopProbe.load()) {
        SysStatus s;
        s.brain = PortOpen(6185);
        s.voice = PortOpen(8137);
        s.memory = PortOpen(11434);
        bool paused = false;
        for (const wchar_t* n : STARTUP_ITEMS) {
            if (FileExists(Join(PAUSE_DIR, n))) {
                paused = true;
                break;
            }
        }
        s.paused = paused;
        if (s.brain) {
            if (tick % 2 == 0) {
                Probe p = ReadProbe();
                s.brain = p.up || s.brain;
                s.online = p.online;
                s.started = p.started;
                if (!p.online) s.detail = L"没连上（点「启动她」）";
            } else {
                std::lock_guard<std::mutex> lk(g_mu);
                s.online = g_st.online;
                s.started = g_st.started;
                s.detail = g_st.detail;
            }
        }
        {
            std::lock_guard<std::mutex> lk(g_mu);
            g_st = s;
        }
        if (g_hwnd) PostMessageW(g_hwnd, WM_APP_STATE, 0, 0);
        ++tick;
        for (int i = 0; i < 20 && !g_stopProbe.load(); ++i) Sleep(100);
    }
    return 0;
}

// ---------------- 动作 ----------------
static bool StartConsoleCmd(const std::wstring& cmdPath) {
    if (!FileExists(cmdPath)) {
        PushLog(L"找不到启动脚本：" + cmdPath);
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
    PushLog(L"启动失败（错误码 " + std::to_wstring(GetLastError()) + L"）");
    return false;
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
static void SetBusyText(const std::wstring& t) {
    g_busyText = t;
    if (g_hwnd) InvalidateRect(g_hwnd, &L.header, FALSE);
}
static void ActionStart() {
    PushLog(L"开始启动");
    bool anything = false;
    if (!PortOpen(6185)) {
        SetBusyText(L"正在启动大脑…");
        PushLog(L"1/2 启动大脑（AstrBot）");
        if (StartConsoleCmd(BRAIN_CMD)) {
            anything = true;
            bool up = false;
            for (int i = 0; i < 60; ++i) {
                if (PortOpen(6185)) {
                    up = true;
                    break;
                }
                SetBusyText(L"正在启动大脑… " + std::to_wstring(i * 3 / 2) + L" 秒");
                Sleep(1500);
            }
            PushLog(up ? L"  大脑起来了" : L"  大脑 90 秒没起来，看那个黑窗口的报错");
        }
    } else {
        PushLog(L"1/2 大脑已经在跑，跳过");
    }
    Probe p = ReadProbe();
    if (!p.online) {
        SetBusyText(L"正在启动登 QQ 的程序…");
        PushLog(L"2/2 启动 NapCat");
        if (StartConsoleCmd(NAPCAT_CMD)) {
            anything = true;
            for (int i = 0; i < 60; ++i) {
                Probe q = ReadProbe();
                if (q.online) {
                    p.online = true;
                    break;
                }
                if (NapCatLogQR()) break;
                SetBusyText(L"正在等小号上线… " + std::to_wstring(i) + L" 次");
                Sleep(1500);
            }
            if (p.online) PushLog(L"  小号上线了，可以聊天");
            else if (NapCatLogQR()) {
                PushLog(L"  QQ 要重新扫码，已打开扫码页");
                ShellExecuteW(nullptr, L"open", QR_PAGE, nullptr, nullptr, SW_SHOWNORMAL);
            } else PushLog(L"  还没连上，稍等看状态");
        }
    } else {
        PushLog(L"2/2 小号已经在线，跳过");
    }
    if (!anything) PushLog(L"没有需要启动的东西");
    PushLog(L"完成");
}
static void ActionStop() {
    PushLog(L"开始关闭（先 QQ，后大脑）");
    SetBusyText(L"正在关闭登 QQ 的程序…");
    PushLog(L"1/2 关 NapCat");
    RunHidden(L"taskkill /F /T /IM NapCatWinBootMain.exe >nul 2>&1");
    Sleep(2500);
    SetBusyText(L"正在关闭大脑…");
    PushLog(L"2/2 关大脑");
    RunHidden(L"taskkill /F /T /IM astrbot.exe >nul 2>&1");
    if (g_includeAll) {
        SetBusyText(L"正在关闭语音和记忆…");
        PushLog(L"顺带关语音和记忆");
        RunHidden(L"taskkill /F /IM breeze-server.exe >nul 2>&1");
        RunHidden(L"taskkill /F /IM ollama.exe >nul 2>&1");
    }
    Sleep(1200);
    PushLog(PortOpen(6185) ? L"大脑还在，再点一次「关闭她」" : L"已关闭");
    PushLog(L"完成");
}
static void ActionToggleAutostart(bool paused) {
    if (!paused) {
        SetBusyText(L"正在暂停开机自启…");
        CreateDirectoryW(PAUSE_DIR, nullptr);
        int moved = 0, found = 0;
        DWORD lastErr = 0;
        for (const wchar_t* n : STARTUP_ITEMS) {
            std::wstring src = Join(StartupDir(), n);
            if (!FileExists(src)) continue;
            ++found;
            std::wstring dst = Join(PAUSE_DIR, n);
            if (MoveFileExW(src.c_str(), dst.c_str(),
                            MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED)) {
                ++moved;
                PushLog(std::wstring(L"  移走 ") + n);
            } else {
                lastErr = GetLastError();
                PushLog(std::wstring(L"  移走失败 ") + n + L"（错误码 " + std::to_wstring(lastErr) + L"）");
            }
        }
        if (moved > 0) PushLog(L"已暂停开机自启（" + std::to_wstring(moved) + L" 项）");
        else if (found > 0) PushLog(L"找到 " + std::to_wstring(found) + L" 项但没移走（错误码 " + std::to_wstring(lastErr) + L"）");
        else PushLog(L"启动文件夹里没有她的自启项");
    } else {
        SetBusyText(L"正在恢复开机自启…");
        int moved = 0, found = 0;
        DWORD lastErr = 0;
        for (const wchar_t* n : STARTUP_ITEMS) {
            std::wstring src = Join(PAUSE_DIR, n);
            if (!FileExists(src)) continue;
            ++found;
            std::wstring dst = Join(StartupDir(), n);
            if (MoveFileExW(src.c_str(), dst.c_str(),
                            MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED)) {
                ++moved;
                PushLog(std::wstring(L"  放回 ") + n);
            } else {
                lastErr = GetLastError();
                PushLog(std::wstring(L"  放回失败 ") + n + L"（错误码 " + std::to_wstring(lastErr) + L"）");
            }
        }
        if (moved > 0) PushLog(L"已恢复开机自启（" + std::to_wstring(moved) + L" 项）");
        else if (found > 0) PushLog(L"找到 " + std::to_wstring(found) + L" 项但没放回（错误码 " + std::to_wstring(lastErr) + L"）");
        else PushLog(L"没有可恢复的项");
    }
}
static DWORD WINAPI ActionThread(LPVOID param) {
    int kind = (int)(INT_PTR)param;
    SysStatus st;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        st = g_st;
    }
    if (kind == ID_START) ActionStart();
    else if (kind == ID_STOP) ActionStop();
    else if (kind == ID_AUTO) ActionToggleAutostart(st.paused);
    g_busy = false;
    g_busyText.clear();
    if (g_hwnd) PostMessageW(g_hwnd, WM_APP_DONE, 0, 0);
    return 0;
}
static void RunAction(int kind) {
    if (g_busy) return;
    g_busy = true;
    g_busyStart = GetTickCount64();
    g_busyText = L"准备中…";
    InvalidateRect(g_hwnd, nullptr, FALSE);
    HANDLE h = CreateThread(nullptr, 0, ActionThread, (LPVOID)(INT_PTR)kind, 0, nullptr);
    if (h) CloseHandle(h);
}

// ---------------- 绘制辅助 ----------------
static float Ease(float t) {
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    float u = 1 - t;
    return 1 - u * u * u;
}
static ARGB LerpC(ARGB a, ARGB b, float t) {
    int aa = (a >> 24) & 0xFF, ar = (a >> 16) & 0xFF, ag = (a >> 8) & 0xFF, ab = a & 0xFF;
    int ba = (b >> 24) & 0xFF, br = (b >> 16) & 0xFF, bg = (b >> 8) & 0xFF, bb = b & 0xFF;
    return (ARGB)((int)(aa + (ba - aa) * t) << 24 | (int)(ar + (br - ar) * t) << 16 |
                  (int)(ag + (bg - ag) * t) << 8 | (int)(ab + (bb - ab) * t));
}
static void RoundRect(Graphics& g, RectF r, float rad, const Brush* fill, const Pen* border) {
    if (r.Width < 1 || r.Height < 1) return;
    float d = std::min(rad * 2, std::min(r.Width, r.Height));
    GraphicsPath p;
    p.AddArc(r.X, r.Y, d, d, 180, 90);
    p.AddArc(r.X + r.Width - d, r.Y, d, d, 270, 90);
    p.AddArc(r.X + r.Width - d, r.Y + r.Height - d, d, d, 0, 90);
    p.AddArc(r.X, r.Y + r.Height - d, d, d, 90, 90);
    p.CloseFigure();
    if (fill) g.FillPath(fill, &p);
    if (border) g.DrawPath(border, &p);
}
static void DrawTextEx(Graphics& g, const std::wstring& s, Font& f, RectF r, ARGB color,
                       StringAlignment hAlign = StringAlignmentNear) {
    if (s.empty()) return;
    SolidBrush br((Color(color)));
    StringFormat fmt;
    fmt.SetAlignment(hAlign);
    fmt.SetLineAlignment(StringAlignmentCenter);
    fmt.SetTrimming(StringTrimmingEllipsisCharacter);
    fmt.SetFormatFlags(StringFormatFlagsNoWrap);
    g.DrawString(s.c_str(), (INT)s.size(), &f, r, &fmt, &br);
}
// 卡片内的小标题：中文 + 英文副标（参考图排版）
static void Section(Graphics& g, RECT card, const wchar_t* zh, const wchar_t* en, Font& fZh,
                    Font& fEn) {
    DrawTextEx(g, zh, fZh,
               RectF((REAL)card.left + S(16), (REAL)card.top + S(10), S(160), S(22)), C_TEXT);
    float zw = S(160);
    DrawTextEx(g, en, fEn,
               RectF((REAL)card.left + S(16) + zw, (REAL)card.top + S(10) + S(2), S(150), S(20)),
               C_SUB);
}

struct Hit { int id; RECT r; };
static std::vector<Hit> g_hits;
static Hit* FindHit(int x, int y) {
    POINT p{x, y};
    for (auto& h : g_hits)
        if (PtInRect(&h.r, p)) return &h;
    return nullptr;
}

static void LayoutAll(int w, int h) {
    int pad = (int)S(18);
    int y = (int)S(14);
    L.header = {pad, y, w - pad, y + (int)S(52)};
    y = L.header.bottom + (int)S(10);

    L.card = {pad, y, w - pad, y + (int)S(216)};
    y = L.card.bottom + (int)S(14);

    int gap = (int)S(10);
    int bw = (w - pad * 2 - gap * 2) / 3;
    L.btnStart = {pad, y, pad + bw, y + (int)S(50)};
    L.btnStop = {L.btnStart.right + gap, y, L.btnStart.right + gap + bw, y + (int)S(50)};
    L.btnAuto = {L.btnStop.right + gap, y, w - pad, y + (int)S(50)};
    y = L.btnStart.bottom + (int)S(10);

    L.btnRefresh = {pad, y, pad + (int)S(112), y + (int)S(34)};
    L.check = {L.btnRefresh.right + (int)S(12), y, w - pad, y + (int)S(34)};
    y = L.btnRefresh.bottom + (int)S(16);

    L.card2 = {pad, y, w - pad, y + (int)S(104)};
    int lw = (L.card2.right - L.card2.left - (int)S(16) * 2 - (int)S(10) * 2) / 3;
    int by = L.card2.top + (int)S(38);
    L.btnWb = {L.card2.left + (int)S(16), by, L.card2.left + (int)S(16) + lw, by + (int)S(46)};
    L.btnQr = {L.btnWb.right + (int)S(10), by, L.btnWb.right + (int)S(10) + lw, by + (int)S(46)};
    L.btnLog = {L.btnQr.right + (int)S(10), by, L.btnQr.right + (int)S(10) + lw, by + (int)S(46)};
    y = L.card2.bottom + (int)S(14);

    L.footer = {pad, h - pad - (int)S(16), w - pad, h - pad};
    L.logBox = {pad, y, w - pad, L.footer.top - (int)S(10)};
    if (L.logBox.bottom < L.logBox.top + (int)S(90)) L.logBox.bottom = L.logBox.top + (int)S(90);

    L.pill = {w - pad - (int)S(104), L.header.top + (int)S(4), w - pad, L.header.top + (int)S(30)};

    g_hits.clear();
    g_hits.push_back({ID_START, L.btnStart});
    g_hits.push_back({ID_STOP, L.btnStop});
    g_hits.push_back({ID_AUTO, L.btnAuto});
    g_hits.push_back({ID_REFRESH, L.btnRefresh});
    g_hits.push_back({ID_CHECK, L.check});
    g_hits.push_back({ID_WORKBENCH, L.btnWb});
    g_hits.push_back({ID_QR, L.btnQr});
    g_hits.push_back({ID_LOGDIR, L.btnLog});
}

static int SelfTest() {
    CreateDirectoryW(L"D:\\dafeiyu\\docs", nullptr);
    SysStatus st;
    st.brain = PortOpen(6185);
    st.voice = PortOpen(8137);
    st.memory = PortOpen(11434);
    Probe p = ReadProbe();
    std::wstring out = std::wstring(L"大肥鱼开关 v") + APP_VERSION + L" 自检\r\n";
    out += L"大脑(6185)=" + std::to_wstring(st.brain ? 1 : 0) + L"\r\n";
    out += L"state.up=" + std::to_wstring(p.up ? 1 : 0) + L"\r\n";
    out += L"online=" + std::to_wstring(p.online ? 1 : 0) + L"\r\n";
    out += L"语音(8137)=" + std::to_wstring(st.voice ? 1 : 0) + L"\r\n";
    out += L"记忆(11434)=" + std::to_wstring(st.memory ? 1 : 0) + L"\r\n";
    out += L"启动项:";
    for (const wchar_t* n : STARTUP_ITEMS) {
        out += L"\r\n  ";
        out += n;
        out += FileExists(Join(StartupDir(), n)) ? L" = 在启动文件夹" : L" = 不在（或已暂停）";
    }
    HANDLE h = CreateFileW(SELFTEST_LOG, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h != INVALID_HANDLE_VALUE) {
        std::string utf8;
        int need = WideCharToMultiByte(CP_UTF8, 0, out.c_str(), -1, nullptr, 0, nullptr, nullptr);
        utf8.resize(need > 0 ? need : 0);
        if (need > 0) WideCharToMultiByte(CP_UTF8, 0, out.c_str(), -1, utf8.data(), need, nullptr, nullptr);
        DWORD w = 0;
        WriteFile(h, utf8.c_str(), (DWORD)(utf8.size() ? utf8.size() - 1 : 0), &w, nullptr);
        CloseHandle(h);
    }
    return 0;
}

// ---------------- 按钮 ----------------
static void DrawButton(Graphics& g, RECT r, const std::wstring& label, int id, Font& f, ARGB fill,
                       ARGB fill2, ARGB textColor, ARGB borderColor, float radius) {
    BtnAnim& a = g_anim[id];
    float scale = 1.0f - 0.03f * a.press;
    float cx = (r.left + r.right) / 2.0f, cy = (r.top + r.bottom) / 2.0f;
    float w = (r.right - r.left) * scale, h = (r.bottom - r.top) * scale;
    RectF rr(cx - w / 2, cy - h / 2, w, h);
    float rad = S(radius);

    ARGB f1 = fill, f2 = fill2;
    if (a.hover > 0.01f) {
        f1 = LerpC(f1, 0xFFFFFFFF, 0.12f * a.hover);
        if (fill2) f2 = LerpC(f2, 0xFFFFFFFF, 0.12f * a.hover);
    }
    if (a.press > 0.01f) {
        f1 = LerpC(f1, 0xFF000000, 0.06f * a.press);
        if (fill2) f2 = LerpC(f2, 0xFF000000, 0.06f * a.press);
    }
    if (fill2) {
        LinearGradientBrush lg(PointF(rr.X, rr.Y), PointF(rr.X, rr.Y + rr.Height),
                               Color(f1), Color(f2));
        RoundRect(g, rr, rad, &lg, nullptr);
    } else {
        SolidBrush b((Color(f1)));
        if (borderColor) {
            Pen pen(Color(borderColor), S(1));
            RoundRect(g, rr, rad, &b, &pen);
        } else {
            RoundRect(g, rr, rad, &b, nullptr);
        }
    }
    DrawTextEx(g, label, f, RectF(rr.X, rr.Y, rr.Width, rr.Height), textColor,
               StringAlignmentCenter);

    if (a.rippleA > 0.01f && a.rippleR > 0.1f) {
        GraphicsPath clip;
        float d = std::min(rad * 2, std::min(rr.Width, rr.Height));
        clip.AddArc(rr.X, rr.Y, d, d, 180, 90);
        clip.AddArc(rr.X + rr.Width - d, rr.Y, d, d, 270, 90);
        clip.AddArc(rr.X + rr.Width - d, rr.Y + rr.Height - d, d, d, 0, 90);
        clip.AddArc(rr.X, rr.Y + rr.Height - d, d, d, 90, 90);
        clip.CloseFigure();
        g.SetClip(&clip);
        SolidBrush rb(Color((ARGB)((int)(46 * a.rippleA) << 24 | 0xFFFFFF)));
        g.FillEllipse(&rb, a.rippleX - a.rippleR, a.rippleY - a.rippleR, a.rippleR * 2,
                      a.rippleR * 2);
        g.ResetClip();
    }
}

// ---------------- 窗口 ----------------
static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        g_hwnd = hwnd;
        RECT rc;
        GetClientRect(hwnd, &rc);
        LayoutAll(rc.right, rc.bottom);
        SetTimer(hwnd, 1, 16, nullptr);
        return 0;
    }
    case WM_SIZE:
        LayoutAll(LOWORD(lp), HIWORD(lp));
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_GETMINMAXINFO: {
        MINMAXINFO* mm = (MINMAXINFO*)lp;
        mm->ptMinTrackSize.x = (int)S(520);
        mm->ptMinTrackSize.y = (int)S(720);
        return 0;
    }
    case WM_TIMER: {
        bool need = g_fadeIn < 1.0f || g_busy;
        if (g_logNewFade > 0) {
            g_logNewFade -= 16 / 600.0f;
            if (g_logNewFade < 0) g_logNewFade = 0;
            need = true;
        }
        float dy = g_logScrollTarget - g_logScrollF;
        if (std::fabs(dy) > 0.05f) {
            g_logScrollF += dy * 0.28f;
            need = true;
        }
        SysStatus st;
        {
            std::lock_guard<std::mutex> lk(g_mu);
            st = g_st;
        }
        bool tgt[4] = {st.brain, st.online, st.voice, st.memory};
        for (int i = 0; i < 4; ++i) {
            float t = tgt[i] ? 1.0f : 0.0f;
            if (std::fabs(g_dotT[i] - t) > 0.01f) {
                g_dotT[i] += (t - g_dotT[i]) * 0.22f;
                need = true;
            } else g_dotT[i] = t;
        }
        for (int i = 0; i < ID_COUNT; ++i) {
            BtnAnim& a = g_anim[i];
            float th = (g_hover == i) ? 1.0f : 0.0f;
            float tp = (g_press == i) ? 1.0f : 0.0f;
            if (std::fabs(a.hover - th) > 0.01f) {
                a.hover += (th - a.hover) * 0.28f;
                need = true;
            } else a.hover = th;
            if (std::fabs(a.press - tp) > 0.01f) {
                a.press += (tp - a.press) * 0.35f;
                need = true;
            } else a.press = tp;
            if (a.rippleA > 0) {
                a.rippleR += 520.0f * 0.016f * S(1);
                a.rippleA -= 0.016f * 1.6f;
                if (a.rippleA < 0) a.rippleA = 0;
                need = true;
            }
        }
        if (g_busy) {
            g_shimmer += 0.016f * 0.85f;
            if (g_shimmer > 1) g_shimmer -= 1;
            need = true;
        }
        if (g_fadeIn < 1.0f) {
            g_fadeIn += 0.016f * 2.8f;
            if (g_fadeIn > 1) g_fadeIn = 1;
            need = true;
        }
        if (need) InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    case WM_APP_STATE:
    case WM_APP_DONE:
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_APP_LOGLINE:
        InvalidateRect(hwnd, &L.logBox, FALSE);
        return 0;
    case WM_MOUSEMOVE: {
        int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
        Hit* h = FindHit(x, y);
        int hv = h ? h->id : -1;
        if (hv != g_hover) {
            g_hover = hv;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd, 0};
        TrackMouseEvent(&tme);
        return 0;
    }
    case WM_MOUSELEAVE:
        g_hover = -1;
        g_press = -1;
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT) {
            POINT pt;
            GetCursorPos(&pt);
            ScreenToClient(hwnd, &pt);
            SetCursor(LoadCursor(nullptr, FindHit(pt.x, pt.y) ? IDC_HAND : IDC_ARROW));
            return TRUE;
        }
        break;
    case WM_LBUTTONDOWN: {
        if (g_busy) return 0;
        int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
        Hit* h = FindHit(x, y);
        if (h) {
            g_press = h->id;
            BtnAnim& a = g_anim[h->id];
            a.rippleX = (float)x;
            a.rippleY = (float)y;
            a.rippleR = 1;
            a.rippleA = 1.0f;
            InvalidateRect(hwnd, nullptr, FALSE);
            SetCapture(hwnd);
        }
        return 0;
    }
    case WM_LBUTTONUP: {
        int was = g_press;
        g_press = -1;
        ReleaseCapture();
        int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
        Hit* h = FindHit(x, y);
        if (h && h->id == was) {
            switch (was) {
            case ID_START: RunAction(ID_START); break;
            case ID_STOP: RunAction(ID_STOP); break;
            case ID_AUTO: RunAction(ID_AUTO); break;
            case ID_REFRESH: InvalidateRect(hwnd, nullptr, FALSE); break;
            case ID_CHECK: g_includeAll = !g_includeAll; break;
            case ID_WORKBENCH:
                ShellExecuteW(nullptr, L"open", WORKBENCH_HTML, nullptr, nullptr, SW_SHOWNORMAL);
                break;
            case ID_QR:
                ShellExecuteW(nullptr, L"open", QR_PAGE, nullptr, nullptr, SW_SHOWNORMAL);
                break;
            case ID_LOGDIR:
                ShellExecuteW(nullptr, L"open", LOG_DIR, nullptr, nullptr, SW_SHOWNORMAL);
                break;
            }
        }
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    case WM_MOUSEWHEEL: {
        POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        ScreenToClient(hwnd, &pt);
        if (PtInRect(&L.logBox, pt)) {
            int delta = GET_WHEEL_DELTA_WPARAM(wp);
            std::lock_guard<std::mutex> lk(g_logMu);
            int maxScroll = std::max(0, (int)g_log.size() - 1);
            if (delta > 0) g_logScrollTarget = std::min(maxScroll, g_logScrollTarget + 3);
            else g_logScrollTarget = std::max(0, g_logScrollTarget - 3);
        }
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);
        int w = rc.right, h = rc.bottom;
        if (L.logBox.right == 0) LayoutAll(w, h);

        HDC memdc = CreateCompatibleDC(hdc);
        HBITMAP bmp = CreateCompatibleBitmap(hdc, w, h);
        HGDIOBJ old = SelectObject(memdc, bmp);
        {
            Graphics g(memdc);
            g.SetSmoothingMode(SmoothingModeAntiAlias);
            g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);

            SolidBrush bg((Color(C_BG)));
            g.FillRectangle(&bg, 0, 0, w, h);

            Font fTitle(L"Microsoft YaHei UI", S(21), FontStyleBold, UnitPixel);
            Font fSub(L"Microsoft YaHei UI", S(11.5f), FontStyleRegular, UnitPixel);
            Font fSec(L"Microsoft YaHei UI", S(12.5f), FontStyleBold, UnitPixel);
            Font fSecEn(L"Segoe UI", S(9), FontStyleRegular, UnitPixel);
            Font fRow(L"Microsoft YaHei UI", S(12.5f), FontStyleRegular, UnitPixel);
            Font fRowB(L"Microsoft YaHei UI", S(12.5f), FontStyleBold, UnitPixel);
            Font fBtn(L"Microsoft YaHei UI", S(13.5f), FontStyleBold, UnitPixel);
            Font fSmall(L"Microsoft YaHei UI", S(10.5f), FontStyleRegular, UnitPixel);
            Font fTiny(L"Microsoft YaHei UI", S(9.5f), FontStyleRegular, UnitPixel);
            Font fLog(L"Consolas", S(10.5f), FontStyleRegular, UnitPixel);

            SysStatus st;
            {
                std::lock_guard<std::mutex> lk(g_mu);
                st = g_st;
            }
            g.SetClip(RectF(0, 0, (REAL)w, (REAL)h));

            // ---- 标题 ----
            RectF hr((REAL)L.header.left, (REAL)L.header.top, (REAL)(L.header.right - L.header.left),
                     (REAL)(L.header.bottom - L.header.top));
            DrawTextEx(g, L"大肥鱼开关", fTitle, RectF(hr.X, hr.Y + S(1), hr.Width, S(30)), C_TEXT);
            std::wstring sub = L"一键开关本机的 AstrBot + NapCat";
            if (g_busy) {
                ULONGLONG el = (GetTickCount64() - g_busyStart) / 1000;
                sub = g_busyText + L"（" + std::to_wstring(el) + L" 秒）";
            }
            DrawTextEx(g, sub, fSub, RectF(hr.X, hr.Y + S(33), hr.Width - S(120), S(20)),
                       g_busy ? C_ACCENT : C_MUTED);

            // 在线摘要
            {
                bool on = st.online;
                ARGB pc = on ? 0xFFEAF7F0 : 0xFFFBEDEF;
                ARGB ptc = on ? 0xFF217A52 : 0xFFB8546A;
                SolidBrush pb((Color(pc)));
                RoundRect(g, RectF((REAL)L.pill.left, (REAL)L.pill.top,
                                   (REAL)(L.pill.right - L.pill.left),
                                   (REAL)(L.pill.bottom - L.pill.top)),
                          S(12), &pb, nullptr);
                SolidBrush db((Color(on ? C_OK : C_BAD)));
                g.FillEllipse(&db, (REAL)L.pill.left + S(11), (REAL)L.pill.top + S(8), S(8), S(8));
                DrawTextEx(g, on ? L"她在线" : L"她离线", fTiny,
                           RectF((REAL)L.pill.left + S(25), (REAL)L.pill.top,
                                 (REAL)(L.pill.right - L.pill.left) - S(30),
                                 (REAL)(L.pill.bottom - L.pill.top)),
                           ptc);
            }

            // ---- 状态卡 ----
            {
                SolidBrush card((Color(C_CARD)));
                Pen pen(Color(C_BORDER), S(1));
                RoundRect(g, RectF((REAL)L.card.left, (REAL)L.card.top,
                                   (REAL)(L.card.right - L.card.left),
                                   (REAL)(L.card.bottom - L.card.top)),
                          S(14), &card, &pen);
            }
            Section(g, L.card, L"运行状态", L"STATUS", fSec, fSecEn);
            {
                struct Row { const wchar_t* label; std::wstring value; };
                std::vector<Row> rows;
                rows.push_back({L"大脑", st.brain ? L"开着" : L"没开"});
                rows.push_back({L"小号", st.online ? L"在线（能聊天）"
                                                   : (st.detail.empty() ? L"没连上" : st.detail)});
                rows.push_back({L"语音", st.voice ? L"开着" : L"没开（只影响发语音）"});
                rows.push_back({L"记忆", st.memory ? L"开着" : L"没开"});
                for (int i = 0; i < 4; ++i) {
                    float ry = (float)L.card.top + S(42) + i * S(30);
                    ARGB dc = LerpC(C_BAD, C_OK, g_dotT[i]);
                    SolidBrush glow((Color((ARGB)((int)(44 * g_dotT[i]) << 24 | (dc & 0x00FFFFFF)))));
                    g.FillEllipse(&glow, (REAL)L.card.left + S(14), ry + S(7), S(16), S(16));
                    SolidBrush db((Color(dc)));
                    g.FillEllipse(&db, (REAL)L.card.left + S(18), ry + S(11), S(9), S(9));
                    DrawTextEx(g, rows[i].label, fRow,
                               RectF((REAL)L.card.left + S(38), ry, S(54), S(28)), C_TEXT);
                    DrawTextEx(g, rows[i].value, fRowB,
                               RectF((REAL)L.card.left + S(94), ry,
                                     (REAL)(L.card.right - L.card.left) - S(112), S(28)),
                               g_dotT[i] > 0.5f ? C_TEXT : C_MUTED);
                }
                float sy = (float)L.card.top + S(42) + 4 * S(30) + S(2);
                Pen div(Color(0xFFEDF0F7), S(1));
                g.DrawLine(&div, (REAL)L.card.left + S(16), sy, (REAL)L.card.right - S(16), sy);

                std::wstring stat;
                if (st.started > 0) {
                    long long secs = (long long)time(nullptr) - st.started;
                    if (secs < 0) secs = 0;
                    long long hh = secs / 3600, mm = (secs % 3600) / 60;
                    stat += hh > 0 ? (L"已运行 " + std::to_wstring(hh) + L" 小时 " +
                                      std::to_wstring(mm) + L" 分")
                                   : (L"已运行 " + std::to_wstring(std::max(1LL, mm)) + L" 分");
                } else {
                    stat += L"未在运行";
                }
                stat += st.paused ? L"　·　开机自启：已暂停" : L"　·　开机自启：已开启";
                DrawTextEx(g, stat, fTiny,
                           RectF((REAL)L.card.left + S(18), sy + S(8),
                                 (REAL)(L.card.right - L.card.left) - S(36), S(24)),
                           C_MUTED);
            }

            // ---- 三个大按钮（柔和新配色）----
            {
                DrawButton(g, L.btnStart, L"启动她", ID_START, fBtn, C_ACCENT, C_ACCENT2, C_WHITE, 0, 12);
                DrawButton(g, L.btnStop, L"关闭她", ID_STOP, fBtn, 0xFFFFFFFF, 0, C_DANGER,
                           C_DANGER_BORDER, 12);
                DrawButton(g, L.btnAuto, st.paused ? L"恢复自启" : L"暂停自启", ID_AUTO, fBtn,
                           C_SOFT, 0, C_TEXT, 0, 12);
                if (g_busy) {
                    SolidBrush veil((Color((ARGB)0xB3FFFFFF)));
                    RECT vs[3] = {L.btnStart, L.btnStop, L.btnAuto};
                    for (auto& r : vs)
                        RoundRect(g, RectF((REAL)r.left, (REAL)r.top,
                                           (REAL)(r.right - r.left), (REAL)(r.bottom - r.top)),
                                  S(12), &veil, nullptr);
                }
            }

            // ---- 刷新 + 复选 ----
            {
                DrawButton(g, L.btnRefresh, L"刷新状态", ID_REFRESH, fSmall, C_WHITE, 0, C_MUTED,
                           C_BORDER, 10);
                float cbx = (float)L.check.left, cby = (float)L.check.top + S(9);
                BtnAnim& ca = g_anim[ID_CHECK];
                ARGB cbf = g_includeAll ? C_ACCENT : C_WHITE;
                if (ca.hover > 0.01f) cbf = LerpC(cbf, 0xFFDCE4F7, 0.9f * ca.hover);
                SolidBrush cbF((Color(cbf)));
                Pen cbPen(Color(g_includeAll ? C_ACCENT
                                             : (ca.hover > 0.2f ? 0xFFA9B4C8 : 0xFFC3CAD8)),
                           S(1.4f));
                RoundRect(g, RectF(cbx, cby, S(17), S(17)), S(5), &cbF, &cbPen);
                if (g_includeAll) {
                    Pen tick(Color(C_WHITE), S(2.2f));
                    g.DrawLine(&tick, cbx + S(3.8f), cby + S(8.6f), cbx + S(7.4f), cby + S(12.2f));
                    g.DrawLine(&tick, cbx + S(7.4f), cby + S(12.2f), cbx + S(13.2f), cby + S(4.8f));
                }
                DrawTextEx(g, L"关闭时连语音和记忆一起关（省内存/显卡）", fSmall,
                           RectF(cbx + S(24), (REAL)L.check.top + S(4),
                                 (REAL)(L.check.right - L.check.left) - S(24), S(26)),
                           C_MUTED);
            }

            // ---- 快捷入口卡 ----
            {
                SolidBrush card((Color(0xFFFCFCFE)));
                Pen pen(Color(C_BORDER), S(1));
                RoundRect(g, RectF((REAL)L.card2.left, (REAL)L.card2.top,
                                   (REAL)(L.card2.right - L.card2.left),
                                   (REAL)(L.card2.bottom - L.card2.top)),
                          S(14), &card, &pen);
                Section(g, L.card2, L"快捷入口", L"SHORTCUTS", fSec, fSecEn);
                DrawButton(g, L.btnWb, L"打开工作台", ID_WORKBENCH, fSmall, 0xFFFFFFFF, 0, C_TEXT,
                           C_BORDER, 10);
                DrawButton(g, L.btnQr, L"扫码页", ID_QR, fSmall, 0xFFFFFFFF, 0, C_TEXT, C_BORDER,
                           10);
                DrawButton(g, L.btnLog, L"日志文件夹", ID_LOGDIR, fSmall, 0xFFFFFFFF, 0, C_TEXT,
                           C_BORDER, 10);
            }

            // ---- 日志卡 ----
            {
                SolidBrush lb((Color(C_LOG_BG)));
                Pen border(Color(C_BORDER), S(1));
                RoundRect(g, RectF((REAL)L.logBox.left, (REAL)L.logBox.top,
                                   (REAL)(L.logBox.right - L.logBox.left),
                                   (REAL)(L.logBox.bottom - L.logBox.top)),
                          S(14), &lb, &border);
                Section(g, L.logBox, L"操作记录", L"LOG", fSec, fSecEn);

                int lh = (int)S(19);
                int top = L.logBox.top + (int)S(38);
                int innerH = (L.logBox.bottom - top) - (int)S(10);
                int maxLines = std::max(1, innerH / lh);
                std::lock_guard<std::mutex> lk(g_logMu);
                int total = (int)g_log.size();
                if (total == 0) {
                    std::vector<std::wstring> tips = {
                        L"点「启动她」：先起大脑，再起登 QQ 的程序",
                        L"她掉线了先看上面「小号」那行，红了就点「启动她」",
                        L"想让她开机不自启，点「暂停自启」",
                        L"「关闭她」不会丢记忆和聊天记录",
                    };
                    float ty = (float)top + S(2);
                    for (auto& t : tips) {
                        SolidBrush dotB((Color(C_ACCENT)));
                        g.FillEllipse(&dotB, (REAL)L.logBox.left + S(18), ty + S(8), S(5), S(5));
                        DrawTextEx(g, t, fSmall,
                                   RectF((REAL)L.logBox.left + S(30), ty,
                                         (REAL)(L.logBox.right - L.logBox.left) - S(46), S(20)),
                                   C_MUTED);
                        ty += S(26);
                    }
                } else {
                    int start = total - maxLines - (int)std::lround(g_logScrollF);
                    int drawn = 0;
                    for (int i = std::max(0, start); i < total && drawn < maxLines; ++i, ++drawn) {
                        float ly = (float)top + drawn * lh;
                        std::wstring line = g_log[i];
                        bool dim = line.rfind(L"  ", 0) == 0;
                        ARGB c = dim ? C_MUTED : C_TEXT;
                        if (i == total - 1 && g_logNewFade > 0)
                            c = LerpC(C_ACCENT, c, 1.0f - g_logNewFade);
                        DrawTextEx(g, line, fLog,
                                   RectF((REAL)L.logBox.left + S(16), ly,
                                         (REAL)(L.logBox.right - L.logBox.left) - S(32), (REAL)lh),
                                   c);
                    }
                }
            }

            // 忙碌流光
            if (g_busy) {
                float pw = S(110);
                float pos = g_shimmer * (w + pw) - pw;
                SolidBrush sb((Color(C_ACCENT)));
                RoundRect(g, RectF(pos, (REAL)L.header.top - S(6), pw, S(3)), S(1.5f), &sb, nullptr);
            }

            DrawTextEx(g, L"关闭顺序：先 QQ 后大脑，登录态不易失效　·　关掉不会丢记忆和聊天记录", fTiny,
                       RectF((REAL)L.footer.left, (REAL)L.footer.top,
                             (REAL)(L.footer.right - L.footer.left),
                             (REAL)(L.footer.bottom - L.footer.top)),
                       C_SUB);
        }
        BitBlt(hdc, 0, 0, w, h, memdc, 0, 0, SRCCOPY);
        SelectObject(memdc, old);
        DeleteObject(bmp);
        DeleteDC(memdc);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_DESTROY:
        KillTimer(hwnd, 1);
        g_stopProbe = true;
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE, LPSTR, int) {
    Trace(L"启动 v2.2.1");
    SetUnhandledExceptionFilter(CrashFilter);

    typedef BOOL(WINAPI * SetCtxFn)(DPI_AWARENESS_CONTEXT);
    if (HMODULE u = GetModuleHandleW(L"user32.dll")) {
        if (auto fn = (SetCtxFn)GetProcAddress(u, "SetProcessDpiAwarenessContext"))
            fn(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    }

    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv) {
        for (int i = 1; i < argc; ++i) {
            if (wcscmp(argv[i], L"--selftest") == 0) {
                LocalFree(argv);
                WSADATA wsa{};
                WSAStartup(MAKEWORD(2, 2), &wsa);
                int r = SelfTest();
                WSACleanup();
                return r;
            }
        }
        LocalFree(argv);
    }

    HANDLE mtx = CreateMutexW(nullptr, TRUE, L"Local\\DafeiyuSwitch_v22");
    if (mtx && GetLastError() == ERROR_ALREADY_EXISTS) {
        if (HWND old = FindWindowW(L"DafeiyuSwitchV2", nullptr)) {
            ShowWindow(old, SW_RESTORE);
            SetForegroundWindow(old);
        }
        return 0;
    }

    WSADATA wsa{};
    WSAStartup(MAKEWORD(2, 2), &wsa);
    GdiplusStartupInput gsi;
    ULONG_PTR gdipTok = 0;
    GdiplusStartup(&gdipTok, &gsi, nullptr);
    Trace(L"GDI+ 初始化完成");

    {
        HDC hdc = GetDC(nullptr);
        g_dpi = GetDeviceCaps(hdc, LOGPIXELSX);
        ReleaseDC(nullptr, hdc);
        if (g_dpi <= 0) g_dpi = 96;
    }

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(1), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE);
    wc.hIconSm = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(1), IMAGE_ICON, 16, 16, 0);
    wc.lpszClassName = L"DafeiyuSwitchV2";
    RegisterClassExW(&wc);

    RECT r{0, 0, (LONG)S(WIN_W), (LONG)S(WIN_H)};
    AdjustWindowRectEx(&r, WS_OVERLAPPEDWINDOW, FALSE, 0);
    HWND hwnd = CreateWindowExW(0, L"DafeiyuSwitchV2", L"大肥鱼开关", WS_OVERLAPPEDWINDOW,
                                CW_USEDEFAULT, CW_USEDEFAULT, r.right - r.left, r.bottom - r.top,
                                nullptr, nullptr, inst, nullptr);
    if (!hwnd) return 1;
    Trace(L"窗口创建成功");

    if (HMODULE dwm = LoadLibraryW(L"dwmapi.dll")) {
        typedef HRESULT(WINAPI * DwmSetAttr)(HWND, DWORD, LPCVOID, DWORD);
        if (auto fn = (DwmSetAttr)GetProcAddress(dwm, "DwmSetWindowAttribute")) {
            DWORD pref = 2;
            fn(hwnd, 33, &pref, sizeof(pref));
        }
    }

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);
    Trace(L"进入消息循环");
    CreateThread(nullptr, 0, ProbeThread, nullptr, 0, nullptr);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    g_stopProbe = true;
    GdiplusShutdown(gdipTok);
    WSACleanup();
    if (mtx) CloseHandle(mtx);
    return 0;
}
