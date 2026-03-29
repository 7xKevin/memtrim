#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>
#include <dxgi1_4.h>
#include <objidl.h>
#include <gdiplus.h>
#include <psapi.h>
#include <shellapi.h>
#include <urlmon.h>

#include <algorithm>
#include <cwctype>
#include <cwchar>
#include <string>
#include <utility>
#include <vector>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "urlmon.lib")

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif

#ifndef PROCESS_QUERY_LIMITED_INFORMATION
#define PROCESS_QUERY_LIMITED_INFORMATION 0x1000
#endif

#ifndef NT_SUCCESS
#define NT_SUCCESS(Status) (((LONG)(Status)) >= 0)
#endif

namespace {

constexpr wchar_t kWindowClass[] = L"MemTrimLiteWindow";
constexpr wchar_t kWindowTitle[] = L"MemTrim Lite";
constexpr wchar_t kAppVersion[] = L"1.1.0";
constexpr wchar_t kUpdateManifestUrl[] = L"https://raw.githubusercontent.com/7xKevin/memtrim/main/website/update.json";
constexpr wchar_t kFallbackInstallerUrl[] = L"https://raw.githubusercontent.com/7xKevin/memtrim/main/website/downloads/MemTrimLite-Setup.exe";
constexpr int kBaseClientWidth = 392;
constexpr int kBaseClientHeight = 432;
constexpr UINT_PTR kRefreshTimerId = 1;
constexpr UINT kRefreshIntervalMs = 1000;
constexpr ULONG kSystemMemoryListInformation = 80;
constexpr DWORD kPaintFontQuality = ANTIALIASED_QUALITY;
constexpr UINT kTrayIconId = 1;
constexpr UINT kTrayMessage = WM_APP + 1;
constexpr UINT kTrayMenuOpen = 1001;
constexpr UINT kTrayMenuExit = 1002;
constexpr UINT kTrayMenuCheckUpdates = 1003;
constexpr UINT kTrayMenuInstallUpdate = 1004;
constexpr UINT kUpdateCheckCompletedMessage = WM_APP + 2;

enum DwmWindowCornerPreference {
    kCornerDefault = 0,
    kCornerDoNotRound = 1,
    kCornerRound = 2,
    kCornerRoundSmall = 3,
};

enum MemoryListCommand : ULONG {
    kMemoryCaptureAccessedBits = 0,
    kMemoryCaptureAndResetAccessedBits = 1,
    kMemoryEmptyWorkingSets = 2,
    kMemoryFlushModifiedList = 3,
    kMemoryPurgeStandbyList = 4,
    kMemoryPurgeLowPriorityStandbyList = 5,
};

using NtSetSystemInformationFn = LONG(NTAPI*)(ULONG, PVOID, ULONG);

Gdiplus::Color ToGdiPlusColor(COLORREF color) {
    return Gdiplus::Color(255, GetRValue(color), GetGValue(color), GetBValue(color));
}

struct AppState;

COLORREF GetUsageAccent(double usagePercent);
std::wstring BuildIdleStatus(const AppState* app);

struct MemorySection {
    double usagePercent = 0.0;
    double available = 0.0;
    double total = 0.0;
    const wchar_t* unit = L"GB";
    int decimals = 2;
    COLORREF accent = RGB(76, 134, 255);
};

struct CleanSummary {
    int trimmedProcesses = 0;
    bool emptiedSystemWorkingSets = false;
    bool purgedStandby = false;
    bool purgedLowPriorityStandby = false;
    DWORD durationMs = 0;
};

struct GpuInfo {
    bool available = false;
    std::wstring name = L"No compatible GPU detected";
    MemorySection dedicated;
    MemorySection shared;
};

struct UpdateState {
    bool available = false;
    bool checking = false;
    bool installInProgress = false;
    bool promptShown = false;
    std::wstring latestVersion;
    std::wstring installerUrl;
};

struct UpdateCheckResult {
    bool success = false;
    bool updateAvailable = false;
    bool userInitiated = false;
    std::wstring latestVersion;
    std::wstring installerUrl;
};

struct ThemePalette {
    COLORREF windowBg;
    COLORREF panelBg;
    COLORREF panelBorder;
    COLORREF titleText;
    COLORREF bodyText;
    COLORREF mutedText;
    COLORREF trackBg;
    COLORREF trackBorder;
    COLORREF toolbarIdle;
    COLORREF toolbarIdleHover;
    COLORREF toolbarIdleDown;
    COLORREF toolbarActive;
    COLORREF toolbarActiveHover;
    COLORREF toolbarActiveDown;
    COLORREF primaryButton;
    COLORREF primaryButtonHover;
    COLORREF primaryButtonDown;
    COLORREF primaryButtonDisabled;
    COLORREF primaryHint;
    COLORREF primaryHintDisabled;
    COLORREF statusDot;
    COLORREF badgeTextBase;
};

struct AppState {
    HWND hwnd = nullptr;
    UINT dpi = 96;
    HCURSOR cursorArrow = nullptr;
    HCURSOR cursorWait = nullptr;
    HFONT fontSmall = nullptr;
    HFONT font = nullptr;
    HFONT fontSection = nullptr;
    HFONT fontBold = nullptr;
    HFONT fontValue = nullptr;
    HICON iconLarge = nullptr;
    HICON iconSmall = nullptr;
    HICON trayUsageIcon = nullptr;
    IDXGIAdapter3* gpuAdapter = nullptr;
    NOTIFYICONDATAW trayIcon{};
    RECT topBarRect{};
    RECT gpuToggleRect{};
    RECT themeToggleRect{};
    RECT sections[3]{};
    RECT statusRect{};
    RECT buttonRect{};
    bool buttonHot = false;
    bool buttonDown = false;
    bool gpuButtonHot = false;
    bool gpuButtonDown = false;
    bool themeButtonHot = false;
    bool themeButtonDown = false;
    bool trackingMouse = false;
    bool isCleaning = false;
    bool showGpu = false;
    bool darkTheme = true;
    bool isElevated = false;
    bool trayIconAdded = false;
    bool exitRequested = false;
    MemorySection physical;
    MemorySection pagefile;
    MemorySection systemWorkingSet;
    GpuInfo gpu;
    UpdateState update;
    std::wstring statusLine;
};

int ScaleValue(int value, UINT dpi) {
    return MulDiv(value, static_cast<int>(dpi), 96);
}

int ScaleValue(const AppState* app, int value) {
    return ScaleValue(value, app->dpi);
}

COLORREF RgbHex(BYTE r, BYTE g, BYTE b) {
    return RGB(r, g, b);
}

COLORREF BlendColor(COLORREF base, COLORREF tint, int tintPercent) {
    const int basePercent = 100 - tintPercent;
    return RGB(
        (GetRValue(base) * basePercent + GetRValue(tint) * tintPercent) / 100,
        (GetGValue(base) * basePercent + GetGValue(tint) * tintPercent) / 100,
        (GetBValue(base) * basePercent + GetBValue(tint) * tintPercent) / 100);
}

bool IsProcessElevated() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        return false;
    }

    TOKEN_ELEVATION elevation{};
    DWORD size = 0;
    const BOOL ok = GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &size);
    CloseHandle(token);
    return ok && elevation.TokenIsElevated != 0;
}

bool HasLaunchFlag(const wchar_t* flag) {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) {
        return false;
    }

    bool found = false;
    for (int i = 1; i < argc; ++i) {
        if (lstrcmpiW(argv[i], flag) == 0) {
            found = true;
            break;
        }
    }
    LocalFree(argv);
    return found;
}

