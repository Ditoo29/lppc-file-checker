# LPPC File Checker

Small EuroScope plugin (C++) that verifies LPPC `.ese`/`.sct` filenames against the official LPPC package naming rules.

Requirements:
- Visual Studio (MSVC) with the C++ workload — CMake and Ninja are bundled with it
- EuroScope SDK in `%APPDATA%\EuroScope\PlugIn` (`EuroScopePlugIn.h` / `EuroScopePlugInDll.lib`)

Build (production only):
- Open the `lppc-file-checker` folder in Visual Studio.
- Select the `x86-Release` configuration — it is the only one available.
- Build the project.
- The output is `out\build\x86-Release\LPPCFileChecker.dll`.

Notes:
- Always Win32/x86 — EuroScope is a 32-bit application.
- The DLL is built as Release with a statically linked runtime, so it is standalone: no debug runtimes and no VC++ Redistributable needed on the machine running EuroScope.

Install:
- Close EuroScope.
- Copy `out\build\x86-Release\LPPCFileChecker.dll` to `%APPDATA%\EuroScope\LPPC\Plugins\LPPCFileChecker\LPPCFileChecker.dll`.

That's all — the plugin checks LPPC files automatically on EuroScope startup.

## EuroScope commands

| Command | Description |
| --- | --- |
| `.lppcversion` | Checks your LPPC `.ese`/`.sct` files right away. If everything is up to date it shows the current AIRAC/version. If your files are outdated or missing it asks whether you want to update. |

- Also accepted without the dot: `lppcversion` (case-insensitive).
- On startup the same check runs automatically a few seconds after EuroScope loads.
- Package downloads: https://files.aero-nav.com/LPPC

## Automatic update flow

When the check finds outdated or missing files:

1. A popup offers to open the download page — choosing **Yes** opens it in your browser.
2. Log in with your VATSIM/Navigraph account and download the **LPPC Update-Package** as **`.zip` or `.7z`** into your **Downloads** folder. The popup shows the exact expected file name (e.g. `LPPC-Update-Package_20260903120704-260901-0001.zip`).
3. Return to EuroScope and click **OK**. The plugin looks for the archive in the Downloads folder and validates its contents. If it is not there (or still downloading), an error with the expected file name is shown and you are asked again — click OK to re-check or Cancel to abort.
4. When the archive is ready, a final popup asks to close EuroScope. On OK, an update wizard embedded in the plugin takes over: it closes EuroScope, extracts the archive into the EuroScope instance the plugin is loaded from (the same folder the checker verifies — not necessarily `%APPDATA%`), overwriting existing files and the `LPPC` folder without deleting anything, verifies the result, and restarts EuroScope.
5. EuroScope must be closed during installation because the package also contains the running plugin DLLs (including `LPPCFileChecker.dll` itself) — this is handled automatically by the wizard.

## Update log

For troubleshooting, both the plugin and the update wizard write a step-by-step log to:

`%TEMP%\LPPCFileChecker_update.log` — e.g. `C:\Users\<you>\AppData\Local\Temp\LPPCFileChecker_update.log`

- Lines starting with `[plugin]` come from the plugin inside EuroScope (wizard launch, handover file).
- Lines starting with `wizard:` come from the update wizard (closing EuroScope, extraction, verification, restart).
- Entries are appended on every update run, so the file grows over time and can be deleted at any time.