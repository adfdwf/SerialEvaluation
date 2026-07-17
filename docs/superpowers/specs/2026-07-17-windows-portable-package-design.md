# Windows Portable Package Design

## Goal

Create a Windows x64 portable ZIP package for CommBenchPro that runs on Windows 10/11 computers without Qt, CMake, or the project source tree installed.

## Scope

- Build the existing project with the `Qt-Release` CMake preset.
- Preserve all current source changes, including the existing working-tree change in `src/tcpclientworker.cpp`.
- Use Qt's `windeployqt.exe` to collect Qt Core, GUI, Widgets, Network, SerialPort, platform, and other required plugins.
- Include the required MSVC x64 runtime handling in the package or document the runtime prerequisite.
- Produce a self-contained package directory and a ZIP archive under a dedicated local packaging directory.
- Do not alter application behavior or push any branch to a remote repository.

## Package Layout

```text
CommBenchPro-Windows-x64/
├─ CommBenchPro.exe
├─ Qt6Core.dll
├─ Qt6Gui.dll
├─ Qt6Widgets.dll
├─ Qt6Network.dll
├─ Qt6SerialPort.dll
├─ platforms/
│  └─ qwindows.dll
├─ Log/
└─ README.txt
```

The package must be tested from its own directory, not from the build tree. Existing runtime logs and build artifacts must not be copied into the release package.

## Verification

- Release configuration builds successfully.
- `windeployqt` completes successfully against the Release executable.
- The package contains the executable, Qt DLLs, and the Windows platform plugin.
- The package is free of Debug DLLs and source/build files.
- The ZIP can be extracted to a separate directory and the executable can start.
- TCP and serial functionality require a real clean-machine test; this local packaging task can verify file completeness and startup only.