bool RelaunchElevatedIfNeeded(int showCommand) {
    if (IsProcessElevated() || HasLaunchFlag(L"--elevated")) {
        return false;
    }

    wchar_t exePath[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    SHELLEXECUTEINFOW execInfo{};
    execInfo.cbSize = sizeof(execInfo);
    execInfo.fMask = SEE_MASK_NOCLOSEPROCESS;
    execInfo.lpVerb = L"runas";
    execInfo.lpFile = exePath;
    execInfo.lpParameters = L"--elevated";
    execInfo.nShow = showCommand;

    if (ShellExecuteExW(&execInfo)) {
        if (execInfo.hProcess) {
            CloseHandle(execInfo.hProcess);
        }
        return true;
    }

    return false;
}

UINT GetDpiForWindowSafe(HWND hwnd) {
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    using GetDpiForWindowFn = UINT(WINAPI*)(HWND);

    if (auto fn = reinterpret_cast<GetDpiForWindowFn>(GetProcAddress(user32, "GetDpiForWindow"))) {
        return fn(hwnd);
    }

    HDC dc = GetDC(hwnd);
    const int dpi = dc ? GetDeviceCaps(dc, LOGPIXELSX) : 96;
    if (dc) {
        ReleaseDC(hwnd, dc);
    }
    return dpi > 0 ? static_cast<UINT>(dpi) : 96;
}

UINT GetSystemDpiSafe() {
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    using GetDpiForSystemFn = UINT(WINAPI*)();

    if (auto fn = reinterpret_cast<GetDpiForSystemFn>(GetProcAddress(user32, "GetDpiForSystem"))) {
        return fn();
    }

    HDC dc = GetDC(nullptr);
    const int dpi = dc ? GetDeviceCaps(dc, LOGPIXELSX) : 96;
    if (dc) {
        ReleaseDC(nullptr, dc);
    }
    return dpi > 0 ? static_cast<UINT>(dpi) : 96;
}

void EnableDpiAwareness() {
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    using SetProcessDpiAwarenessContextFn = BOOL(WINAPI*)(HANDLE);
    using SetProcessDPIAwareFn = BOOL(WINAPI*)();

    if (auto fn = reinterpret_cast<SetProcessDpiAwarenessContextFn>(
            GetProcAddress(user32, "SetProcessDpiAwarenessContext"))) {
        if (fn(reinterpret_cast<HANDLE>(-4))) {
            return;
        }
    }

    if (auto fallback = reinterpret_cast<SetProcessDPIAwareFn>(
            GetProcAddress(user32, "SetProcessDPIAware"))) {
        fallback();
    }
}

SIZE GetWindowSizeForDpi(UINT dpi, DWORD style, DWORD exStyle) {
    RECT rect{0, 0, ScaleValue(kBaseClientWidth, dpi), ScaleValue(kBaseClientHeight, dpi)};

    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    using AdjustWindowRectExForDpiFn = BOOL(WINAPI*)(LPRECT, DWORD, BOOL, DWORD, UINT);
    if (auto fn = reinterpret_cast<AdjustWindowRectExForDpiFn>(
            GetProcAddress(user32, "AdjustWindowRectExForDpi"))) {
        fn(&rect, style, FALSE, exStyle, dpi);
    } else {
        AdjustWindowRectEx(&rect, style, FALSE, exStyle);
    }

    return {rect.right - rect.left, rect.bottom - rect.top};
}

void InitializeAppearance(AppState* app) {
    app->physical.accent = RgbHex(82, 132, 255);
    app->pagefile.accent = RgbHex(66, 201, 184);
    app->systemWorkingSet.accent = RgbHex(246, 181, 84);
    app->gpu.dedicated.accent = RgbHex(92, 140, 255);
    app->gpu.shared.accent = RgbHex(255, 134, 88);
    app->statusLine.clear();
}

void DeleteFontObject(HFONT& font) {
    if (font) {
        DeleteObject(font);
        font = nullptr;
    }
}

void RecreateFonts(AppState* app) {
    DeleteFontObject(app->fontSmall);
    DeleteFontObject(app->font);
    DeleteFontObject(app->fontSection);
    DeleteFontObject(app->fontBold);
    DeleteFontObject(app->fontValue);

    const int smallHeight = -MulDiv(9, static_cast<int>(app->dpi), 72);
    const int bodyHeight = -MulDiv(11, static_cast<int>(app->dpi), 72);
    const int sectionHeight = -MulDiv(11, static_cast<int>(app->dpi), 72);
    const int boldHeight = -MulDiv(12, static_cast<int>(app->dpi), 72);
    const int valueHeight = -MulDiv(13, static_cast<int>(app->dpi), 72);

    app->fontSmall = CreateFontW(
        smallHeight, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, kPaintFontQuality,
        DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

    app->font = CreateFontW(
        bodyHeight, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, kPaintFontQuality,
        DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

    app->fontSection = CreateFontW(
        sectionHeight, 0, 0, 0, 500, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, kPaintFontQuality,
        DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

    app->fontBold = CreateFontW(
        boldHeight, 0, 0, 0, 600, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, kPaintFontQuality,
        DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

    app->fontValue = CreateFontW(
        valueHeight, 0, 0, 0, 600, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, kPaintFontQuality,
        DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
}

std::wstring GetAssetPath(const wchar_t* fileName) {
    wchar_t modulePath[MAX_PATH]{};
    GetModuleFileNameW(nullptr, modulePath, MAX_PATH);

    std::wstring path(modulePath);
    const size_t slash = path.find_last_of(L"\\/");
    if (slash != std::wstring::npos) {
        path.resize(slash + 1);
    } else {
        path.clear();
    }

    path += fileName;
    return path;
}

std::wstring GetSettingsPath() {
    return GetAssetPath(L"memtrim.ini");
}

bool LoadDarkThemePreference() {
    wchar_t value[16]{};
    GetPrivateProfileStringW(L"ui", L"theme", L"dark", value, ARRAYSIZE(value), GetSettingsPath().c_str());
    return lstrcmpiW(value, L"light") != 0;
}

void SaveDarkThemePreference(bool darkTheme) {
    WritePrivateProfileStringW(L"ui", L"theme", darkTheme ? L"dark" : L"light", GetSettingsPath().c_str());
}

const ThemePalette& GetTheme(const AppState* app) {
    static const ThemePalette darkTheme{
        RgbHex(15, 17, 21),
        RgbHex(21, 27, 35),
        RgbHex(34, 42, 54),
        RgbHex(245, 248, 252),
        RgbHex(235, 240, 247),
        RgbHex(147, 158, 172),
        RgbHex(11, 16, 22),
        RgbHex(26, 34, 44),
        RgbHex(21, 27, 35),
        RgbHex(26, 33, 42),
        RgbHex(18, 23, 30),
        RgbHex(39, 97, 214),
        RgbHex(44, 104, 220),
        RgbHex(23, 73, 173),
        RgbHex(34, 91, 204),
        RgbHex(42, 100, 216),
        RgbHex(23, 73, 173),
        RgbHex(48, 72, 118),
        RgbHex(187, 212, 255),
        RgbHex(111, 121, 132),
        RgbHex(66, 201, 184),
        RgbHex(255, 255, 255)
    };

    static const ThemePalette lightTheme{
        RgbHex(241, 245, 249),
        RgbHex(255, 255, 255),
        RgbHex(212, 220, 231),
        RgbHex(17, 24, 39),
        RgbHex(31, 41, 55),
        RgbHex(101, 114, 132),
        RgbHex(231, 237, 244),
        RgbHex(207, 216, 228),
        RgbHex(244, 247, 251),
        RgbHex(236, 241, 247),
        RgbHex(226, 233, 242),
        RgbHex(37, 99, 235),
        RgbHex(49, 113, 248),
        RgbHex(29, 78, 216),
        RgbHex(37, 99, 235),
        RgbHex(49, 113, 248),
        RgbHex(29, 78, 216),
        RgbHex(165, 180, 204),
        RgbHex(220, 234, 255),
        RgbHex(124, 140, 163),
        RgbHex(16, 185, 129),
        RgbHex(17, 24, 39)
    };

    return app->darkTheme ? darkTheme : lightTheme;
}

void LoadAppIcons(AppState* app) {
    const std::wstring iconPath = GetAssetPath(L"mem_trim.ico");
    app->iconLarge = static_cast<HICON>(LoadImageW(
        nullptr, iconPath.c_str(), IMAGE_ICON,
        GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON),
        LR_LOADFROMFILE));
    app->iconSmall = static_cast<HICON>(LoadImageW(
        nullptr, iconPath.c_str(), IMAGE_ICON,
        GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON),
        LR_LOADFROMFILE));

    if (app->iconLarge) {
        SendMessageW(app->hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(app->iconLarge));
    }
    if (app->iconSmall) {
        SendMessageW(app->hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(app->iconSmall));
    }
}

std::wstring ReadUtf8TextFile(const std::wstring& path) {
    FILE* file = nullptr;
    _wfopen_s(&file, path.c_str(), L"rb");
    if (!file) {
        return {};
    }

    std::string bytes;
    char buffer[1024];
    size_t read = 0;
    while ((read = fread(buffer, 1, sizeof(buffer), file)) > 0) {
        bytes.append(buffer, read);
    }
    fclose(file);

    if (bytes.size() >= 3 &&
        static_cast<unsigned char>(bytes[0]) == 0xEF &&
        static_cast<unsigned char>(bytes[1]) == 0xBB &&
        static_cast<unsigned char>(bytes[2]) == 0xBF) {
        bytes.erase(0, 3);
    }

    if (bytes.empty()) {
        return {};
    }

    const int chars = MultiByteToWideChar(CP_UTF8, 0, bytes.data(), static_cast<int>(bytes.size()), nullptr, 0);
    if (chars <= 0) {
        return {};
    }

    std::wstring text(chars, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, bytes.data(), static_cast<int>(bytes.size()), text.data(), chars);
    return text;
}

std::wstring ExtractJsonString(const std::wstring& json, const wchar_t* key) {
    std::wstring token = L"\"";
    token += key;
    token += L"\"";

    size_t start = json.find(token);
    if (start == std::wstring::npos) {
        return {};
    }

    start = json.find(L':', start);
    if (start == std::wstring::npos) {
        return {};
    }

    start = json.find(L'"', start);
    if (start == std::wstring::npos) {
        return {};
    }

    ++start;
    size_t end = start;
    while (end < json.size()) {
        if (json[end] == L'"' && json[end - 1] != L'\\') {
            break;
        }
        ++end;
    }

    return end > start ? json.substr(start, end - start) : std::wstring();
}

int CompareVersionText(const std::wstring& left, const std::wstring& right) {
    size_t leftPos = 0;
    size_t rightPos = 0;

    while (leftPos < left.size() || rightPos < right.size()) {
        int leftValue = 0;
        while (leftPos < left.size() && iswdigit(left[leftPos])) {
            leftValue = leftValue * 10 + (left[leftPos] - L'0');
            ++leftPos;
        }

        int rightValue = 0;
        while (rightPos < right.size() && iswdigit(right[rightPos])) {
            rightValue = rightValue * 10 + (right[rightPos] - L'0');
            ++rightPos;
        }

        if (leftValue != rightValue) {
            return (leftValue < rightValue) ? -1 : 1;
        }

        while (leftPos < left.size() && !iswdigit(left[leftPos])) {
            ++leftPos;
        }
        while (rightPos < right.size() && !iswdigit(right[rightPos])) {
            ++rightPos;
        }
    }

    return 0;
}

std::wstring MakeTempFilePath(const wchar_t* prefix, const wchar_t* extension) {
    wchar_t tempPath[MAX_PATH]{};
    if (!GetTempPathW(MAX_PATH, tempPath)) {
        return {};
    }

    wchar_t tempFile[MAX_PATH]{};
    if (!GetTempFileNameW(tempPath, prefix, 0, tempFile)) {
        return {};
    }

    std::wstring filePath = tempFile;
    if (extension && *extension) {
        size_t dot = filePath.find_last_of(L'.');
        if (dot != std::wstring::npos) {
            filePath.resize(dot);
        }
        filePath += extension;
        MoveFileExW(tempFile, filePath.c_str(), MOVEFILE_REPLACE_EXISTING);
    }

    return filePath;
}

DWORD WINAPI UpdateCheckThreadProc(LPVOID param) {
    auto* payload = reinterpret_cast<std::pair<HWND, bool>*>(param);
    const HWND hwnd = payload->first;
    const bool userInitiated = payload->second;
    delete payload;

    auto* result = new UpdateCheckResult();
    result->userInitiated = userInitiated;

    const std::wstring manifestPath = MakeTempFilePath(L"mtu", L".json");
    if (!manifestPath.empty() &&
        SUCCEEDED(URLDownloadToFileW(nullptr, kUpdateManifestUrl, manifestPath.c_str(), 0, nullptr))) {
        const std::wstring json = ReadUtf8TextFile(manifestPath);
        DeleteFileW(manifestPath.c_str());

        result->latestVersion = ExtractJsonString(json, L"version");
        result->installerUrl = ExtractJsonString(json, L"installer_url");
        if (result->installerUrl.empty()) {
            result->installerUrl = kFallbackInstallerUrl;
        }

        result->success = !result->latestVersion.empty();
        result->updateAvailable = result->success && CompareVersionText(kAppVersion, result->latestVersion) < 0;
    } else if (!manifestPath.empty()) {
        DeleteFileW(manifestPath.c_str());
    }

    PostMessageW(hwnd, kUpdateCheckCompletedMessage, 0, reinterpret_cast<LPARAM>(result));
    return 0;
}

void StartUpdateCheck(AppState* app, bool userInitiated) {
    if (app->update.checking || app->update.installInProgress) {
        return;
    }

    app->update.checking = true;
    if (userInitiated) {
        app->statusLine = L"Checking for updates...";
        InvalidateRect(app->hwnd, &app->statusRect, FALSE);
    }

    auto* payload = new std::pair<HWND, bool>(app->hwnd, userInitiated);
    HANDLE thread = CreateThread(nullptr, 0, UpdateCheckThreadProc, payload, 0, nullptr);
    if (!thread) {
        delete payload;
        app->update.checking = false;
        if (userInitiated) {
            app->statusLine = L"Unable to start the update check.";
        }
        return;
    }
    CloseHandle(thread);
}

bool DownloadAndLaunchInstaller(AppState* app, const std::wstring& installerUrl) {
    std::wstring downloadUrl = installerUrl.empty() ? std::wstring(kFallbackInstallerUrl) : installerUrl;
    const std::wstring installerPath = MakeTempFilePath(L"mti", L".exe");
    if (installerPath.empty()) {
        return false;
    }

    app->update.installInProgress = true;
    app->statusLine = L"Downloading update installer...";
    InvalidateRect(app->hwnd, nullptr, FALSE);
    UpdateWindow(app->hwnd);
    SetCursor(app->cursorWait);

    const HRESULT downloadResult = URLDownloadToFileW(nullptr, downloadUrl.c_str(), installerPath.c_str(), 0, nullptr);
    if (FAILED(downloadResult)) {
        app->update.installInProgress = false;
        SetCursor(app->cursorArrow);
        DeleteFileW(installerPath.c_str());
        ShellExecuteW(app->hwnd, L"open", downloadUrl.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        app->statusLine = L"Opened the update download in your browser.";
        return false;
    }

    SHELLEXECUTEINFOW execInfo{};
    execInfo.cbSize = sizeof(execInfo);
    execInfo.fMask = SEE_MASK_NOCLOSEPROCESS;
    execInfo.hwnd = app->hwnd;
    execInfo.lpVerb = L"open";
    execInfo.lpFile = installerPath.c_str();
    execInfo.nShow = SW_SHOWNORMAL;

    const bool launched = ShellExecuteExW(&execInfo) != FALSE;
    app->update.installInProgress = false;
    SetCursor(app->cursorArrow);

    if (!launched) {
        app->statusLine = L"Downloaded the update, but Windows could not launch it.";
        return false;
    }

    if (execInfo.hProcess) {
        CloseHandle(execInfo.hProcess);
    }
    app->exitRequested = true;
    DestroyWindow(app->hwnd);
    return true;
}

void PromptForUpdate(AppState* app) {
    if (!app->update.available || app->update.promptShown) {
        return;
    }

    app->update.promptShown = true;
    std::wstring message = L"MemTrim Lite ";
    message += app->update.latestVersion;
    message += L" is available.\n\nDownload and launch the updater now?";

    const int answer = MessageBoxW(app->hwnd, message.c_str(), L"Update available",
                                   MB_YESNO | MB_ICONINFORMATION | MB_SETFOREGROUND);
    if (answer == IDYES) {
        DownloadAndLaunchInstaller(app, app->update.installerUrl);
    } else {
        app->statusLine = BuildIdleStatus(app);
        InvalidateRect(app->hwnd, &app->statusRect, FALSE);
    }
}

void AddTrayIcon(AppState* app) {
    ZeroMemory(&app->trayIcon, sizeof(app->trayIcon));
    app->trayIcon.cbSize = sizeof(app->trayIcon);
    app->trayIcon.hWnd = app->hwnd;
    app->trayIcon.uID = kTrayIconId;
    app->trayIcon.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
    app->trayIcon.uCallbackMessage = kTrayMessage;
    app->trayIcon.hIcon = app->iconSmall ? app->iconSmall : LoadIconW(nullptr, IDI_APPLICATION);
    lstrcpynW(app->trayIcon.szTip, L"MemTrim Lite", ARRAYSIZE(app->trayIcon.szTip));

    if (Shell_NotifyIconW(NIM_ADD, &app->trayIcon)) {
        app->trayIcon.uVersion = NOTIFYICON_VERSION_4;
        Shell_NotifyIconW(NIM_SETVERSION, &app->trayIcon);
        app->trayIconAdded = true;
    }
}

void RemoveTrayIcon(AppState* app) {
    if (app->trayIconAdded) {
        Shell_NotifyIconW(NIM_DELETE, &app->trayIcon);
        app->trayIconAdded = false;
    }
    if (app->trayUsageIcon) {
        DestroyIcon(app->trayUsageIcon);
        app->trayUsageIcon = nullptr;
    }
}

void RestoreFromTray(AppState* app) {
    ShowWindow(app->hwnd, SW_SHOW);
    ShowWindow(app->hwnd, SW_RESTORE);
    SetForegroundWindow(app->hwnd);
}

void MinimizeToTray(AppState* app) {
    ShowWindow(app->hwnd, SW_HIDE);
}

void ShowTrayMenu(AppState* app) {
    HMENU menu = CreatePopupMenu();
    if (!menu) {
        return;
    }

    AppendMenuW(menu, MF_STRING, kTrayMenuOpen, L"Open");
    AppendMenuW(menu, MF_STRING, kTrayMenuCheckUpdates, L"Check for updates");
    if (app->update.available) {
        AppendMenuW(menu, MF_STRING, kTrayMenuInstallUpdate, L"Install update");
    }
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kTrayMenuExit, L"Exit");

    POINT pt{};
    GetCursorPos(&pt);
    SetForegroundWindow(app->hwnd);
    const UINT command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY, pt.x, pt.y, 0, app->hwnd, nullptr);
    DestroyMenu(menu);

    if (command == kTrayMenuOpen) {
        RestoreFromTray(app);
    } else if (command == kTrayMenuCheckUpdates) {
        StartUpdateCheck(app, true);
    } else if (command == kTrayMenuInstallUpdate) {
        DownloadAndLaunchInstaller(app, app->update.installerUrl);
    } else if (command == kTrayMenuExit) {
        app->exitRequested = true;
        RemoveTrayIcon(app);
        DestroyWindow(app->hwnd);
    }
}

HICON CreateTrayUsageIcon(AppState* app) {
    const ThemePalette& theme = GetTheme(app);
    const int iconSize = GetSystemMetrics(SM_CXSMICON);
    Gdiplus::Bitmap bitmap(iconSize, iconSize, PixelFormat32bppARGB);
    Gdiplus::Graphics graphics(&bitmap);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    graphics.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
    graphics.Clear(Gdiplus::Color(0, 0, 0, 0));

    const int usage = std::clamp(static_cast<int>(app->physical.usagePercent + 0.5), 0, 99);
    wchar_t text[4];
    swprintf_s(text, L"%d", usage);

    const COLORREF accent = GetUsageAccent(app->physical.usagePercent);
    const Gdiplus::REAL badgeSize = static_cast<Gdiplus::REAL>(iconSize) - 1.0f;
    const Gdiplus::REAL radius = 3.5f;

    Gdiplus::GraphicsPath badgePath;
    badgePath.AddArc(0.5f, 0.5f, radius, radius, 180.0f, 90.0f);
    badgePath.AddArc(badgeSize - radius, 0.5f, radius, radius, 270.0f, 90.0f);
    badgePath.AddArc(badgeSize - radius, badgeSize - radius, radius, radius, 0.0f, 90.0f);
    badgePath.AddArc(0.5f, badgeSize - radius, radius, radius, 90.0f, 90.0f);
    badgePath.CloseFigure();

    Gdiplus::SolidBrush bgBrush(ToGdiPlusColor(BlendColor(theme.panelBg, accent, app->darkTheme ? 28 : 18)));
    Gdiplus::Pen borderPen(ToGdiPlusColor(BlendColor(theme.panelBorder, accent, app->darkTheme ? 52 : 34)), 1.0f);
    graphics.FillPath(&bgBrush, &badgePath);
    graphics.DrawPath(&borderPen, &badgePath);

    Gdiplus::RectF bounds(-0.2f, -1.1f, static_cast<Gdiplus::REAL>(iconSize) + 0.4f, static_cast<Gdiplus::REAL>(iconSize) + 0.8f);
    Gdiplus::FontFamily fontFamily(L"Segoe UI");
    Gdiplus::Font font(&fontFamily, usage >= 10 ? 9.2f : 11.2f, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    Gdiplus::StringFormat format;
    format.SetAlignment(Gdiplus::StringAlignmentCenter);
    format.SetLineAlignment(Gdiplus::StringAlignmentCenter);
    format.SetFormatFlags(Gdiplus::StringFormatFlagsNoClip);

    Gdiplus::SolidBrush textBrush(Gdiplus::Color(255, 255, 255, 255));
    graphics.DrawString(text, -1, &font, bounds, &format, &textBrush);

    HICON icon = nullptr;
    bitmap.GetHICON(&icon);
    return icon;
}

void UpdateTrayIcon(AppState* app) {
    if (!app->trayIconAdded) {
        return;
    }

    HICON newIcon = CreateTrayUsageIcon(app);
    if (!newIcon) {
        return;
    }

    if (app->trayUsageIcon) {
        DestroyIcon(app->trayUsageIcon);
    }
    app->trayUsageIcon = newIcon;
    app->trayIcon.hIcon = app->trayUsageIcon;

    wchar_t tip[128];
    swprintf_s(tip, L"MemTrim Lite - RAM %.1f%% used", app->physical.usagePercent);
    lstrcpynW(app->trayIcon.szTip, tip, ARRAYSIZE(app->trayIcon.szTip));
    app->trayIcon.uFlags = NIF_ICON | NIF_TIP;
    Shell_NotifyIconW(NIM_MODIFY, &app->trayIcon);
    app->trayIcon.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
}

double BytesToGB(ULONGLONG bytes) {
    return static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0);
}

double PagesToMB(SIZE_T pages, SIZE_T pageSize) {
    return static_cast<double>(pages) * static_cast<double>(pageSize) / (1024.0 * 1024.0);
}

void UpdateMemoryStats(AppState* app) {
    MEMORYSTATUSEX memStatus{};
    memStatus.dwLength = sizeof(memStatus);
    if (GlobalMemoryStatusEx(&memStatus)) {
        const double physTotal = BytesToGB(memStatus.ullTotalPhys);
        const double physAvail = BytesToGB(memStatus.ullAvailPhys);
        app->physical.unit = L"GB";
        app->physical.decimals = 2;
        app->physical.total = physTotal;
        app->physical.available = physAvail;
        app->physical.usagePercent = physTotal > 0.0 ? (100.0 * (physTotal - physAvail) / physTotal) : 0.0;

        const double pageTotal = BytesToGB(memStatus.ullTotalPageFile);
        const double pageAvail = BytesToGB(memStatus.ullAvailPageFile);
        app->pagefile.unit = L"GB";
        app->pagefile.decimals = 2;
        app->pagefile.total = pageTotal;
        app->pagefile.available = pageAvail;
        app->pagefile.usagePercent = pageTotal > 0.0 ? (100.0 * (pageTotal - pageAvail) / pageTotal) : 0.0;
    }

    PERFORMANCE_INFORMATION perfInfo{};
    perfInfo.cb = sizeof(perfInfo);
    if (GetPerformanceInfo(&perfInfo, sizeof(perfInfo))) {
        const double totalMb = PagesToMB(perfInfo.PhysicalTotal, perfInfo.PageSize);
        const double cacheMb = PagesToMB(perfInfo.SystemCache, perfInfo.PageSize);
        const double availMb = std::max(0.0, totalMb - cacheMb);

        app->systemWorkingSet.unit = L"MB";
        app->systemWorkingSet.decimals = 0;
        app->systemWorkingSet.total = totalMb;
        app->systemWorkingSet.available = availMb;
        app->systemWorkingSet.usagePercent = totalMb > 0.0 ? (100.0 * cacheMb / totalMb) : 0.0;
    }
}

void ReleaseGpuAdapter(AppState* app) {
    if (app->gpuAdapter) {
        app->gpuAdapter->Release();
        app->gpuAdapter = nullptr;
    }
}

void InitializeGpuAdapter(AppState* app) {
    ReleaseGpuAdapter(app);
    app->gpu.available = false;
    app->gpu.name = L"No compatible GPU detected";
    app->gpu.dedicated = {};
    app->gpu.shared = {};
    app->gpu.dedicated.unit = L"GB";
    app->gpu.shared.unit = L"GB";
    app->gpu.dedicated.decimals = 2;
    app->gpu.shared.decimals = 2;
    app->gpu.dedicated.accent = RgbHex(92, 140, 255);
    app->gpu.shared.accent = RgbHex(255, 134, 88);

    IDXGIFactory1* factory = nullptr;
    if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&factory))) || !factory) {
        return;
    }

    SIZE_T bestDedicated = 0;
    for (UINT index = 0;; ++index) {
        IDXGIAdapter1* adapter = nullptr;
        if (factory->EnumAdapters1(index, &adapter) == DXGI_ERROR_NOT_FOUND) {
            break;
        }

        DXGI_ADAPTER_DESC1 desc{};
        if (SUCCEEDED(adapter->GetDesc1(&desc)) && !(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) {
            IDXGIAdapter3* adapter3 = nullptr;
            if (SUCCEEDED(adapter->QueryInterface(__uuidof(IDXGIAdapter3), reinterpret_cast<void**>(&adapter3))) &&
                adapter3) {
                const SIZE_T score = desc.DedicatedVideoMemory ? desc.DedicatedVideoMemory : desc.SharedSystemMemory;
                if (!app->gpuAdapter || score >= bestDedicated) {
                    if (app->gpuAdapter) {
                        app->gpuAdapter->Release();
                    }
                    app->gpuAdapter = adapter3;
                    bestDedicated = score;
                    app->gpu.name = desc.Description;
                    app->gpu.available = true;
                } else {
                    adapter3->Release();
                }
            }
        }

        adapter->Release();
    }

    factory->Release();
}

