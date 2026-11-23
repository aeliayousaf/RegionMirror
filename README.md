# Region Mirror

Native Windows 11 desktop app that captures a window or monitor with **Windows.Graphics.Capture**, lets you crop a live region, and mirrors that crop onto another display. Primary use case: enlarge a game minimap on a second monitor.

## Requirements

| Component | Notes |
| --- | --- |
| Windows 10 1903+ / Windows 11 | Graphics Capture APIs |
| Visual Studio 2022/2026 **Build Tools** or IDE with **MSVC v145** (or v143) x64 toolset | `Microsoft.VisualStudio.Component.VC.Tools.x86.x64` |
| Windows 10 SDK (this machine used **10.0.28000.0**) | Includes C++/WinRT headers under `Include\<ver>\cppwinrt` |
| Direct3D 11 capable GPU | Hardware device |

Optional for development: Windows SDK **Debugging Tools** / D3D debug layer (Debug builds request `D3D11_CREATE_DEVICE_DEBUG` and fall back if unavailable).

## Build

From a **x64 Native Tools** / Developer Command Prompt, or after `vcvars64.bat`:

```bat
msbuild RegionMirror.sln /m /p:Configuration=Debug /p:Platform=x64
msbuild RegionMirror.sln /m /p:Configuration=Release /p:Platform=x64
```

### Outputs

| Config | Executable |
| --- | --- |
| Debug | `bin\Debug\RegionMirror.exe` |
| Release | `bin\Release\RegionMirror.exe` |
| Tests | `bin\Debug\RegionMathTests.exe` / `bin\Release\RegionMathTests.exe` |

Run rectangle-conversion tests:

```bat
bin\Release\RegionMathTests.exe
```

## Launch

Double-click `bin\Release\RegionMirror.exe` (or Debug). No config files or developer commands are required after launch.

Settings (output monitor preference, windowed/fullscreen, debug panel) are stored under:

`%LocalAppData%\RegionMirror\settings.ini`

Source selection is **session-local**; use **Choose source** again after relaunch.

## Supported capture modes

- **Window capture** via the system Graphics Capture picker (including games in **borderless windowed** / windowed modes when the compositor can capture them).
- **Monitor / display capture** via the same picker.
- **SDR** `B8G8R8A8` path only for this MVP.

### HDR

Capture and present use `B8G8R8A8UIntNormalized` / `DXGI_FORMAT_B8G8R8A8_UNORM`. HDR sources may appear tone-mapped, washed out, or incorrect; there is no HDR10/scRGB pipeline yet. Prefer SDR display modes for predictable results.

### Cursor

Cursor capture is disabled when `GraphicsCaptureSession.IsCursorCaptureEnabled` is available.

### Self-exclusion

The control and output windows call `SetWindowDisplayAffinity(..., WDA_EXCLUDEFROMCAPTURE)` so they should not appear inside display capture. If exclusion is unavailable on an older OS build, move the Region Mirror windows off the captured monitor.

Display capture **refuses** starting a mirror on the same monitor as the captured display (feedback). Window capture remains usable on a single-monitor system.

## User flow

1. **Choose source** — system picker (initialized with the main window handle). Cancel leaves the current source unchanged.
2. Live preview (aspect-preserving). **Select region** — drag on the video area (not letterbox bars). Esc cancels an in-progress drag. Empty selections are rejected. Size is shown in source pixels.
3. **Output monitor** + **Windowed** / **Fullscreen**, then **Start mirror**.
4. **Edit region** / **Change source** / **Stop mirror** as needed. Closing the output window stops the mirror. Esc exits fullscreen when the output has focus.

## Architecture notes

- **UI thread** owns the D3D11 immediate context and both flip-model swap chains (preview + mirror).
- **Free-threaded** `Direct3D11CaptureFramePool` callbacks stash the latest WinRT frame (keeping the surface alive) and `PostMessage` the UI thread.
- The UI thread GPU-copies into a reusable shader-readable texture, closes the capture frame promptly, then presents. Stale pending frames are dropped when rendering falls behind.
- Preview painting is suspended while the control window is minimized; the mirror continues.
- Crop is stored as **normalized** content coordinates and remapped when the same source changes size.

## Troubleshooting

| Symptom | What to try |
| --- | --- |
| “Graphics Capture is not supported” | Upgrade Windows / ensure desktop (not remote-only) session |
| Black / frozen game capture | Use **borderless windowed**; or capture the **display** instead of the game window. Exclusive fullscreen is often incompatible with WGC. |
| Picker cancel | Previous source stays active |
| Destination unplugged | Mirror stops; pick another monitor (crop kept) |
| Device removed | One automatic recovery attempt; then **Retry graphics** |
| Feedback on display capture | Choose a different output monitor; confirm exclusion affinity |

## Verification

### Automated

`RegionMathTests` covers letterbox mapping, reverse drags, clamping, empty rejection, and crop remap after capture size change. **Result: all passed** (Debug + Release) on the development machine.

### Manual (this environment)

| Scenario | Result |
| --- | --- |
| Build Debug + Release x64 | Passed |
| App launches (`RegionMirror.exe`) | Smoke-tested: Release process stayed running after start |
| Drag region all directions / letterbox mapping | Covered by unit tests; exercise in UI with a clock/video window |
| Mirror while control minimized | Implemented (preview suspends; mirror presents on capture ticks) |
| Windowed + fullscreen placement / aspect bars | Implemented via monitor bounds vs work area |
| Monitor left of primary / mixed DPI | Code uses virtual-desktop coords + Per-Monitor V2; **not fully hardware-verified** on this machine if topology unavailable |
| Source resize / close / start-stop churn | Handlers implemented; exercise manually |
| Display capture feedback | Exclusion + same-monitor block implemented; verify visually |
| Destination unplug | `WM_DISPLAYCHANGE` refresh; verify if hardware available |
| Game borderless windowed | **Not exercised in this session** — test locally |
| Exclusive fullscreen game | **Not supported / not claimed** for WGC |
| End-to-end display latency | **Not measured**. To measure: show the same timer in source and mirror, film both displays at high frame rate, compare readings; record refresh rates. App “frame age” in Debug is process-clock age of the last captured frame, **not** glass-to-glass latency. |

### Performance target

60 fps present/capture is a target under suitable conditions. Use the in-app **Debug** panel for capture FPS, present FPS, and dropped frames. No zero-latency guarantee.

## MVP limits

One source and one mirrored crop. No audio, recording, networking, multi-region, installer, or exclusive-fullscreen capture backend.
