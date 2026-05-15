# ShadowRec Game Capture

DirectX game capture via DLL injection for ShadowRec streaming software.

## Status

**Phase 3 - Complete** ✅ (10 May 2026)

- ✅ Hook DLL with logging (`hook/dllmain.cpp`)
- ✅ Process injector with CreateRemoteThread + LoadLibraryA (`hook/injector.cpp`)
- ✅ Successfully tested injecting into Notepad

## Build

```bash
cd hook
mkdir build && cd build
cmake .. -A x64
cmake --build . --config Release
```

Output:
- `Release/shadowrec_hook.dll` - the hook DLL
- `Release/injector.exe` - the injector tool

## Test

```bash
# Open notepad first
start notepad

# Inject the DLL into notepad
Release\injector.exe notepad.exe F:\game-capture\hook\build\Release\shadowrec_hook.dll

# Check the log written from inside notepad
type %TEMP%\ShadowRec_HookLog.txt
```

Expected log output:
```
[HH:MM:SS.mmm] ShadowRec Hook DLL yuklendi! PID=XXXXX, Process=C:\WINDOWS\system32\notepad.exe
```

## Architecture

```
ShadowRec (Electron)
   |
   v
Injector (CreateRemoteThread + LoadLibraryA)
   |
   v
shadowrec_hook.dll loaded INSIDE game process
   |
   v
[Phase 4] DirectX Present() VTable hook
   |
   v
[Phase 4] Capture frame in DLL
   |
   v
[Phase 4] Write to Shared Memory ring buffer
   |
   v
ShadowRec reads frames -> FFmpeg -> MP4/RTMPS
```

## Phase 4 - Next Steps

- [ ] DirectX 11 `IDXGISwapChain::Present` VTable hook
- [ ] Dummy SwapChain creation to obtain VTable address
- [ ] Frame capture inside DLL (CopyResource to staging texture)
- [ ] Shared memory IPC (DLL writes, ShadowRec reads)
- [ ] Lock-free ring buffer design
- [ ] Integration with existing ShadowRec native module
- [ ] DirectX 9 hook for legacy games (Terminator 3, etc.)
- [ ] DirectX 12 hook for modern games
- [ ] OpenGL hook (Minecraft Java, older games)
- [ ] Vulkan hook (newer games)

## License

GNU General Public License v3.0

## Related Projects

- [shadowrec](https://github.com/gungkhan6-source/shadowrec) - Main native capture module (DXGI based)
