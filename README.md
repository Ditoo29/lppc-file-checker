# LPPC File Checker

Small EuroScope plugin (C++) that verifies LPPC `.ese`/`.sct` filenames against the official LPPC package naming rules.

Requirements:
- Visual Studio (MSVC) with the C++ workload or another MSVC toolchain
- CMake

Quick build:
- Open the `lppc-file-checker` folder in Visual Studio.
- Select the `Win32` platform.
- Build the project.

Notes:
- Build for `Win32` (plugins must match EuroScope bitness).
- The project looks for the EuroScope SDK in `%APPDATA%\EuroScope\PlugIn` by default.

Install:
- Copy the built `LPPCFileChecker.dll` to `%APPDATA%\EuroScope\LPPC\Plugins\LPPCFileChecker\LPPCFileChecker.dll`.
- The plugin checks the `.ese` and `.sct` files in `%APPDATA%\EuroScope\`.

That's all — the plugin checks LPPC files automatically on EuroScope startup.