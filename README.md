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
| `.lppcversion` | Checks your LPPC `.ese`/`.sct` files right away. Shows a popup with the current AIRAC/version if everything is up to date, or a warning with the download link if your files are outdated or missing. |

- Also accepted without the dot: `lppcversion` (case-insensitive).
- On startup the same check runs automatically a few seconds after EuroScope loads.
- Package downloads: https://files.aero-nav.com/LPPC