# Cereal Monitor

A native Windows C++ serial monitor project built with Visual Studio.

## Current project status

This repository contains the Visual Studio project and its current Win32 serial-monitor implementation.
It is an in-progress project, not yet a verified production release. In particular, the UI still uses
some native Windows controls, and the serial features should be tested with real USB-to-serial hardware.

## Build in Visual Studio

1. Open `Cereal Monitor.vcxproj` in Visual Studio 2022 or newer with the **Desktop development with C++** workload.
2. Select **Release** and **x64**.
3. Select **Build > Clean Solution**.
4. Select **Build > Rebuild Solution**.
5. The executable should be created under `x64/Release/`.

The project uses the Windows API and the C++ standard library. No third-party runtime library is intentionally required.

## Portable build

After a successful Release x64 build, run `scripts/make-portable.ps1` from the repository root.
It copies the executable and README into `dist/CerealMonitor-Portable/`.

## Installer

The `installer/CerealMonitor.iss` script is for Inno Setup 6. Build the Release x64 executable first,
then open the script in Inno Setup Compiler and compile it. The installer output is written to
`installer/Output/`.

## USB serial notes

- Close other serial-terminal programs before connecting to the same COM port.
- Select the baud rate that matches the device firmware.
- Windows may require the USB-to-serial driver for the device (for example, a CH340, CP210x, or FTDI driver).
- Serial communication has not been hardware-tested as part of this source package.

## Contributing

Issues and pull requests are welcome. Please include your Windows version, Visual Studio version,
device/USB-serial chipset, baud rate, and build output when reporting a problem.