void UpdateGpuStats(AppState* app) {
    if (!app->gpuAdapter) {
        app->gpu.available = false;
        app->gpu.name = L"No compatible GPU detected";
        return;
    }

    DXGI_ADAPTER_DESC2 desc{};
    if (FAILED(app->gpuAdapter->GetDesc2(&desc))) {
        app->gpu.available = false;
        app->gpu.name = L"GPU adapter unavailable";
        return;
    }

    app->gpu.available = true;
    app->gpu.name = desc.Description;

    DXGI_QUERY_VIDEO_MEMORY_INFO localInfo{};
    DXGI_QUERY_VIDEO_MEMORY_INFO nonLocalInfo{};

    const bool hasLocal = SUCCEEDED(app->gpuAdapter->QueryVideoMemoryInfo(
        0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &localInfo));
    const bool hasShared = SUCCEEDED(app->gpuAdapter->QueryVideoMemoryInfo(
        0, DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL, &nonLocalInfo));

    const ULONGLONG localTotalBytes = desc.DedicatedVideoMemory ? desc.DedicatedVideoMemory :
        (hasLocal ? localInfo.Budget : 0ULL);
    const ULONGLONG localUsedBytes = hasLocal ? std::min(localInfo.CurrentUsage, localTotalBytes) : 0ULL;
    const double localTotal = BytesToGB(localTotalBytes);
    const double localUsed = BytesToGB(localUsedBytes);
    app->gpu.dedicated.unit = L"GB";
    app->gpu.dedicated.decimals = 2;
    app->gpu.dedicated.total = localTotal;
    app->gpu.dedicated.available = std::max(0.0, localTotal - localUsed);
    app->gpu.dedicated.usagePercent = localTotal > 0.0 ? (100.0 * localUsed / localTotal) : 0.0;

    const ULONGLONG sharedTotalBytes = desc.SharedSystemMemory ? desc.SharedSystemMemory :
        (hasShared ? nonLocalInfo.Budget : 0ULL);
    const ULONGLONG sharedUsedBytes = hasShared ? std::min(nonLocalInfo.CurrentUsage, sharedTotalBytes) : 0ULL;
    const double sharedTotal = BytesToGB(sharedTotalBytes);
    const double sharedUsed = BytesToGB(sharedUsedBytes);
    app->gpu.shared.unit = L"GB";
    app->gpu.shared.decimals = 2;
    app->gpu.shared.total = sharedTotal;
    app->gpu.shared.available = std::max(0.0, sharedTotal - sharedUsed);
    app->gpu.shared.usagePercent = sharedTotal > 0.0 ? (100.0 * sharedUsed / sharedTotal) : 0.0;
}

