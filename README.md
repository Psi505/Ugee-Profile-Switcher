# Ugee-Profile-Switcher

Switches UGEE tablet configuration profiles using global hotkeys. Runs as a hidden window with no tray icon and no console.

---

## Why

The UGEE S640 driver stores a single config at `%APPDATA%\Roaming\ugeeTablet\Ugee_Tablet.xml` and only reads it at startup. If you work across multiple monitor setups you have to manually swap settings every time through the driver UI. This tool automates that.

---

## How

I reversed `ugeeTablet.exe` (v4.3.2.0) in Ghidra first. Config loading lives in `LoadConfig` at `0x00442520`, targeting the XML above. Both IPC channels (QLocalServer and the driver socket) were checked for a "reload" or "switch" command. Neither has one. The import/export buttons in the UI fire directly off Qt signals with no IPC binding. There is no way to tell a running instance to reload its config.

That left the blunt approach:

1. Copy the desired profile XML over the live config file
2. Kill both `ugeeTablet.exe` and `ugeeTabletDriver.exe`
3. Restart them

The program registers `Ctrl+Win+Alt+1` and `Ctrl+Win+Alt+2` as global hotkeys. On each hotkey it swaps the file, kills the processes, and relaunches them. `ugeeTablet.exe` likes to flash a splash window on startup so the tool enumerates and hides its windows after launch.

A service was ruled out because session 0 can't launch GUI apps in the user's desktop. A tray icon was skipped because there is nothing to show or configure.

| Hotkey | Profile  | File                                    |
|--------|----------|-----------------------------------------|
| `1`    | Laptop   | `Laptop_Screen__Ugee_Tablet_DUMMY.xml`  |
| `2`    | Monitor  | `Monitor_Screen__Ugee_Tablet_DUMMY.xml` |

---

## Project Layout

```
Ugee-Profile-Switcher/
├── profiles/
│   ├── Laptop_Screen__Ugee_Tablet_DUMMY.xml
│   └── Monitor_Screen__Ugee_Tablet_DUMMY.xml
├── res/
│   ├── ugee-profile-switcher.rc
│   ├── ugee-profile-switcher.res
│   └── IDI_ICON1.ico
├── src/
│   └── ugee-profile-switcher.c
├── ugee-profile-switcher.exe
└── README.md
```

The `profiles/` directory must live next to the EXE. Paths are resolved at runtime relative to the executable location.

> The XML files in this repo are dummy placeholders. Swap them with your actual UGEE config exports before using the tool.

---

## Compile

MinGW-w64 (tested with [w64devkit](https://github.com/skeeto/w64devkit)):

```powershell
windres -O coff res\ugee-profile-switcher.rc -o res\ugee-profile-switcher.res
gcc -O2 -mwindows -o ugee-profile-switcher.exe src\ugee-profile-switcher.c res\ugee-profile-switcher.res -luser32 -lgdi32
```

---

## Adding a Profile

1. Export your config from the UGEE driver. The `.ugeecfg` files it saves are plain XML; rename to `.xml`.
2. Place it as `Name__Ugee_Tablet.xml` in the `profiles/` directory.
3. Add an entry to the `profiles` array in `src\ugee-profile-switcher.c`:
   ```c
   { '4', "MyProfile", "MyProfile__Ugee_Tablet.xml" },
   ```
4. Bump `NUM_PROFILES`.
5. Recompile.

---

## Auto-Start

Drop a shortcut to `ugee-profile-switcher.exe` into `shell:startup` (`%APPDATA%\Microsoft\Windows\Start Menu\Programs\Startup`).

---

## Notes

- The icon was pulled from `ugeeTablet.exe` via `ExtractAssociatedIcon`.
- If hotkey registration fails, try running as administrator once. Some group policies block global hotkeys otherwise.
- The binary must run in a user session, not as a system service.
