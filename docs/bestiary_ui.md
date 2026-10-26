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

## Native SPR preview (work in progress)

The UI now calls `hrobest_load_sprite_bitmap(mob_id)` for discovered
monsters and draws the first decoded SPR image in the right panel.

Resource lookup uses the existing `load_client_file` function, so a
loose client file or an unencrypted GRF entry can supply the bytes.
No PNG conversion is involved.

The Mob ID to sprite path is **not yet automatic**. For a temporary
resource test, put a text entry at `data\\hro_bestiary\\1002.txt` whose
single line is the **exact client-relative SPR path** for Poring.
The file is a mapping, not a graphic; the actual SPR is read from the
original client resources.

Known limitations:
- First raw SPR image only. ACT layer composition and animation are not implemented.
- SPR parser supports a subset of 2.x and has not been verified against
  all modern sprites.
- The GDI preview currently handles only fully transparent pixels, not
  partial alpha.
- Automatic mob view ID / client sprite name resolution is still required.
- Pagination is disabled until a safe packet protocol is available.
- Build and in-game behavior must be validated before distribution.