std::wstring FormatPercent(double value) {
    wchar_t buffer[32];
    swprintf_s(buffer, L"%.1f%%", value);
    return buffer;
}

std::wstring FormatAmount(double value, const wchar_t* unit, int decimals) {
    wchar_t buffer[64];
    if (decimals <= 0) {
        swprintf_s(buffer, L"%.0f %s", value, unit);
    } else {
        swprintf_s(buffer, L"%.2f %s", value, unit);
    }
    return buffer;
}

std::wstring FormatGpuSummary(double dedicatedTotalGb, double sharedTotalGb) {
    wchar_t buffer[96];
    swprintf_s(buffer, L"Dedicated %.2f GB  •  Shared %.2f GB", dedicatedTotalGb, sharedTotalGb);
    return buffer;
}

std::wstring BuildIdleStatus(const AppState* app) {
    if (app->update.available) {
        std::wstring status = L"Update available: v";
        status += app->update.latestVersion;
        status += L". Open the tray menu to install it.";
        return status;
    }
    return app->isElevated
        ? L"Administrator mode. Deeper cleaning available."
        : L"Standard mode. Deeper cleaning may be limited.";
}

COLORREF GetUsageAccent(double usagePercent) {
    if (usagePercent >= 85.0) {
        return RgbHex(255, 134, 88);
    }
    if (usagePercent >= 65.0) {
        return RgbHex(246, 181, 84);
    }
    return RgbHex(82, 132, 255);
}

