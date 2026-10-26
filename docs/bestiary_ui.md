# HikariRO Bestiary visual UI (prototype)

Branch: `hro_bestiary_ui_test` (based on client extension `main`).

The visual window lives in `HRO_Fishing_UI/hro_bestiary_ui.h` and is included by the **actual** existing source `HRO_Fishing_UI/ddraw.c`, preserving Fishing, Card Album and Cooking UI. The `src/ddraw/ddraw.c` file on main is currently a placeholder and must not be used as a production build source.

## Build

Use `HRO_Fishing_UI/build_x86.bat` (x86 Visual Studio tools) on Windows, not the root `build/build_x86.bat` script until its source path is corrected.

## Server dependency

Requires map-server branch `hro_bestiary_test` with the temporary `@hrobestiary [page]` command. The server sends a self-only broadcast protocol:
- `HROBEST|B|page|total;`
- `HROBEST|M|id|state|kills|lv|hp|race|element|name;`
- `HROBEST|E;`

State 0 is masked; state 1 includes only the name; state 2 includes attributes. Server checks account-specific SQL records. The client never determines unlocks.

## Current limitations

This is a **first interactive Win32 grid**, not a completed graphical bestiary:
- No sprite rendering yet; tiles show IDs and names.
- No filters/search, drop lists, map list, or server-driven page controls.
- No automatic protocol reassembly across split network buffers.
- No compiled DLL or in-game validation has been performed.
- Opening the window currently uses the temporary command; final access method is undecided.
- Existing fishing/cooking windows must be regression-tested.

Do not merge into `main` or deploy to production before compiling and validating with the matching map-server branch.
