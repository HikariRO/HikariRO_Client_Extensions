# ⛔ HikariRO Bestiary — ABANDONED EXPERIMENT / NOT FUNCTIONAL

> **STOP — DO NOT INSTALL OR DISTRIBUTE `ddraw.dll` BUILT FROM THIS BRANCH.**
>
> **Branch:** `hro_bestiary_ui_test`
>
> **Status:** INCOMPLETE, NOT FUNCTIONAL, NOT APPROVED FOR USE.
>
> This branch contains an unfinished experimental Bestiary interface. Its visuals, background asset integration, monster sprite loading and UI behavior are not production-ready. It has known rendering/alignment defects and unresolved functional issues.
>
> **Never use artifacts, DLLs or source builds from this branch to update the HikariRO game client.** Do not include this branch in the launcher/Thor patcher, automatic update packages or production releases.
>
> **For normal HikariRO client updates, use only the separately approved stable client-extension release/branch.** Do not assume that a successful GitHub Actions build means a DLL is safe or ready to deploy.
>
> This branch is retained **for historical/reference purposes only**. Development is paused indefinitely. No merge into production is authorized.
>
> **ESPAÑOL:** Esta rama del Bestiario está **INCOMPLETA, NO FUNCIONA CORRECTAMENTE y NO DEBE USARSE**. No descargues ni distribuyas su `ddraw.dll`, ni la incluyas en el actualizador de HikariRO. El código se conserva únicamente como referencia histórica.

---

# HikariRO Client Extensions

Custom client-side extensions used by HikariRO.

## Current baseline

The initial source baseline is **HRO_Fishing_UI_Filter_HRO_Prefix_v8.1** (2026-09-14). It contains the current Fishing HUD / Fishing Album, Card Album, Cooking Recipe Book and the HRO custom-storage control-traffic filtering used by the client extension.

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

The Cooking Recipe Book is opened by the map-server through `HROCOOK` control records. Categories and recipes are populated from `db/import/hro_cooking_db.yml`; no recipe list is embedded in the DLL.

## Development rule

`main` represents the last accepted client-extension baseline. Experimental fixes should be developed on separate branches and merged only after in-game testing.