void FillRoundedRect(HDC dc, const RECT& rect, COLORREF fill, COLORREF border, int radius) {
    if (radius <= 0) {
        HBRUSH brush = CreateSolidBrush(fill);
        HPEN pen = CreatePen(PS_SOLID, 1, border);
        const HGDIOBJ oldBrush = SelectObject(dc, brush);
        const HGDIOBJ oldPen = SelectObject(dc, pen);
        Rectangle(dc, rect.left, rect.top, rect.right, rect.bottom);
        SelectObject(dc, oldPen);
        SelectObject(dc, oldBrush);
        DeleteObject(pen);
        DeleteObject(brush);
        return;
    }

    HBRUSH brush = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, 1, border);
    const HGDIOBJ oldBrush = SelectObject(dc, brush);
    const HGDIOBJ oldPen = SelectObject(dc, pen);

    RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, radius, radius);

    SelectObject(dc, oldPen);
    SelectObject(dc, oldBrush);
    DeleteObject(pen);
    DeleteObject(brush);
}

void FillRoundedRectAA(HDC dc, const RECT& rect, COLORREF fill, COLORREF border, int radius) {
    if (radius <= 0) {
        FillRoundedRect(dc, rect, fill, border, 0);
        return;
    }

    Gdiplus::Graphics graphics(dc);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);

    const Gdiplus::REAL x = static_cast<Gdiplus::REAL>(rect.left) + 0.5f;
    const Gdiplus::REAL y = static_cast<Gdiplus::REAL>(rect.top) + 0.5f;
    const Gdiplus::REAL width = static_cast<Gdiplus::REAL>(rect.right - rect.left) - 1.0f;
    const Gdiplus::REAL height = static_cast<Gdiplus::REAL>(rect.bottom - rect.top) - 1.0f;
    const Gdiplus::REAL diameter = static_cast<Gdiplus::REAL>(radius * 2);

    Gdiplus::GraphicsPath path;
    path.AddArc(x, y, diameter, diameter, 180.0f, 90.0f);
    path.AddArc(x + width - diameter, y, diameter, diameter, 270.0f, 90.0f);
    path.AddArc(x + width - diameter, y + height - diameter, diameter, diameter, 0.0f, 90.0f);
    path.AddArc(x, y + height - diameter, diameter, diameter, 90.0f, 90.0f);
    path.CloseFigure();

    Gdiplus::SolidBrush brush(ToGdiPlusColor(fill));
    Gdiplus::Pen pen(ToGdiPlusColor(border), 1.0f);
    graphics.FillPath(&brush, &path);
    graphics.DrawPath(&pen, &path);
}

void FillSolidRect(HDC dc, const RECT& rect, COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color);
    FillRect(dc, &rect, brush);
    DeleteObject(brush);
}

void DrawTextLine(HDC dc, const RECT& rect, const wchar_t* text, COLORREF color, UINT format, HFONT font) {
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    const HGDIOBJ oldFont = SelectObject(dc, font);
    RECT drawRect = rect;
    DrawTextW(dc, text, -1, &drawRect, format);
    SelectObject(dc, oldFont);
}

void DrawTextLine(HDC dc, const RECT& rect, const std::wstring& text, COLORREF color, UINT format, HFONT font) {
    DrawTextLine(dc, rect, text.c_str(), color, format, font);
}

void RecalculateLayout(AppState* app) {
    RECT client{};
    GetClientRect(app->hwnd, &client);

    const int pad = ScaleValue(app, 14);
    const int gap = ScaleValue(app, 8);
    const int topBarHeight = ScaleValue(app, 28);
    const int sectionHeight = ScaleValue(app, 94);
    const int statusHeight = ScaleValue(app, 16);
    const int buttonHeight = ScaleValue(app, 38);
    const int bottomPad = ScaleValue(app, 14);

    int y = pad;
    app->topBarRect = {pad, y, client.right - pad, y + topBarHeight};
    app->gpuToggleRect = {
        app->topBarRect.left + ScaleValue(app, 28),
        app->topBarRect.top + ScaleValue(app, 1),
        app->topBarRect.left + ScaleValue(app, 82),
        app->topBarRect.bottom - ScaleValue(app, 1)};
    app->themeToggleRect = {
        app->topBarRect.right - ScaleValue(app, 76),
        app->topBarRect.top + ScaleValue(app, 1),
        app->topBarRect.right,
        app->topBarRect.bottom - ScaleValue(app, 1)};
    y = app->topBarRect.bottom + gap;

    for (int i = 0; i < 3; ++i) {
        app->sections[i] = {pad, y, client.right - pad, y + sectionHeight};
        y += sectionHeight + gap;
    }

    app->statusRect = {pad, y, client.right - pad, y + statusHeight};
    y = app->statusRect.bottom + ScaleValue(app, 6);
    app->buttonRect = {pad, y, client.right - pad, y + buttonHeight};

    const int overflow = app->buttonRect.bottom + bottomPad - client.bottom;
    if (overflow > 0) {
        OffsetRect(&app->buttonRect, 0, -overflow);
        OffsetRect(&app->statusRect, 0, -overflow);
    }
}

void DrawBadge(HDC dc, AppState* app, const RECT& rect, const std::wstring& text, COLORREF accent) {
    const ThemePalette& theme = GetTheme(app);
    const COLORREF badgeText = app->darkTheme
        ? BlendColor(accent, theme.badgeTextBase, 18)
        : BlendColor(accent, theme.badgeTextBase, 12);
    DrawTextLine(dc, rect, text, badgeText,
                 DT_CENTER | DT_VCENTER | DT_SINGLELINE, app->fontBold);
}

void DrawLogoMark(HDC dc, AppState* app, const RECT& rect) {
    const int barRadius = ScaleValue(app, 3);
    RECT bar1{rect.left + ScaleValue(app, 1), rect.top + ScaleValue(app, 7),
              rect.left + ScaleValue(app, 7), rect.bottom - ScaleValue(app, 1)};
    RECT bar2{bar1.right + ScaleValue(app, 3), rect.top + ScaleValue(app, 3),
              bar1.right + ScaleValue(app, 9), rect.bottom - ScaleValue(app, 1)};
    RECT bar3{bar2.right + ScaleValue(app, 3), rect.top + ScaleValue(app, 9),
              bar2.right + ScaleValue(app, 9), rect.bottom - ScaleValue(app, 1)};

    FillRoundedRect(dc, bar1, app->physical.accent, app->physical.accent, barRadius);
    FillRoundedRect(dc, bar2, app->pagefile.accent, app->pagefile.accent, barRadius);
    FillRoundedRect(dc, bar3, app->systemWorkingSet.accent, app->systemWorkingSet.accent, barRadius);
}

