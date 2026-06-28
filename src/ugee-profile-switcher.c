// Ugee-Profile-Switcher
// Switches UGEE tablet config profiles via Ctrl+Win+Alt+1/2
//
// Build:
//   windres -O coff res\ugee-profile-switcher.rc -o res\ugee-profile-switcher.res
//   gcc -O2 -mwindows -o ugee-profile-switcher.exe src\ugee-profile-switcher.c ^
//     res\ugee-profile-switcher.res -luser32 -lgdi32

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <stdio.h>

#define NUM_PROFILES 2
#define MAX_PATH_LEN 520
#define HOTKEY_BASE 1

typedef struct {
    UINT vkey;
    LPCSTR name;
    LPCSTR filename;
} Profile;

static const Profile profiles[NUM_PROFILES] = {
    { '1', "Laptop",  "Laptop_Screen__Ugee_Tablet_DUMMY.xml" },
    { '2', "Monitor", "Monitor_Screen__Ugee_Tablet_DUMMY.xml" },
};

static CHAR g_profilesDir[MAX_PATH_LEN];
static CHAR g_roamingConfig[MAX_PATH_LEN];
static CHAR g_programDir[MAX_PATH_LEN];
static HINSTANCE g_hInst;

static BOOL BuildPaths(VOID) {
    CHAR exePath[MAX_PATH_LEN];
    if (!GetModuleFileNameA(g_hInst, exePath, sizeof(exePath)))
        return FALSE;

    LPSTR last = strrchr(exePath, '\\');
    if (!last) return FALSE;
    *last = '\0';

    snprintf(g_profilesDir, sizeof(g_profilesDir), "%s\\profiles", exePath);

    snprintf(g_programDir, sizeof(g_programDir), "C:\\Program Files\\ugeeTablet");

    LPCSTR appdata = getenv("APPDATA");
    if (!appdata) return FALSE;

    snprintf(g_roamingConfig, sizeof(g_roamingConfig), "%s\\ugeeTablet\\Ugee_Tablet.xml", appdata);
    return TRUE;
}

static BOOL RegisterHotkeys(VOID) {
    for (int i = 0; i < NUM_PROFILES; i++) {
        if (!RegisterHotKey(NULL, HOTKEY_BASE + i, MOD_CONTROL | MOD_WIN | MOD_ALT, profiles[i].vkey)) {
            return FALSE;
        }
    }
    return TRUE;
}

static VOID UnregisterHotkeys(VOID) {
    for (int i = 0; i < NUM_PROFILES; i++)
        UnregisterHotKey(NULL, HOTKEY_BASE + i);
}

static VOID ExecAndWait(LPCSTR cmd) {
    STARTUPINFOA si = { sizeof(si) };
    PROCESS_INFORMATION pi;
    if (CreateProcessA(NULL, (LPSTR)cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        WaitForSingleObject(pi.hProcess, INFINITE);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
}

static BOOL KillProcess(LPCSTR name) {
    CHAR cmd[256];
    snprintf(cmd, sizeof(cmd), "taskkill /f /im %s", name);
    ExecAndWait(cmd);
    return TRUE;
}

static BOOL CALLBACK HideEnumProc(HWND hwnd, LPARAM lParam) {
    DWORD pid;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == (DWORD)lParam)
        ShowWindow(hwnd, SW_HIDE);
    return TRUE;
}

static DWORD StartProcessGetPid(LPCSTR name) {
    CHAR path[MAX_PATH_LEN];
    snprintf(path, sizeof(path), "%s\\%s", g_programDir, name);
    STARTUPINFOA si = { sizeof(si) };
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi;
    if (!CreateProcessA(NULL, path, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi))
        return 0;
    DWORD pid = pi.dwProcessId;
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return pid;
}

static VOID SwitchProfile(INT index) {
    CHAR src[MAX_PATH_LEN];
    snprintf(src, sizeof(src), "%s\\%s", g_profilesDir, profiles[index].filename);

    if (!CopyFileA(src, g_roamingConfig, FALSE)) {
        CHAR msg[256];
        snprintf(msg, sizeof(msg), "Failed to copy config: %lu", GetLastError());
        MessageBoxA(NULL, msg, "Error", MB_OK | MB_ICONERROR);
        return;
    }

    KillProcess("ugeeTablet.exe");
    KillProcess("ugeeTabletDriver.exe");

    Sleep(500);

    DWORD pid = StartProcessGetPid("ugeeTablet.exe");
    StartProcessGetPid("ugeeTabletDriver.exe");

    Sleep(1000);
    if (pid)
        EnumWindows(HideEnumProc, (LPARAM)pid);
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_DESTROY)
        PostQuitMessage(0);
    return DefWindowProcA(hWnd, msg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    g_hInst = hInstance;

    if (!BuildPaths()) {
        MessageBoxA(NULL, "Failed to build paths", "Error", MB_OK | MB_ICONERROR);
        return 1;
    }

    WNDCLASSA wc = { 0 };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = "UgeeSwitcherClass";

    if (!RegisterClassA(&wc)) {
        MessageBoxA(NULL, "Failed to register window class", "Error", MB_OK | MB_ICONERROR);
        return 1;
    }

    HWND hWnd = CreateWindowExA(0, wc.lpszClassName, "Ugee Profile Switcher",
                                0, 0, 0, 0, 0, NULL, NULL, hInstance, NULL);
    if (!hWnd) {
        MessageBoxA(NULL, "Failed to create window", "Error", MB_OK | MB_ICONERROR);
        return 1;
    }

    if (!RegisterHotkeys()) {
        MessageBoxA(NULL, "Failed to register hotkeys.\nTry running as administrator.", "Error", MB_OK | MB_ICONERROR);
        return 1;
    }

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        if (msg.message == WM_HOTKEY) {
            INT id = (INT)msg.wParam - HOTKEY_BASE;
            if (id >= 0 && id < NUM_PROFILES)
                SwitchProfile(id);
        } else {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    UnregisterHotkeys();
    return 0;
}
