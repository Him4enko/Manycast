<div align="center">

# ManyCast

**One OBS program - several RTMP platforms at once - plus a second, vertical canvas for mobile.**
No restreaming service, no extra encoding when the destinations can share the main stream.

[![Build](https://img.shields.io/github/actions/workflow/status/Him4enko/Manycast/build.yaml?branch=main&label=build&style=flat-square&logo=github)](https://github.com/Him4enko/Manycast/actions/workflows/build.yaml)
[![Release](https://img.shields.io/github/v/release/Him4enko/Manycast?style=flat-square&logo=github)](https://github.com/Him4enko/Manycast/releases)
[![License](https://img.shields.io/badge/license-GPL--2.0--or--later-blue?style=flat-square&logo=gnu&logoColor=white)](LICENSE)
[![Localizations](https://img.shields.io/badge/localizations-7-6E4C13?style=flat-square&logo=googletranslate&logoColor=white)](data/locale)

[![OBS](https://img.shields.io/badge/OBS-30.x%20--%2032.x-302E31?style=flat-square&logo=obsstudio&logoColor=white)](#-requirements)
[![Windows](https://img.shields.io/badge/Windows-x64-0078D6?style=flat-square&logo=windows11&logoColor=white)](#-requirements)
[![C++](https://img.shields.io/badge/C%2B%2B-20-00599C?style=flat-square&logo=cplusplus&logoColor=white)](#-requirements)
[![Qt](https://img.shields.io/badge/Qt-6-41CD52?style=flat-square&logo=qt&logoColor=white)](#-requirements)
[![CMake](https://img.shields.io/badge/CMake-3.28%2B-064F8C?style=flat-square&logo=cmake&logoColor=white)](#-requirements)

**English** | [Русский](README.ru.md)

</div>

---

## ✨ Features

| | |
|---|---|
| **Several destinations at once** | Any number of RTMP targets (Twitch, YouTube, TikTok, ...) stream simultaneously from one OBS instance. |
| **No restreaming service** | Every destination is an ordinary output with its own stream key - nothing leaves your machine except to the platforms themselves. |
| **Zero extra encoding** | By default the destinations reuse the encoder of the main stream: one encoded picture is muxed and uploaded several times. |
| **Dedicated encoders on demand** | Any destination can instead use its own encoder with its own bitrate, resolution, frame rate and audio bitrate. |
| **Vertical canvas** | A second canvas (1080×1920 by default) with its own scene, live preview and mouse editing - for TikTok, YouTube Shorts and any other 9:16 target. |
| **Independent layout** | Sources are shared with the horizontal scene, but their position, scale and crop in the portrait canvas are independent: move the webcam for mobile without touching the desktop layout. |
| **Shared portrait encoder** | Portrait destinations with identical settings share one encoder instead of one per platform. |
| **Per-profile settings** | Destinations and stream keys are stored per OBS profile; the canvas size and the portrait scene list per plugin. |
| **7 languages** | English, Russian, German, French, Italian, Spanish and Simplified Chinese. |
| **Tested** | Unit tests (doctest) for the OBS independent code, a locale consistency check and a smoke test of the built DLL. |

## 📦 Requirements

| Component | Version |
|---|---|
| OBS Studio | 30.x / 31.x / 32.x (developed and tested against 32.2.2) |
| Plugin API | built against libobs 31.1.1 headers |
| Operating system | Windows 10/11, x64 (the portrait preview needs a native window handle) |
| Compiler | MSVC - Visual Studio 2022 or the Build Tools |
| Language / UI | C++20, Qt 6 Widgets |
| Build system | CMake 3.28+ |

## 🚀 Installation

1. Download `manycast-windows-x64.zip` from the [latest release](https://github.com/Him4enko/Manycast/releases).
2. Extract the `manycast` folder into `%ProgramData%\obs-studio\plugins` so that the result is
   `%ProgramData%\obs-studio\plugins\manycast\bin\64bit\manycast.dll`.
3. Restart OBS and enable the docks in `View → Docks → ManyCast` and `View → Docks → ManyCast Portrait`.

The load can be confirmed in `Help → Log Files → View Current Log`:
`[manycast] ManyCast loaded (version 0.1.0)`.

<details>
<summary>Portable installation (next to OBS itself)</summary>

```powershell
$obs = "{your-path-to-obs}\OBS Studio"
Copy-Item "<extracted>\manycast\bin\64bit\manycast.dll" "$obs\obs-plugins\64bit\" -Force
Copy-Item "<extracted>\manycast\data\locale\*.ini" "$obs\data\obs-plugins\manycast\locale\" -Force
```
Remove the `%ProgramData%` copy so that OBS does not load the plugin twice.

</details>

## 🔨 Building

The project is based on the [obs-plugin-template](https://github.com/obsproject/obs-plugintemplate):
CMake downloads the OBS sources, the prebuilt OBS dependencies and Qt6 into `.deps` and builds
`obs-frontend-api` for you, so the first configure needs an internet connection and takes a while.

```powershell
# run from the repository root
$cmake = "{your_path}\cmake.exe"

& $cmake --preset windows-x64
& $cmake --build --preset windows-x64 --config RelWithDebInfo
& $cmake --install build_x64 --config RelWithDebInfo --prefix "$env:ProgramData\obs-studio\plugins"
```

In CLion just open the folder and pick the `windows-x64` (Visual Studio) or `windows-x64-ninja`
(Ninja + the CLion MSVC toolchain) preset. The same steps are executed on every push by
[`.github/workflows/build.yaml`](.github/workflows/build.yaml), which also attaches a ready to
install zip as an artifact.

## 🎛 Usage

### Destinations dock (`View → Docks → ManyCast`)

1. **Add destination** and fill in the dialog:
   * **Mode → OBS service** - pick a platform and a server from the same list OBS itself uses, then
     paste the stream key;
   * **Mode → Custom RTMP server** - enter the RTMP URL and the stream key.
   * **Frame** - `Main (16:9)` for the normal picture, `Portrait (9:16)` for the vertical canvas.
   * **Encoder** - reuse the main stream encoder (default, no extra load) or configure a dedicated
     one. Empty fields inherit the settings of the main stream and show the inherited value.
   * **Stream** - start/stop together with the main stream, delay, reconnection attempts.
2. Press **Start** on a destination or **Start all**.

The row of a destination shows its state, uptime, upstream bitrate, fps and dropped frames; the
toolbar summarises how many destinations are live and the total upstream.

### Portrait dock (`View → Docks → ManyCast Portrait`)

1. Press **From current scene** to duplicate the horizontal scene (sources are shared, transforms
   are independent) or build the vertical layout from scratch with **Add source**.
2. Arrange it in the preview: drag to move (with snapping to the edges and the centre of the
   frame), the corner handle to scale, `Ctrl`+wheel to resize around the centre, double click for
   the source properties. **Fit**, **Fill** and **Center** help with the first placement.
3. Use the source list to reorder, hide, lock or remove items; **Canvas size** switches between
   1080×1920, 720×1280, 1080×1080 and 1920×1080 (portrait streams have to be stopped first).

## 📱 Streaming the portrait frame

| Platform | How to deliver the portrait feed |
|---|---|
| TikTok LIVE, Instagram, ... | one destination, `Frame: Portrait (9:16)`, the platform's stream key |
| YouTube (horizontal + vertical) | enable **Dual stream** in the Live Control Room, then add a destination with the **second** key and `Frame: Portrait (9:16)`; the primary key stays in OBS's own streaming settings |
| Twitch (Dual Format) | OBS sends both feeds inside a single Enhanced Broadcasting session: enable `Settings → Stream → Multitrack Video → Enhanced Broadcasting` and pick `ManyCast Portrait` as the **Additional Canvas**. Do not add a separate Twitch destination for the portrait feed - Twitch would treat it as a second, independent stream |

> **One key = one stream.** Two feeds must never be sent to the same stream key (the second
> connection drops the first), so the portrait feed needs its own key/stream on the platform.

## 🌍 Localization

`data/locale` contains English, Russian, German, French, Italian, Spanish and Simplified Chinese.
To add a language, copy `en-US.ini`, translate the values, keep the `%1`/`%2` placeholders and keep
every value in quotes - OBS truncates unquoted values at the first space. The consistency of all
files is verified by `tests/check-locales.ps1` (also run in CI).

## ✅ Testing

```powershell
# run from the repository root
$cmake = "{your_path}\cmake.exe"

# unit tests (doctest) for the code without OBS dependencies
& $cmake --build --preset windows-x64 --config RelWithDebInfo --target manycast-tests
& ".\build_x64\tests\RelWithDebInfo\manycast-tests.exe"
& "{your_path}\ctest.exe" --test-dir build_x64 -C RelWithDebInfo --output-on-failure

# localization consistency (keys, quotes, placeholders)
powershell -ExecutionPolicy Bypass -File tests\check-locales.ps1

# the built dll loads, resolves its imports and exports the OBS module API
powershell -ExecutionPolicy Bypass -File tests\smoke-plugin.ps1
```

Everything that talks to the OBS C API (docks, outputs, encoders, canvases) can only be exercised
inside OBS: watch `Help → Log Files → View Current Log` for the `[manycast]` lines.

## 🗂 Project layout

```
src/
  plugin-main.cpp          module entry points: dock registration, frontend events
  plugin-support.c.in/.h   template scaffolding: PLUGIN_NAME/PLUGIN_VERSION and obs_log
  common/                  infrastructure and pure helpers (no plugin domain logic)
    obs-refs.hpp           RAII wrappers around libobs handles
    guard.hpp              exception guard for callbacks coming from libobs and Qt
    plugin-state.{hpp,cpp} "OBS is shutting down" flag
    localization.hpp       obs_module_text -> QString helpers
    format.{hpp,cpp}       key masking, duration/bitrate formatting, id generator  [unit tested]
    stats.{hpp,cpp}        bitrate and fps maths from two counter samples          [unit tested]
    snap.{hpp,cpp}         snapping to the canvas edges and centre                 [unit tested]
  core/                    domain model and streaming (talks to libobs)
    destination.hpp        destination model                                       [unit tested]
    config.*               per-profile JSON persistence
    services.*             OBS platform/server/encoder enumeration
    output.*               outputs, services, encoders, the shared portrait encoders
    portrait-canvas.*      the second (vertical) canvas and its scenes
  ui/                      Qt
    destinations-dock.*    destinations dock with per-destination status
    destination-dialog.*   destination editor
    portrait-dock.*        vertical layout dock
    portrait-preview.*     obs_display preview with mouse editing
tests/
  test-main.cpp            doctest unit tests
  check-locales.ps1        localization consistency checker
  smoke-plugin.ps1         loads the built dll and verifies the OBS module exports
  doctest/doctest.h        doctest 2.4.11 (MIT, vendored)
```

## 🩺 Troubleshooting

| Symptom | Cause and fix |
|---|---|
| The dock shows `Offline` and the stream does not start | the encoder of the main stream is not available yet - start the main stream once, or switch the destination to a dedicated encoder |
| `Could not create the service` or `the server rejected the stream key` | wrong platform, server or key; after enabling YouTube dual stream copy **both** keys again |
| The portrait preview stays black | the portrait scene is empty - press `From current scene` or add a source |
| `The canvas size cannot be changed while portrait streams are running` | stop the portrait destinations, then apply the new size |
| The canvas has the wrong size after a restart | OBS does not store the video settings of a canvas; ManyCast recreates it and stores the size in its own config - set it once more in the dock |
| A destination works but the platform shows nothing | check that both feeds do not use the same stream key |
| Nothing useful in the log | the plugin logs with the `[manycast]` prefix in `Help → Log Files → View Current Log` |

## 🤝 Contributing

Issues and pull requests are welcome. Please make sure that

* the project builds with `windows-x64` and the unit tests, the locale check and the smoke test pass;
* the sources and the CMake files are formatted (`.clang-format`, tabs, 120 columns);
* new UI strings are added to **all** files in `data/locale` (or at least to `en-US.ini`, the check
  will tell you what is missing).

## 📄 License

GPL-2.0-or-later - see [LICENSE](LICENSE). OBS Studio itself is licensed under the same terms.

## 🙏 Acknowledgements

* The [obs-plugin-template](https://github.com/obsproject/obs-plugintemplate) this project is built on.
* [doctest](https://github.com/doctest/doctest) - the unit test framework.
* [shields.io](https://shields.io) and [Simple Icons](https://simpleicons.org) for the badges.