void DrawToolbar(AppState* app, HDC dc) {
    const ThemePalette& theme = GetTheme(app);
    RECT logoRect{
        app->topBarRect.left,
        app->topBarRect.top + ScaleValue(app, 5),
        app->topBarRect.left + ScaleValue(app, 21),
        app->topBarRect.bottom - ScaleValue(app, 4)};
    DrawLogoMark(dc, app, logoRect);

    COLORREF fill = app->showGpu ? theme.toolbarActive : theme.toolbarIdle;
    if (app->gpuButtonDown) {
        fill = app->showGpu ? theme.toolbarActiveDown : theme.toolbarIdleDown;
    } else if (app->gpuButtonHot) {
        fill = app->showGpu ? theme.toolbarActiveHover : theme.toolbarIdleHover;
    }

    FillRoundedRectAA(dc, app->gpuToggleRect, fill, fill, 0);
    DrawTextLine(dc, app->gpuToggleRect, L"GPU",
                 app->showGpu ? RgbHex(255, 255, 255) : theme.mutedText,
                 DT_CENTER | DT_VCENTER | DT_SINGLELINE, app->fontBold);

    COLORREF themeFill = theme.toolbarIdle;
    if (app->themeButtonDown) {
        themeFill = theme.toolbarIdleDown;
    } else if (app->themeButtonHot) {
        themeFill = theme.toolbarIdleHover;
    }

    FillRoundedRectAA(dc, app->themeToggleRect, themeFill, themeFill, 0);
    DrawTextLine(dc, app->themeToggleRect, app->darkTheme ? L"Bright" : L"Dark",
                 theme.bodyText, DT_CENTER | DT_VCENTER | DT_SINGLELINE, app->fontBold);
}

void DrawMetricBlock(HDC dc, AppState* app, const RECT& rect, const wchar_t* label, const std::wstring& value) {
    const ThemePalette& theme = GetTheme(app);
    RECT labelRect{rect.left, rect.top, rect.left + ScaleValue(app, 62), rect.bottom};
    RECT valueRect{labelRect.right + ScaleValue(app, 6), rect.top, rect.right, rect.bottom};
    DrawTextLine(dc, labelRect, label, theme.mutedText,
                 DT_LEFT | DT_VCENTER | DT_SINGLELINE, app->fontSmall);
    DrawTextLine(dc, valueRect, value, theme.bodyText,
                 DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS, app->fontValue);
}

void DrawUsageBar(HDC dc, AppState* app, const RECT& rect, double usagePercent, COLORREF accent) {
    const ThemePalette& theme = GetTheme(app);
    const RECT track = rect;
    FillRoundedRect(dc, track, theme.trackBg, theme.trackBorder, 0);

    RECT fill = track;
    const int width = track.right - track.left;
    const int fillWidth = static_cast<int>(width * std::clamp(usagePercent, 0.0, 100.0) / 100.0);
    fill.right = fill.left + std::max(0, fillWidth);
    if (fill.right > fill.left) {
        FillRoundedRect(dc, fill, accent, accent, 0);
    }
}

void DrawSection(HDC dc, AppState* app, const RECT& rect, const wchar_t* title, const MemorySection& section) {
    const ThemePalette& theme = GetTheme(app);
    const int innerPad = ScaleValue(app, 12);
    FillRoundedRect(dc, rect, theme.panelBg, theme.panelBorder, 0);

    RECT titleRect{
        rect.left + innerPad,
        rect.top + ScaleValue(app, 8),
        rect.right - innerPad - ScaleValue(app, 78),
        rect.top + ScaleValue(app, 30)};
    DrawTextLine(dc, titleRect, title, theme.titleText,
                 DT_LEFT | DT_TOP | DT_SINGLELINE, app->fontSection);

    RECT badgeRect{
        rect.right - innerPad - ScaleValue(app, 72),
        rect.top + ScaleValue(app, 8),
        rect.right - innerPad,
        rect.top + ScaleValue(app, 28)};
    DrawBadge(dc, app, badgeRect, FormatPercent(section.usagePercent), section.accent);

    RECT barRect{
        rect.left + innerPad,
        rect.top + ScaleValue(app, 38),
        rect.right - innerPad,
        rect.top + ScaleValue(app, 44)};
    DrawUsageBar(dc, app, barRect, section.usagePercent, section.accent);

    RECT availableRect{
        rect.left + innerPad,
        rect.top + ScaleValue(app, 56),
        rect.left + (rect.right - rect.left) / 2 - ScaleValue(app, 8),
        rect.bottom - ScaleValue(app, 10)};
    RECT totalRect{
        rect.left + (rect.right - rect.left) / 2 + ScaleValue(app, 8),
        rect.top + ScaleValue(app, 56),
        rect.right - innerPad,
        rect.bottom - ScaleValue(app, 10)};

    DrawMetricBlock(dc, app, availableRect, L"Available",
                    FormatAmount(section.available, section.unit, section.decimals));
    DrawMetricBlock(dc, app, totalRect, L"Total",
                    FormatAmount(section.total, section.unit, section.decimals));
}

void DrawGpuNameSection(HDC dc, AppState* app, const RECT& rect) {
    const ThemePalette& theme = GetTheme(app);
    const int innerPad = ScaleValue(app, 12);
    FillRoundedRect(dc, rect, theme.panelBg, theme.panelBorder, 0);

    RECT titleRect{
        rect.left + innerPad,
        rect.top + ScaleValue(app, 10),
        rect.right - innerPad,
        rect.top + ScaleValue(app, 26)};
    DrawTextLine(dc, titleRect, L"GPU adapter", theme.titleText,
                 DT_LEFT | DT_TOP | DT_SINGLELINE, app->fontSection);

    RECT nameRect{
        rect.left + innerPad,
        rect.top + ScaleValue(app, 34),
        rect.right - innerPad,
        rect.top + ScaleValue(app, 58)};
    DrawTextLine(dc, nameRect, app->gpu.name,
                 app->gpu.available ? theme.bodyText : theme.mutedText,
                 DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS, app->fontValue);

    RECT summaryRect{
        rect.left + innerPad,
        rect.bottom - ScaleValue(app, 22),
        rect.right - innerPad,
        rect.bottom - ScaleValue(app, 8)};
    DrawTextLine(dc, summaryRect,
                 FormatGpuSummary(app->gpu.dedicated.total, app->gpu.shared.total),
                 theme.mutedText,
                 DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS, app->fontSmall);
}

void DrawStatus(HDC dc, AppState* app) {
    const ThemePalette& theme = GetTheme(app);
    RECT dotRect{
        app->statusRect.left,
        app->statusRect.top + ScaleValue(app, 4),
        app->statusRect.left + ScaleValue(app, 8),
        app->statusRect.top + ScaleValue(app, 12)};
    FillRoundedRect(dc, dotRect, theme.statusDot, theme.statusDot, 0);

    RECT textRect{
        dotRect.right + ScaleValue(app, 8),
        app->statusRect.top,
        app->statusRect.right,
        app->statusRect.bottom};
    DrawTextLine(dc, textRect, app->statusLine, theme.mutedText,
                 DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS, app->fontSmall);
}

void DrawButton(HDC dc, AppState* app) {
    const ThemePalette& theme = GetTheme(app);
    COLORREF fill = theme.primaryButton;
    if (app->isCleaning) {
        fill = theme.primaryButtonDisabled;
    } else if (app->buttonDown) {
        fill = theme.primaryButtonDown;
    } else if (app->buttonHot) {
        fill = theme.primaryButtonHover;
    }

    FillRoundedRect(dc, app->buttonRect, fill, fill, 0);

    RECT hintRect{
        app->buttonRect.left + ScaleValue(app, 14),
        app->buttonRect.top,
        app->buttonRect.left + ScaleValue(app, 110),
        app->buttonRect.bottom};
    DrawTextLine(dc, hintRect, app->showGpu ? L"VIEW" : L"RAM",
                 app->isCleaning ? theme.primaryHintDisabled : theme.primaryHint,
                 DT_LEFT | DT_VCENTER | DT_SINGLELINE, app->fontSmall);

    const wchar_t* buttonText = app->isCleaning ? L"Cleaning..." :
        (app->showGpu ? L"Back to memory" : L"Clean memory");
    DrawTextLine(dc, app->buttonRect, buttonText, RgbHex(255, 255, 255),
                 DT_CENTER | DT_VCENTER | DT_SINGLELINE, app->fontBold);
}

void PaintWindow(AppState* app, HDC paintDc, const RECT& paintRect) {
    RECT client{};
    GetClientRect(app->hwnd, &client);

    const int width = client.right - client.left;
    const int height = client.bottom - client.top;
    if (width <= 0 || height <= 0) {
        return;
    }

    HDC memDc = CreateCompatibleDC(paintDc);
    HBITMAP bitmap = CreateCompatibleBitmap(paintDc, width, height);
    const HGDIOBJ oldBitmap = SelectObject(memDc, bitmap);

    FillSolidRect(memDc, client, GetTheme(app).windowBg);
    DrawToolbar(app, memDc);
    if (app->showGpu) {
        DrawGpuNameSection(memDc, app, app->sections[0]);
        DrawSection(memDc, app, app->sections[1], L"Dedicated memory", app->gpu.dedicated);
        DrawSection(memDc, app, app->sections[2], L"Shared GPU memory", app->gpu.shared);
    } else {
        DrawSection(memDc, app, app->sections[0], L"Physical memory", app->physical);
        DrawSection(memDc, app, app->sections[1], L"Pagefile", app->pagefile);
        DrawSection(memDc, app, app->sections[2], L"System working set", app->systemWorkingSet);
    }
    DrawStatus(memDc, app);
    DrawButton(memDc, app);

    BitBlt(paintDc, paintRect.left, paintRect.top,
           paintRect.right - paintRect.left, paintRect.bottom - paintRect.top,
           memDc, paintRect.left, paintRect.top, SRCCOPY);

    SelectObject(memDc, oldBitmap);
    DeleteObject(bitmap);
    DeleteDC(memDc);
}

