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
#define MAX_PATH_LEN 520  // 2 * MAX_PATH to support long-path-enabled systems
#define HOTKEY_BASE 1     // Arbitrary ID for the first RegisterHotKey

typedef struct {
    UINT vkey;       // Virtual key code ('1', '2', etc.)
    LPCSTR name;     // Display name for the profile
    LPCSTR filename; // XML file inside profiles/
} Profile;

static const Profile profiles[NUM_PROFILES] = {
    { '1', "Laptop",  "Laptop_Screen__Ugee_Tablet_DUMMY.xml" },
    { '2', "Monitor", "Monitor_Screen__Ugee_Tablet_DUMMY.xml" },
};

// Paths resolved once at startup
static CHAR g_profilesDir[MAX_PATH_LEN];
static CHAR g_roamingConfig[MAX_PATH_LEN];
static CHAR g_programDir[MAX_PATH_LEN];
static HINSTANCE g_hInst;

// Strips the EXE name from the full path, keeps only the directory
static BOOL BuildPaths(VOID) {
    CHAR exePath[MAX_PATH_LEN];
    if (!GetModuleFileNameA(g_hInst, exePath, sizeof(exePath)))
        return FALSE;

    LPSTR last = strrchr(exePath, '\\');
    if (!last) return FALSE;
    *last = '\0';  // Keep exeDir only

    snprintf(g_profilesDir, sizeof(g_profilesDir), "%s\\profiles", exePath);
    snprintf(g_programDir, sizeof(g_programDir), "C:\\Program Files\\ugeeTablet");

    LPCSTR appdata = getenv("APPDATA");
    if (!appdata) return FALSE;

    snprintf(g_roamingConfig, sizeof(g_roamingConfig),
             "%s\\ugeeTablet\\Ugee_Tablet.xml", appdata);
    return TRUE;
}

// Ctrl+Win+Alt+<digit> for each profile
static BOOL RegisterHotkeys(VOID) {
    for (int i = 0; i < NUM_PROFILES; i++) {
        if (!RegisterHotKey(NULL, HOTKEY_BASE + i,
                            MOD_CONTROL | MOD_WIN | MOD_ALT,
                            profiles[i].vkey)) {
            return FALSE;
        }
    }
    return TRUE;
}

static VOID UnregisterHotkeys(VOID) {
    for (int i = 0; i < NUM_PROFILES; i++)
        UnregisterHotKey(NULL, HOTKEY_BASE + i);
}

// Runs a command and blocks until it finishes
static VOID ExecAndWait(LPCSTR cmd) {
    STARTUPINFOA si = { sizeof(si) };
    PROCESS_INFORMATION pi;
    // CREATE_NO_WINDOW keeps taskkill from flashing a console
    if (CreateProcessA(NULL, (LPSTR)cmd, NULL, NULL, FALSE,
                       CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        WaitForSingleObject(pi.hProcess, INFINITE);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
}

// Force-kills a process by image name so the config file is released
static BOOL KillProcess(LPCSTR name) {
    CHAR cmd[256];
    snprintf(cmd, sizeof(cmd), "taskkill /f /im %s", name);
    ExecAndWait(cmd);
    return TRUE;
}

// EnumWindows callback: hides every window belonging to the given PID
static BOOL CALLBACK HideEnumProc(HWND hwnd, LPARAM lParam) {
    DWORD pid;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == (DWORD)lParam)
        ShowWindow(hwnd, SW_HIDE);  // Driver ignores STARTF_USESHOWWINDOW
    return TRUE;
}

// Starts a process with its main window hidden and returns its PID
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

// Core flow: copy XML -> kill processes -> restart -> hide windows
static VOID SwitchProfile(INT index) {
    CHAR src[MAX_PATH_LEN];
    snprintf(src, sizeof(src), "%s\\%s",
             g_profilesDir, profiles[index].filename);

    // 1. Overwrite the live config the driver reads at startup
    if (!CopyFileA(src, g_roamingConfig, FALSE)) {
        CHAR msg[256];
        snprintf(msg, sizeof(msg), "Failed to copy config: %lu", GetLastError());
        MessageBoxA(NULL, msg, "Error", MB_OK | MB_ICONERROR);
        return;
    }

    // 2. Kill both processes so they release the old config
    KillProcess("ugeeTablet.exe");
    KillProcess("ugeeTabletDriver.exe");

    Sleep(500);  // Wait for processes to fully exit

    // 3. Restart both; they will read the new XML
    DWORD pid = StartProcessGetPid("ugeeTablet.exe");
    StartProcessGetPid("ugeeTabletDriver.exe");

    // 4. Hide the splash window that ugeeTablet.exe spawns
    Sleep(1000);
    if (pid)
        EnumWindows(HideEnumProc, (LPARAM)pid);
}

// Minimal window procedure: only handles WM_DESTROY to quit
LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_DESTROY)
        PostQuitMessage(0);
    return DefWindowProcA(hWnd, msg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                   LPSTR lpCmdLine, int nCmdShow) {
    g_hInst = hInstance;

    // Resolve all file paths before entering the message loop
    if (!BuildPaths()) {
        MessageBoxA(NULL, "Failed to build paths", "Error", MB_OK | MB_ICONERROR);
        return 1;
    }

    // Create a hidden window to receive WM_HOTKEY messages
    // Windows requires a window with a message loop for global hotkeys
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
        MessageBoxA(NULL, "Failed to register hotkeys.\nTry running as administrator.",
                    "Error", MB_OK | MB_ICONERROR);
        return 1;
    }

    // Message loop: dispatch WM_HOTKEY to SwitchProfile,
    // Everything else goes to the default window procedure
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
