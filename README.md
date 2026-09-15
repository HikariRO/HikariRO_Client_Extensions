# HikariRO Client Extensions

Custom client-side extensions used by HikariRO.

## Current baseline

The initial source baseline is **HRO_Fishing_UI_Filter_HRO_Prefix_v8.1** (2026-09-14). It contains the current Fishing HUD / Fishing Album, Card Album and the HRO custom-storage control-traffic filtering used by the client extension.

The repository intentionally tracks source code and build scripts, not compiled `ddraw.dll` binaries or Ragnarok Online client data.

## Layout

- `src/ddraw/` - ddraw proxy/extension source and its miniz dependency.
- `build/build_x86.bat` - Visual Studio x86 build script.
- `docs/` - implementation and test notes.

## Build

Install Visual Studio Build Tools with **Desktop development with C++**, then run:

```bat
build\build_x86.bat
```

The resulting `ddraw.dll` is generated in the repository root and is ignored by Git.

## Development rule

`main` represents the last accepted client-extension baseline. Experimental fixes should be developed on separate branches and merged only after in-game testing.