void EnableWindowChrome(HWND hwnd, bool darkThemeEnabled) {
    const BOOL darkMode = darkThemeEnabled ? TRUE : FALSE;
    if (FAILED(DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &darkMode, sizeof(darkMode)))) {
        constexpr DWORD kLegacyDarkMode = 19;
        DwmSetWindowAttribute(hwnd, kLegacyDarkMode, &darkMode, sizeof(darkMode));
    }

    const DWORD corner = kCornerDoNotRound;
    DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));
}

bool EnablePrivilege(const wchar_t* privilegeName) {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token)) {
        return false;
    }

    TOKEN_PRIVILEGES privileges{};
    privileges.PrivilegeCount = 1;
    if (!LookupPrivilegeValueW(nullptr, privilegeName, &privileges.Privileges[0].Luid)) {
        CloseHandle(token);
        return false;
    }

    privileges.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    SetLastError(ERROR_SUCCESS);
    AdjustTokenPrivileges(token, FALSE, &privileges, sizeof(privileges), nullptr, nullptr);
    const bool ok = (GetLastError() == ERROR_SUCCESS);
    CloseHandle(token);
    return ok;
}

bool TryMemoryCommand(NtSetSystemInformationFn fn, MemoryListCommand command) {
    ULONG payload = static_cast<ULONG>(command);
    return fn && NT_SUCCESS(fn(kSystemMemoryListInformation, &payload, sizeof(payload)));
}

int TrimAccessibleWorkingSets() {
    int trimmedCount = 0;

    auto trimHandle = [&trimmedCount](HANDLE process) {
        if (!process) {
            return;
        }

        bool changed = false;
        if (SetProcessWorkingSetSize(process, static_cast<SIZE_T>(-1), static_cast<SIZE_T>(-1))) {
            changed = true;
        }
        if (EmptyWorkingSet(process)) {
            changed = true;
        }
        if (changed) {
            ++trimmedCount;
        }
    };

    trimHandle(GetCurrentProcess());

    DWORD bytesNeeded = 0;
    std::vector<DWORD> processes(1024);
    while (true) {
        if (!EnumProcesses(processes.data(),
                           static_cast<DWORD>(processes.size() * sizeof(DWORD)),
                           &bytesNeeded)) {
            return trimmedCount;
        }

        if (bytesNeeded < processes.size() * sizeof(DWORD)) {
            break;
        }

        processes.resize(processes.size() * 2);
    }

    const size_t count = bytesNeeded / sizeof(DWORD);
    for (size_t i = 0; i < count; ++i) {
        const DWORD pid = processes[i];
        if (pid == 0 || pid == GetCurrentProcessId()) {
            continue;
        }

        HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_SET_QUOTA, FALSE, pid);
        if (!process) {
            continue;
        }

        trimHandle(process);
        CloseHandle(process);
    }

    return trimmedCount;
}

CleanSummary RunMemoryClean() {
    CleanSummary summary{};
    const DWORD started = GetTickCount();

    summary.trimmedProcesses = TrimAccessibleWorkingSets();

    bool loadedLibrary = false;
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (!ntdll) {
        ntdll = LoadLibraryW(L"ntdll.dll");
        loadedLibrary = (ntdll != nullptr);
    }

    if (ntdll && EnablePrivilege(SE_PROF_SINGLE_PROCESS_NAME)) {
        const auto fn = reinterpret_cast<NtSetSystemInformationFn>(
            GetProcAddress(ntdll, "NtSetSystemInformation"));

        // Best-effort deeper clean: trim global working sets and then purge standby lists
        // when the current token is allowed to request it.
        summary.emptiedSystemWorkingSets = TryMemoryCommand(fn, kMemoryEmptyWorkingSets);
        summary.purgedLowPriorityStandby = TryMemoryCommand(fn, kMemoryPurgeLowPriorityStandbyList);
        summary.purgedStandby = TryMemoryCommand(fn, kMemoryPurgeStandbyList);
    }

    if (loadedLibrary && ntdll) {
        FreeLibrary(ntdll);
    }

    summary.durationMs = GetTickCount() - started;
    return summary;
}

std::wstring BuildCleanStatus(const CleanSummary& summary) {
    SYSTEMTIME st{};
    GetLocalTime(&st);

    wchar_t timeBuffer[16];
    swprintf_s(timeBuffer, L"%02u:%02u:%02u", st.wHour, st.wMinute, st.wSecond);

    std::wstring status = L"Last clean ";
    status += timeBuffer;
    status += L" • ";
    status += std::to_wstring(summary.trimmedProcesses);
    status += L" processes";

    if (summary.purgedStandby) {
        status += L" • standby cache purged";
    } else if (summary.purgedLowPriorityStandby) {
        status += L" • low-priority standby purged";
    } else if (summary.emptiedSystemWorkingSets) {
        status += L" • system working sets trimmed";
    } else {
        status += L" • working sets refreshed";
    }

    return status;
}

