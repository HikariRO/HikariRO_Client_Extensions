# Cooking Recipe Book

The map-server remains authoritative. When the Chef NPC invokes `hrocookingbookopen`, it sends a short `HROCOOK` catalog stream containing the categories, recipes, learned state, success rate, failure behavior, and current ingredient counts.

The client extension suppresses these control messages from chat and renders them in a 720x500 open-book window. Dish and ingredient resources are resolved by item ID from the client's `itemInfo.lua`/`itemInfo.lub`, then their BMPs are loaded from the configured GRFs through `DATA.ini`. The selected dish uses its higher-quality `collection` artwork, while list and ingredient rows use compact `item` icons at their native size without pixelating enlargement. Recipe categories are selected through a scrollable drop-down so the YAML can grow without overflowing the window. `View item details` reads the produced item's existing client description directly from `itemInfo`; cooking YAML files require no description field.

## Test checklist

1. Build the x86 DLL with `HRO_Fishing_UI/build_x86.bat`.
2. Replace the client's existing `ddraw.dll` while the client is closed.
3. Start the server from branch `hro_cooking_system` and reload `npc/hikariro/cooking.txt`.
4. Select **Recipe Book** at `Chef#HRO_Navayo`.
5. Confirm category tabs, learned/locked state, dish icons, ingredient icons, owned/required counts and page navigation.
6. Add a new YAML category and recipe, run `@reloadcookingdb`, and confirm both appear without rebuilding the DLL.

Cooking remains a separate NPC action. The recipe-book window is intentionally read-only, like the Fishing Album.
