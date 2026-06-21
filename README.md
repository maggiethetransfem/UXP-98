# UXP / New Moon for Windows 98 + KernelEX

This tree is a Windows 98 + KernelEX source tree.

The goal of this fork is to keep the browser buildable with the old UXP/MSVC
toolchain while avoiding runtime imports and code paths that break on Windows
98 with KernelEx.

## What has been done:

- Avoided `api-ms-*` dependencies.
- Avoided direct dependencies on newer kernel APIs.
- Keep imports roughly within Windows 2000/XP RTM/SP1.

## Known local build setup

The tested build environment used:

- MozillaBuild
- Visual Studio 2015 / MSVC 14.0
- Windows SDK 10.0.14393.0
- 32-bit target: `i686-pc-mingw32`
- 64-bit host tools: `x86_64-pc-mingw32`

See `.mozconfig.example` for the build configuration used by this tree.

## Important helper files

- `tools/stage-win98-runtime.ps1` stages a test runtime folder after build.
- `tools/win98-font-safe-prefs.js` contains font/rendering safety prefs.
- `tools/discord-cert-prefs.js` contains certificate/network prefs used while testing.
- `tools/discord-compat@uxp-kernelex/` is a bootstrap extension with compatibility shims for current Discord web.

## DISCLAIMER:

THIS IS A PERSONAL PROJECT, THIS WAS VIBECODED USING CODEX AND I DON'T OWN ANY PART OF THIS CODE, ALL CREDIT GOES TO ROYTAM1 AND THE Unified XUL Platform DEVELOPERS


# How to open the program in Windows 98:

After you copy all the files from the source you compiled, copy the folder inside the (root of the source)/(obj name you compiled)/dist/bin/ to your Windows 98 Machine/VM,
then make sure you have the latest version of KernelEX installed, and set compatibility mode to Windows XP SP3/SP2 for both XUL.DLL and palemoon.exe, then just double click the program.

I recommend you install the latest root certificates to not have to deal with certificate problems

# Compilation process

Requirements

This fork was tested with:

- Windows 10 as the build host
- MozillaBuild
- Visual Studio 2015 / MSVC 14.0
- Windows SDK `10.0.14393.0`
- 32-bit target build
- UXP source checked out on a path without spaces

The tested local layout used:

C:\mozilla-build
X:\VS2015
X:\WinKits

If your Visual Studio or SDK paths are different, edit .mozconfig before building.

1. Prepare .mozconfig

Copy the example .mozconfig.example to .mozconfig

Then edit .mozconfig and adjust these paths if needed:

VS="/x/VS2015"
SDK="/x/WinKits"
SDKVER="10.0.14393.0"

The important build options are:
ac_add_options --enable-application=palemoon
ac_add_options --enable-win9x-vxkex
ac_add_options --target=i686-pc-mingw32
ac_add_options --host=x86_64-pc-mingw32

2. Open MozillaBuild shell

Run:
C:\mozilla-build\start-shell.bat

3. Enter the source tree

From the MozillaBuild shell:

cd /path-to-UXP-98/

4. Build

Run:

./mach build
If you need to force the known-good tool paths manually, use:

export PATH=/path-to-VS2015/VC/bin/amd64_x86:/path-to-VS2015/VC/bin/amd64:/path-to-VS2015/Common7/IDE:/path-to-WinKits/bin/x86:/path-to-WinKits/bin/x64:$PATH

./mach build

Common7/IDE is important because MSVC 2015 needs mspdb140.dll.

5. Build output

The browser output should be created under:

obj-*\dist\bin

Troubleshooting

If configure complains about GetShortPathName or 8.3 paths, move the source/toolchain to shorter paths such as:

C:\UXP
C:\VS2015
C:\WinKits

If configure says the host compiler CPU does not match the host CPU, make sure host tools use the 64-bit compiler:

HOST_CC="$VS/VC/bin/amd64/cl.exe"
HOST_CXX="$VS/VC/bin/amd64/cl.exe"

If ml.exe or assembler checks fail, make sure this is present in .mozconfig:

export AS="$VS/VC/bin/amd64_x86/ml.exe"
export CCAS="$VS/VC/bin/amd64_x86/ml.exe"

If mspdb140.dll is missing, make sure this is in PATH:

$VS/Common7/IDE