void TrackButtonHover(AppState* app) {
    if (app->trackingMouse) {
        return;
    }

    TRACKMOUSEEVENT tme{};
    tme.cbSize = sizeof(tme);
    tme.dwFlags = TME_LEAVE;
    tme.hwndTrack = app->hwnd;
    if (TrackMouseEvent(&tme)) {
        app->trackingMouse = true;
    }
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* app = reinterpret_cast<AppState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (message) {
    case WM_NCCREATE: {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        auto* state = reinterpret_cast<AppState*>(create->lpCreateParams);
        state->hwnd = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
        return TRUE;
    }

    case WM_CREATE:
        if (!app) {
            return -1;
        }
        app->cursorArrow = LoadCursorW(nullptr, IDC_ARROW);
        app->cursorWait = LoadCursorW(nullptr, IDC_WAIT);
        app->darkTheme = LoadDarkThemePreference();
        app->isElevated = IsProcessElevated();
        app->dpi = GetDpiForWindowSafe(hwnd);
        InitializeAppearance(app);
        app->statusLine = BuildIdleStatus(app);
        RecreateFonts(app);
        RecalculateLayout(app);
        UpdateMemoryStats(app);
        InitializeGpuAdapter(app);
        UpdateGpuStats(app);
        LoadAppIcons(app);
        AddTrayIcon(app);
        UpdateTrayIcon(app);
        EnableWindowChrome(hwnd, app->darkTheme);
        StartUpdateCheck(app, false);
        SetTimer(hwnd, kRefreshTimerId, kRefreshIntervalMs, nullptr);
        return 0;

    case WM_DPICHANGED:
        if (app) {
            app->dpi = HIWORD(wParam);
            RecreateFonts(app);
            RecalculateLayout(app);

            const DWORD style = static_cast<DWORD>(GetWindowLongPtrW(hwnd, GWL_STYLE));
            const DWORD exStyle = static_cast<DWORD>(GetWindowLongPtrW(hwnd, GWL_EXSTYLE));
            const SIZE size = GetWindowSizeForDpi(app->dpi, style, exStyle);
            auto* suggested = reinterpret_cast<RECT*>(lParam);
            SetWindowPos(hwnd, nullptr, suggested->left, suggested->top, size.cx, size.cy,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;

    case WM_GETMINMAXINFO:
        if (app) {
            const DWORD style = static_cast<DWORD>(GetWindowLongPtrW(hwnd, GWL_STYLE));
            const DWORD exStyle = static_cast<DWORD>(GetWindowLongPtrW(hwnd, GWL_EXSTYLE));
            const SIZE size = GetWindowSizeForDpi(app->dpi, style, exStyle);
            auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
            info->ptMinTrackSize.x = size.cx;
            info->ptMinTrackSize.y = size.cy;
            info->ptMaxTrackSize.x = size.cx;
            info->ptMaxTrackSize.y = size.cy;
        }
        return 0;

    case WM_SIZE:
        if (app) {
            if (wParam == SIZE_MINIMIZED) {
                MinimizeToTray(app);
                return 0;
            }
            RecalculateLayout(app);
        }
        return 0;

    case WM_TIMER:
        if (app && wParam == kRefreshTimerId) {
            UpdateMemoryStats(app);
            UpdateGpuStats(app);
            UpdateTrayIcon(app);
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;

    case WM_MOUSEMOVE:
        if (app) {
            const POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            const bool buttonHot = PtInRect(&app->buttonRect, pt) != FALSE;
            const bool gpuHot = PtInRect(&app->gpuToggleRect, pt) != FALSE;
            const bool themeHot = PtInRect(&app->themeToggleRect, pt) != FALSE;
            if (buttonHot != app->buttonHot) {
                app->buttonHot = buttonHot;
                InvalidateRect(hwnd, &app->buttonRect, FALSE);
            }
            if (gpuHot != app->gpuButtonHot) {
                app->gpuButtonHot = gpuHot;
                InvalidateRect(hwnd, &app->topBarRect, FALSE);
            }
            if (themeHot != app->themeButtonHot) {
                app->themeButtonHot = themeHot;
                InvalidateRect(hwnd, &app->topBarRect, FALSE);
            }
            if (buttonHot || gpuHot || themeHot) {
                TrackButtonHover(app);
            }
        }
        return 0;

    case WM_MOUSELEAVE:
        if (app) {
            app->trackingMouse = false;
            if (app->buttonHot || app->buttonDown) {
                app->buttonHot = false;
                app->buttonDown = false;
                InvalidateRect(hwnd, &app->buttonRect, FALSE);
            }
            if (app->gpuButtonHot || app->gpuButtonDown) {
                app->gpuButtonHot = false;
                app->gpuButtonDown = false;
                InvalidateRect(hwnd, &app->topBarRect, FALSE);
            }
            if (app->themeButtonHot || app->themeButtonDown) {
                app->themeButtonHot = false;
                app->themeButtonDown = false;
                InvalidateRect(hwnd, &app->topBarRect, FALSE);
            }
        }
        return 0;

    case WM_SETCURSOR:
        if (app && LOWORD(lParam) == HTCLIENT) {
            SetCursor(app->isCleaning && app->cursorWait ? app->cursorWait : app->cursorArrow);
            return TRUE;
        }
        break;

    case WM_LBUTTONDOWN:
        if (app) {
            const POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            if (app->isCleaning) {
                return 0;
            }
            if (PtInRect(&app->gpuToggleRect, pt)) {
                app->gpuButtonDown = true;
                app->themeButtonDown = false;
                SetCapture(hwnd);
                InvalidateRect(hwnd, &app->topBarRect, FALSE);
            } else if (PtInRect(&app->themeToggleRect, pt)) {
                app->themeButtonDown = true;
                app->gpuButtonDown = false;
                SetCapture(hwnd);
                InvalidateRect(hwnd, &app->topBarRect, FALSE);
            } else if (PtInRect(&app->buttonRect, pt)) {
                app->buttonDown = true;
                SetCapture(hwnd);
                InvalidateRect(hwnd, &app->buttonRect, FALSE);
            }
        }
        return 0;

    case WM_LBUTTONUP:
        if (app && GetCapture() == hwnd) {
            ReleaseCapture();
            const POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            const bool activateGpu = app->gpuButtonDown && PtInRect(&app->gpuToggleRect, pt);
            const bool activateTheme = app->themeButtonDown && PtInRect(&app->themeToggleRect, pt);
            const bool activateButton = app->buttonDown && PtInRect(&app->buttonRect, pt);
            app->gpuButtonDown = false;
            app->gpuButtonHot = PtInRect(&app->gpuToggleRect, pt) != FALSE;
            app->themeButtonDown = false;
            app->themeButtonHot = PtInRect(&app->themeToggleRect, pt) != FALSE;
            app->buttonDown = false;
            app->buttonHot = PtInRect(&app->buttonRect, pt) != FALSE;
            InvalidateRect(hwnd, &app->topBarRect, FALSE);
            InvalidateRect(hwnd, &app->buttonRect, FALSE);

            if (activateGpu) {
                app->showGpu = !app->showGpu;
                app->statusLine = app->showGpu
                    ? (app->gpu.available ? L"GPU stats refresh every second." : L"GPU stats unavailable on this system.")
                    : BuildIdleStatus(app);
                UpdateGpuStats(app);
                InvalidateRect(hwnd, nullptr, FALSE);
            } else if (activateTheme) {
                app->darkTheme = !app->darkTheme;
                SaveDarkThemePreference(app->darkTheme);
                EnableWindowChrome(hwnd, app->darkTheme);
                UpdateTrayIcon(app);
                InvalidateRect(hwnd, nullptr, FALSE);
            } else if (activateButton) {
                if (app->showGpu) {
                    app->showGpu = false;
                    app->statusLine = BuildIdleStatus(app);
                } else {
                    app->isCleaning = true;
                    app->statusLine = L"Cleaning memory...";
                    InvalidateRect(hwnd, nullptr, FALSE);
                    UpdateWindow(hwnd);
                    SetCursor(app->cursorWait);
                    const CleanSummary summary = RunMemoryClean();
                    app->isCleaning = false;
                    app->statusLine = BuildCleanStatus(summary);
                    UpdateMemoryStats(app);
                    UpdateTrayIcon(app);
                    SetCursor(app->cursorArrow);
                }
                InvalidateRect(hwnd, nullptr, FALSE);
            }
        }
        return 0;

    case kTrayMessage:
        if (app) {
            const UINT trayEvent = LOWORD(lParam);
            if (trayEvent == WM_LBUTTONUP || trayEvent == WM_LBUTTONDBLCLK || trayEvent == NIN_SELECT || trayEvent == NIN_KEYSELECT) {
                RestoreFromTray(app);
            } else if (trayEvent == WM_RBUTTONUP || trayEvent == WM_CONTEXTMENU) {
                ShowTrayMenu(app);
            }
        }
        return 0;

    case kUpdateCheckCompletedMessage:
        if (app) {
            auto* result = reinterpret_cast<UpdateCheckResult*>(lParam);
            app->update.checking = false;

            if (result) {
                if (result->success && result->updateAvailable) {
                    app->update.available = true;
                    app->update.latestVersion = result->latestVersion;
                    app->update.installerUrl = result->installerUrl;
                    app->update.promptShown = false;
                    app->statusLine = BuildIdleStatus(app);
                    InvalidateRect(hwnd, &app->statusRect, FALSE);
                    PromptForUpdate(app);
                } else if (result->userInitiated) {
                    app->statusLine = result->success
                        ? L"MemTrim Lite is already up to date."
                        : L"Update check failed. Please try again later.";
                    MessageBoxW(hwnd, app->statusLine.c_str(), L"Update check",
                                MB_OK | (result->success ? MB_ICONINFORMATION : MB_ICONWARNING));
                    InvalidateRect(hwnd, &app->statusRect, FALSE);
                }
                delete result;
            }
        }
        return 0;

    case WM_CLOSE:
        if (app && !app->exitRequested) {
            MinimizeToTray(app);
            return 0;
        }
        break;

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT:
        if (app) {
            PAINTSTRUCT ps{};
            HDC dc = BeginPaint(hwnd, &ps);
            PaintWindow(app, dc, ps.rcPaint);
            EndPaint(hwnd, &ps);
            return 0;
        }
        break;

    case WM_DESTROY:
        if (app) {
            KillTimer(hwnd, kRefreshTimerId);
            RemoveTrayIcon(app);
            DeleteFontObject(app->fontSmall);
            DeleteFontObject(app->font);
            DeleteFontObject(app->fontSection);
            DeleteFontObject(app->fontBold);
            DeleteFontObject(app->fontValue);
            ReleaseGpuAdapter(app);
            if (app->iconLarge) {
                DestroyIcon(app->iconLarge);
                app->iconLarge = nullptr;
            }
            if (app->iconSmall) {
                DestroyIcon(app->iconSmall);
                app->iconSmall = nullptr;
            }
        }
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(hwnd, message, wParam, lParam);
}

} // namespace

// Single-file Win32 memory utility.
// Build with MSVC:
//   cl /std:c++17 /O2 /EHsc /DUNICODE /D_UNICODE mem_trim.cpp user32.lib gdi32.lib psapi.lib dwmapi.lib advapi32.lib dxgi.lib gdiplus.lib shell32.lib urlmon.lib /link /SUBSYSTEM:WINDOWS
//
// Build with MinGW g++:
//   g++ -std=c++17 -O2 -s -municode -mwindows mem_trim.cpp -o MemTrimLite.exe -luser32 -lgdi32 -lpsapi -ldwmapi -ladvapi32 -ldxgi -lgdiplus -lshell32 -lurlmon
//
// Optional brand asset:
//   Place mem_trim.ico beside the executable to use the custom app icon.
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
    EnableDpiAwareness();
    if (RelaunchElevatedIfNeeded(showCommand)) {
        return 0;
    }
    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    ULONG_PTR gdiplusToken = 0;
    Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, nullptr);

    AppState app{};
    const std::wstring iconPath = GetAssetPath(L"mem_trim.ico");
    HICON classIconLarge = static_cast<HICON>(LoadImageW(
        nullptr, iconPath.c_str(), IMAGE_ICON,
        GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON),
        LR_LOADFROMFILE));
    HICON classIconSmall = static_cast<HICON>(LoadImageW(
        nullptr, iconPath.c_str(), IMAGE_ICON,
        GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON),
        LR_LOADFROMFILE));

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = classIconLarge ? classIconLarge : LoadIconW(nullptr, IDI_APPLICATION);
    wc.hIconSm = classIconSmall ? classIconSmall : wc.hIcon;
    wc.hbrBackground = nullptr;
    wc.lpszClassName = kWindowClass;

    if (!RegisterClassExW(&wc)) {
        return 0;
    }

    const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    const DWORD exStyle = WS_EX_APPWINDOW;
    const UINT initialDpi = GetSystemDpiSafe();
    const SIZE size = GetWindowSizeForDpi(initialDpi, style, exStyle);

    HWND hwnd = CreateWindowExW(
        exStyle,
        kWindowClass,
        kWindowTitle,
        style,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        size.cx,
        size.cy,
        nullptr,
        nullptr,
        instance,
        &app);

    if (!hwnd) {
        return 0;
    }

    ShowWindow(hwnd, showCommand);
    UpdateWindow(hwnd);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (gdiplusToken) {
        Gdiplus::GdiplusShutdown(gdiplusToken);
    }

    return static_cast<int>(msg.wParam);
}
