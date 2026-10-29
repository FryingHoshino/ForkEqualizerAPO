<div align="center">

<img src="https://capsule-render.vercel.app/api?type=waving&color=0:1a1f2b,100:3b82f6&height=180&section=header&text=EqualizerAPO%20Fork&fontSize=48&fontColor=ffffff&fontAlignY=40&desc=Small%20features%20the%20official%20release%20doesn%27t%20have&descSize=16&descAlignY=62" alt="EqualizerAPO Fork" />

[![License: GPL v2](https://img.shields.io/badge/License-GPL%20v2-blue.svg?style=for-the-badge)](License.txt)
![Platform](https://img.shields.io/badge/Platform-Windows-0078D6?style=for-the-badge&logo=windows&logoColor=white)
![Based on](https://img.shields.io/badge/Based%20on-Equalizer%20APO%201.4.2-orange?style=for-the-badge)

[**Original Project**](https://sourceforge.net/projects/equalizerapo/) · [**Official Website**](https://equalizerapo.com) · [**Releases**](https://github.com/FryingHoshino/ForkEqualizerAPO/releases)

</div>

---

## About

A fork of [Equalizer APO](https://sourceforge.net/projects/equalizerapo/), a parametric/graphic equalizer for Windows. This fork adds a few small features the official release doesn't have. The feature list may change over time.

## Preview

<div align="center">
<img width="100%" alt="Configuration Editor screenshot" src="https://github.com/user-attachments/assets/41259bab-e5fe-4948-9a4a-8f52d0748cb0" />
</div>

## Building

Tested with:

- Visual Studio 2026 (MSVC 14.51, platform toolset `v145`). The project files were retargeted from `v143`, so VS 2022 users need to retarget.
- Qt 6.7.3 (MSVC 2022 64-bit) plus the Qt Visual Studio Tools extension
- NSIS (for the installer)
- TCLAP 1.6.0 (only for `Benchmark`)

Steps:

1. Build `Common`, `EqualizerAPO`, `VoicemeeterClient` and `Benchmark` with `msbuild EqualizerAPO.sln /p:Configuration=Release /p:Platform=x64`.
2. For `DeviceSelector` and `UpdateChecker`, add `/p:QtInstall=C:\Qt\6.7.3\msvc2022_64 /p:SolutionDir=<repo path>\`.
3. Build the Editor: `qmake Editor\Editor.pro -spec win32-msvc "CONFIG+=release"`, then `nmake release`.
4. Copy the Qt 6.7.3 and VC runtime DLLs into `Setup\lib64`.
5. Run `makensis Setup64.nsi` inside `Setup`. (`Setup.nsi` is saved as UTF-8 with BOM, since newer NSIS rejects the original Windows-1252 encoding.)
## Credits

- Original software by **Jonas Thedering** and the Equalizer APO contributors
- Preview artwork by [Rune Xiao](https://www.pixiv.net/en/users/44473246)

## License

Released under the **GNU GPL v2**, the same as the original project. See [License.txt](License.txt).
