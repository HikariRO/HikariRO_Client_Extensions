#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>
#include <tlhelp32.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "miniz.h"

typedef HRESULT (WINAPI *CreateFn)(GUID*, void**, REFIID, void*);
typedef HRESULT (WINAPI *EnumFn)(void*, void*, DWORD);
typedef int (WSAAPI *RecvFn)(SOCKET, char*, int, int);
typedef int (WSAAPI *WSARecvFn)(SOCKET, LPWSABUF, DWORD, LPDWORD, LPDWORD,
	LPWSAOVERLAPPED, LPWSAOVERLAPPED_COMPLETION_ROUTINE);
static HMODULE real_ddraw;
static CreateFn real_create;
static EnumFn real_enum;
static RecvFn real_recv;
static WSARecvFn real_wsarecv;
static volatile LONG state_, tension_, distance_, resistance_, action_;
static volatile LONG fishing_level_, fishing_exp_, fishing_next_exp_;
static volatile LONG gained_exp_;
static float shown_tension_, shown_distance_;
static HWND game_, hud_;
static HWND album_;
static HWND card_album_;
static HWND cooking_book_;
static HBITMAP card_album_background_;
static int card_album_background_attempted_;
static HBITMAP album_background_;
static int album_background_attempted_;
static HBITMAP fight_background_;
static int fight_background_attempted_;
static HBITMAP waiting_background_, result_background_;
static int waiting_background_attempted_, result_background_attempted_;
static LONG marker_logged_;
static LONG recv_calls_;
static LONG wsarecv_calls_;
static LONG client_pointer_hooked_;
static DWORD result_until_;
static BYTE tail_[160];
static int tail_len_;

typedef struct FishingAlbumEntry {
	int id, catches, size, weight, quality;
	char fish_name[40], char_name[40], map_name[32], caught_at[24], resource_name[64];
	HBITMAP collection_image;
	HBITMAP locked_image;
	int collection_attempted;
} FishingAlbumEntry;

static FishingAlbumEntry album_entries_[512];
static volatile LONG album_open_, album_level_, album_exp_, album_next_exp_;
static int album_count_, album_expected_, album_selected_, album_page_;
static int album_first_paint_ = 1, album_image_load_budget_;
static BYTE album_tail_[512];
static int album_tail_len_;

typedef struct CardAlbumEntry {
	int id, amount;
	char name[100], registered_at[24], last_obtained_at[24];
	HBITMAP image;
	HBITMAP locked_image;
	int image_attempted;
} CardAlbumEntry;

static CardAlbumEntry card_entries_[4096];
static volatile LONG card_album_open_;
static int card_count_, card_expected_, card_selected_, card_page_;
static int card_filter_;
static char card_search_[64];
static int card_search_active_;
static char card_page_input_[8];
static int card_page_input_active_;
static int card_first_paint_ = 1, card_image_load_budget_;
static BYTE card_tail_[512];
static int card_tail_len_;
static BYTE storage_tail_[512];
static int storage_tail_len_;
static BYTE* card_name_table_;
static DWORD card_name_table_size_;
static int card_name_table_attempted_;
static BYTE* item_info_table_;
static DWORD item_info_table_size_;
static int item_info_table_attempted_;
static BYTE* english_item_info_table_;
static DWORD english_item_info_table_size_;
static int english_item_info_table_attempted_;
static BYTE* item_description_table_;
static DWORD item_description_table_size_;
static int item_description_table_attempted_;
static int card_chat_bytes_to_discard_;

typedef struct CookingIngredient {
	int item_id, required, owned;
	char name[64], resource_name[64];
	HBITMAP image;
	int image_attempted;
} CookingIngredient;

typedef struct CookingRecipe {
	int id, category, product_id, amount, success_rate, consume_failure, unlocked, recipe_item;
	char name[64], resource_name[64];
	CookingIngredient ingredients[12];
	int ingredient_count;
	HBITMAP image;
	int image_attempted;
	HBITMAP collection_image;
	int collection_attempted;
	char description[768];
	int description_attempted;
} CookingRecipe;

static CookingRecipe cooking_recipes_[256];
static char cooking_categories_[64][32];
static volatile LONG cooking_book_open_;
static int cooking_recipe_count_, cooking_expected_, cooking_category_count_;
static int cooking_selected_, cooking_category_ = -1, cooking_page_;
static int cooking_category_dropdown_, cooking_category_scroll_;
static int cooking_item_details_open_;
static int cooking_mode_, cooking_result_, cooking_result_received_, cooking_request_pending_;
static volatile LONG cooking_request_serial_;
static volatile LONG cooking_progress_start_;
static const DWORD COOKING_PROGRESS_DURATION_MS = 2500;
static int cooking_requested_category_ = -1;

typedef struct CookingRequest {
	int recipe_id;
	LONG serial;
} CookingRequest;
static int cooking_first_paint_ = 1, cooking_image_load_budget_;
static HBITMAP cooking_title_icon_;
static int cooking_title_icon_attempted_;
static BYTE cooking_tail_[512];
static int cooking_tail_len_;

static HBITMAP load_fishing_background(const char* filename);

static void byte_copy(void* output, const void* input, int length) {
	BYTE* out = (BYTE*)output;
	const BYTE* in = (const BYTE*)input;
	for (int i = 0; i < length; ++i) out[i] = in[i];
}

static BOOL bytes_equal(const BYTE* input, const char* text, int length) {
	for (int i = 0; i < length; ++i)
		if (input[i] != (BYTE)text[i]) return FALSE;
	return TRUE;
}

static BOOL parse_number(const BYTE* data, int total, int* position, int* value, char delimiter) {
	int result = 0;
	BOOL found = FALSE;
	while (*position < total && data[*position] >= '0' && data[*position] <= '9') {
		found = TRUE;
		result = result * 10 + data[*position] - '0';
		(*position)++;
	}
	if (!found || *position >= total || data[*position] != (BYTE)delimiter) return FALSE;
	(*position)++;
	*value = result;
	return TRUE;
}

static BOOL parse_album_entry(const char* payload, FishingAlbumEntry* entry) {
	/* Album records deliberately contain empty fields for locked species:
	 * id|catches|size|weight|quality|name||| |resource. sscanf scansets stop at
	 * an empty field, so split delimiters explicitly and preserve empty values. */
	char record[400];
	char* field[10];
	lstrcpynA(record, payload, sizeof(record));
	field[0] = record;
	for (int i = 1; i < 10; ++i) {
		char* delimiter = strchr(field[i - 1], '|');
		if (!delimiter) return FALSE;
		*delimiter = '\0';
		field[i] = delimiter + 1;
	}
	ZeroMemory(entry, sizeof(*entry));
	if (sscanf(field[0], "%d", &entry->id) != 1 ||
		sscanf(field[1], "%d", &entry->catches) != 1 ||
		sscanf(field[2], "%d", &entry->size) != 1 ||
		sscanf(field[3], "%d", &entry->weight) != 1 ||
		sscanf(field[4], "%d", &entry->quality) != 1) return FALSE;
	lstrcpynA(entry->fish_name, field[5], sizeof(entry->fish_name));
	lstrcpynA(entry->char_name, field[6], sizeof(entry->char_name));
	lstrcpynA(entry->map_name, field[7], sizeof(entry->map_name));
	lstrcpynA(entry->caught_at, field[8], sizeof(entry->caught_at));
	lstrcpynA(entry->resource_name, field[9], sizeof(entry->resource_name));
	return entry->id > 0 && entry->fish_name[0] && entry->resource_name[0];
}

static void parse_album_payload(const char* payload) {
	if (payload[0] == 'B' && payload[1] == '|') {
		int level = 1, exp = 0, next_exp = 250, expected = 0;
		if (sscanf(payload + 2, "%d|%d|%d|%d", &level, &exp, &next_exp, &expected) == 4) {
			/* Do not destroy bitmaps from the network-hook thread while the album
			 * window may still be painting them on the HUD thread. The catalog is
			 * small and is safely replaced when the client process exits. */
			album_count_ = 0;
			album_expected_ = expected;
			album_selected_ = 0;
			album_page_ = 0;
			album_first_paint_ = 1;
			InterlockedExchange(&album_level_, level);
			InterlockedExchange(&album_exp_, exp);
			InterlockedExchange(&album_next_exp_, next_exp);
			// Open immediately. Entries may continue arriving while the album is
			// visible; a slow SQL query must never leave the NPC interaction stuck.
			InterlockedExchange(&album_open_, 1);
			if (album_) InvalidateRect(album_, NULL, FALSE);
		}
	} else if (payload[0] == 'E' && payload[1] == '|' && album_count_ < 512) {
		FishingAlbumEntry entry = {0};
		if (parse_album_entry(payload + 2, &entry)) {
			album_entries_[album_count_++] = entry;
			if (album_) InvalidateRect(album_, NULL, FALSE);
		}
	} else if (payload[0] == 'O') {
		InterlockedExchange(&album_open_, 1);
		if (album_) InvalidateRect(album_, NULL, FALSE);
	}
}

static BOOL parse_card_entry(const char* payload, CardAlbumEntry* entry) {
	char record[320];
	char* field[5];
	lstrcpynA(record, payload, sizeof(record));
	field[0] = record;
	for (int i = 1; i < 5; ++i) {
		char* delimiter = strchr(field[i - 1], '|');
		if (!delimiter) return FALSE;
		*delimiter = '\0';
		field[i] = delimiter + 1;
	}
	ZeroMemory(entry, sizeof(*entry));
	if (sscanf(field[0], "%d", &entry->id) != 1 ||
		sscanf(field[1], "%d", &entry->amount) != 1) return FALSE;
	lstrcpynA(entry->name, field[2], sizeof(entry->name));
	lstrcpynA(entry->registered_at, field[3], sizeof(entry->registered_at));
	lstrcpynA(entry->last_obtained_at, field[4], sizeof(entry->last_obtained_at));
	return entry->id > 0 && entry->name[0];
}

static void parse_card_payload(const char* payload) {
	if (payload[0] == 'B' && payload[1] == '|') {
		int expected = 0;
		if (sscanf(payload + 2, "%d", &expected) == 1) {
			card_count_ = 0;
			card_expected_ = expected;
			card_selected_ = 0;
			card_page_ = 0;
			card_first_paint_ = 1;
			card_filter_ = 0;
			card_search_[0] = 0;
			card_search_active_ = 0;
			card_page_input_[0] = 0;
			card_page_input_active_ = 0;
			InterlockedExchange(&card_album_open_, 1);
			if (card_album_) InvalidateRect(card_album_, NULL, FALSE);
		}
	} else if (payload[0] == 'E' && payload[1] == '|' && card_count_ < 4096) {
		CardAlbumEntry entry = {0};
		if (parse_card_entry(payload + 2, &entry)) {
			card_entries_[card_count_++] = entry;
			if (card_album_) InvalidateRect(card_album_, NULL, FALSE);
		}
	} else if (payload[0] == 'O') {
		InterlockedExchange(&card_album_open_, 1);
		if (card_album_) InvalidateRect(card_album_, NULL, FALSE);
	}
}

static int split_fields(char* record, char** fields, int maximum) {
	int count = 1;
	fields[0] = record;
	while (count < maximum) {
		char* delimiter = strchr(fields[count - 1], '|');
		if (!delimiter) break;
		*delimiter = '\0';
		fields[count++] = delimiter + 1;
	}
	return count;
}

static CookingRecipe* cooking_recipe_by_id(int id) {
	for (int i = 0; i < cooking_recipe_count_; ++i)
		if (cooking_recipes_[i].id == id) return &cooking_recipes_[i];
	return NULL;
}

static void release_cooking_catalog(void) {
	for (int recipe_index = 0; recipe_index < cooking_recipe_count_; ++recipe_index) {
		CookingRecipe* recipe = &cooking_recipes_[recipe_index];
		if (recipe->image) DeleteObject(recipe->image);
		if (recipe->collection_image) DeleteObject(recipe->collection_image);
		for (int ingredient_index = 0; ingredient_index < 12; ++ingredient_index)
			if (recipe->ingredients[ingredient_index].image)
				DeleteObject(recipe->ingredients[ingredient_index].image);
	}
	ZeroMemory(cooking_recipes_, sizeof(cooking_recipes_));
}

static void parse_cooking_payload(const char* payload) {
	if (payload[0] == 'B' && payload[1] == '|') {
		int categories = 0, recipes = 0, mode = 0;
		if (sscanf(payload + 2, "%d|%d|%d", &categories, &recipes, &mode) >= 2) {
			cooking_mode_ = mode;
			if (!cooking_request_pending_) { cooking_result_ = 0; cooking_result_received_ = 0; }
			release_cooking_catalog();
			cooking_recipe_count_ = 0;
			cooking_category_count_ = categories > 64 ? 64 : categories;
			cooking_expected_ = recipes;
			cooking_selected_ = cooking_page_ = 0;
			cooking_category_ = -1;
			cooking_category_dropdown_ = cooking_category_scroll_ = 0;
			cooking_first_paint_ = 1;
			ZeroMemory(cooking_categories_, sizeof(cooking_categories_));
			InterlockedExchange(&cooking_book_open_, 1);
			if (cooking_book_) InvalidateRect(cooking_book_, NULL, FALSE);
		}
	} else if (payload[0] == 'C' && payload[1] == '|') {
		int index = -1; char name[32] = {0};
		if (sscanf(payload + 2, "%d|%31[^|]", &index, name) == 2 && index >= 0 && index < 64)
			lstrcpynA(cooking_categories_[index], name, sizeof(cooking_categories_[index]));
	} else if (payload[0] == 'R' && payload[1] == '|' && cooking_recipe_count_ < 256) {
		char record[512]; char* field[11];
		lstrcpynA(record, payload + 2, sizeof(record));
		if (split_fields(record, field, 11) == 11) {
			CookingRecipe entry = {0};
			if (sscanf(field[0], "%d", &entry.id) == 1 && sscanf(field[1], "%d", &entry.category) == 1 &&
				sscanf(field[2], "%d", &entry.product_id) == 1 && sscanf(field[3], "%d", &entry.amount) == 1 &&
				sscanf(field[4], "%d", &entry.success_rate) == 1 && sscanf(field[5], "%d", &entry.consume_failure) == 1 &&
				sscanf(field[6], "%d", &entry.unlocked) == 1 && sscanf(field[7], "%d", &entry.recipe_item) == 1 &&
				sscanf(field[8], "%d", &entry.ingredient_count) == 1) {
				lstrcpynA(entry.name, field[9], sizeof(entry.name));
				lstrcpynA(entry.resource_name, field[10], sizeof(entry.resource_name));
				if (entry.ingredient_count > 12) entry.ingredient_count = 12;
				cooking_recipes_[cooking_recipe_count_++] = entry;
			}
		}
	} else if (payload[0] == 'I' && payload[1] == '|') {
		char record[384]; char* field[6];
		lstrcpynA(record, payload + 2, sizeof(record));
		if (split_fields(record, field, 6) == 6) {
			int recipe_id = 0; CookingIngredient ingredient = {0};
			if (sscanf(field[0], "%d", &recipe_id) == 1 && sscanf(field[1], "%d", &ingredient.item_id) == 1 &&
				sscanf(field[2], "%d", &ingredient.required) == 1 && sscanf(field[3], "%d", &ingredient.owned) == 1) {
				lstrcpynA(ingredient.name, field[4], sizeof(ingredient.name));
				lstrcpynA(ingredient.resource_name, field[5], sizeof(ingredient.resource_name));
				CookingRecipe* recipe = cooking_recipe_by_id(recipe_id);
				if (recipe) {
					int slot = 0;
					while (slot < 12 && recipe->ingredients[slot].item_id) ++slot;
					if (slot < 12) recipe->ingredients[slot] = ingredient;
				}
			}
		}
	} else if (payload[0] == 'S' && payload[1] == '|') {
		int result = 0, recipe_id = 0;
		if (sscanf(payload + 2, "%d|%d", &result, &recipe_id) == 2) {
			cooking_result_ = result;
			cooking_result_received_ = 1;
			cooking_request_pending_ = 0;
			InterlockedExchange(&cooking_progress_start_, 0);
			cooking_category_ = cooking_requested_category_;
			cooking_requested_category_ = -1;
			int visible_position = 0;
			for (int index = 0; index < cooking_recipe_count_; ++index) {
				if (cooking_recipes_[index].id == recipe_id) {
					cooking_selected_ = index;
					cooking_page_ = visible_position / 6;
					break;
				}
				if (cooking_category_ < 0 || cooking_recipes_[index].category == cooking_category_) ++visible_position;
			}
			if (cooking_book_) InvalidateRect(cooking_book_, NULL, FALSE);
		}
	} else if (payload[0] == 'O') {
		InterlockedExchange(&cooking_book_open_, 1);
		if (cooking_book_) InvalidateRect(cooking_book_, NULL, FALSE);
	}
}

static void suppress_stream_marker(char* data, int length, const char* marker, int marker_length) {
	for (int i = 0; i + marker_length <= length; ++i)
		if (bytes_equal((BYTE*)data + i, marker, marker_length)) data[i] = 0;
	for (int prefix_length = marker_length - 1; prefix_length >= 4; --prefix_length) {
		if (length >= prefix_length && bytes_equal((BYTE*)data + length - prefix_length, marker, prefix_length)) {
			data[length - prefix_length] = 0;
			break;
		}
	}
}

static BOOL marker_crosses_boundary(const BYTE* tail, int tail_length,
	const char* data, int length, const char* marker, int marker_length) {
	for (int prefix_length = 1; prefix_length < marker_length; ++prefix_length) {
		int remaining = marker_length - prefix_length;
		if (tail_length >= prefix_length && length >= remaining &&
			bytes_equal(tail + tail_length - prefix_length, marker, prefix_length) &&
			bytes_equal((BYTE*)data, marker + prefix_length, remaining)) return TRUE;
	}
	return FALSE;
}

/* HROSTORAGE is control traffic for the native custom-storage window.
 * It must never be rendered in chat, but it must not open or replace any UI. */
static void suppress_storage_stream(char* data, int length) {
	static const char marker[] = "HROSTORAGE|";
	const int marker_length = 11;
	BOOL crosses = marker_crosses_boundary(storage_tail_, storage_tail_len_, data, length,
		marker, marker_length);
	int copy = length > 8192 ? 8192 : length;
	BYTE merged[8704];
	byte_copy(merged, storage_tail_, storage_tail_len_);
	byte_copy(merged + storage_tail_len_, data + length - copy, copy);
	int total = storage_tail_len_ + copy;
	storage_tail_len_ = total > marker_length - 1 ? marker_length - 1 : total;
	byte_copy(storage_tail_, merged + total - storage_tail_len_, storage_tail_len_);
	suppress_stream_marker(data, length, marker, marker_length);
	if (crosses && length > 0) data[0] = 0;
}

static void parse_card_stream(char* data, int length) {
	static const char marker[] = "HROCARD|";
	const int marker_length = 8;
	BOOL crosses = marker_crosses_boundary(card_tail_, card_tail_len_, data, length, marker, marker_length);
	BYTE merged[8704];
	int copy = length > 8192 ? 8192 : length;
	byte_copy(merged, card_tail_, card_tail_len_);
	byte_copy(merged + card_tail_len_, data + length - copy, copy);
	int total = card_tail_len_ + copy;
	int unfinished = -1;
	for (int i = 0; i + marker_length <= total; ++i) {
		if (!bytes_equal(merged + i, marker, marker_length)) continue;
		int end = i + marker_length;
		while (end < total && merged[end] != ';' && end - i < 319) ++end;
		if (end >= total || merged[end] != ';') { unfinished = i; break; }
		char payload[320];
		int payload_length = end - (i + marker_length);
		byte_copy(payload, merged + i + marker_length, payload_length);
		payload[payload_length] = '\0';
		parse_card_payload(payload);
		i = end;
	}
	if (unfinished >= 0) {
		card_tail_len_ = total - unfinished;
		if (card_tail_len_ > 511) card_tail_len_ = 511;
		byte_copy(card_tail_, merged + total - card_tail_len_, card_tail_len_);
	} else {
		card_tail_len_ = total > marker_length - 1 ? marker_length - 1 : total;
		byte_copy(card_tail_, merged + total - card_tail_len_, card_tail_len_);
	}
	suppress_stream_marker(data, length, marker, marker_length);
	if (crosses && length > 0) data[0] = 0;
}

static void parse_cooking_stream(char* data, int length) {
	static const char marker[] = "HROCOOK|";
	const int marker_length = 8;
	BOOL crosses = marker_crosses_boundary(cooking_tail_, cooking_tail_len_, data, length, marker, marker_length);
	BYTE merged[8704];
	int copy = length > 8192 ? 8192 : length;
	byte_copy(merged, cooking_tail_, cooking_tail_len_);
	byte_copy(merged + cooking_tail_len_, data + length - copy, copy);
	int total = cooking_tail_len_ + copy, unfinished = -1;
	for (int i = 0; i + marker_length <= total; ++i) {
		if (!bytes_equal(merged + i, marker, marker_length)) continue;
		int end = i + marker_length;
		while (end < total && merged[end] != ';' && end - i < 511) ++end;
		if (end >= total || merged[end] != ';') { unfinished = i; break; }
		char payload[512]; int payload_length = end - (i + marker_length);
		byte_copy(payload, merged + i + marker_length, payload_length);
		payload[payload_length] = '\0';
		parse_cooking_payload(payload);
		i = end;
	}
	if (unfinished >= 0) {
		cooking_tail_len_ = total - unfinished;
		if (cooking_tail_len_ > 511) cooking_tail_len_ = 511;
		byte_copy(cooking_tail_, merged + total - cooking_tail_len_, cooking_tail_len_);
	} else {
		cooking_tail_len_ = total > marker_length - 1 ? marker_length - 1 : total;
		byte_copy(cooking_tail_, merged + total - cooking_tail_len_, cooking_tail_len_);
	}
	suppress_stream_marker(data, length, marker, marker_length);
	if (crosses && length > 0) data[0] = 0;
}

static void suppress_album_markers(char* data, int length) {
	static const char marker[] = "HROALBUM|";
	const int marker_length = 9;
	for (int i = 0; i + marker_length <= length; ++i)
		if (bytes_equal((BYTE*)data + i, marker, marker_length)) data[i] = 0;

	/* A recv boundary can split HROALBUM|. The stream parser reconstructs that
	 * record from album_tail_, but the old filter only hid complete prefixes.
	 * Null the beginning of a sufficiently distinctive trailing prefix before
	 * the current buffer reaches the client's chat renderer. */
	for (int prefix_length = marker_length - 1; prefix_length >= 3; --prefix_length) {
		if (length >= prefix_length &&
			bytes_equal((BYTE*)data + length - prefix_length, marker, prefix_length)) {
			data[length - prefix_length] = 0;
			break;
		}
	}
}

static BOOL album_marker_crosses_boundary(const char* data, int length) {
	static const char marker[] = "HROALBUM|";
	for (int prefix_length = 1; prefix_length < 9; ++prefix_length) {
		const int remaining = 9 - prefix_length;
		if (album_tail_len_ >= prefix_length && length >= remaining &&
			bytes_equal(album_tail_ + album_tail_len_ - prefix_length, marker, prefix_length) &&
			bytes_equal((BYTE*)data, marker + prefix_length, remaining))
			return TRUE;
	}
	return FALSE;
}

static void parse_album_stream(char* data, int length) {
	// Any album marker is enough to request the window. Payload parsing still
	// fills the catalog, but a damaged or fragmented record can no longer keep
	// the interface permanently closed.
	for (int marker = 0; marker + 9 <= length; ++marker)
		if (bytes_equal((BYTE*)data + marker, "HROALBUM|", 9)) {
			InterlockedExchange(&album_open_, 1);
			break;
		}
	const BOOL marker_crosses_boundary = album_marker_crosses_boundary(data, length);
	BYTE merged[8704];
	int copy = length > 8192 ? 8192 : length;
	byte_copy(merged, album_tail_, album_tail_len_);
	byte_copy(merged + album_tail_len_, data + length - copy, copy);
	int total = album_tail_len_ + copy;
	int unfinished = -1;
	for (int i = 0; i + 9 <= total; ++i) {
		if (!bytes_equal(merged + i, "HROALBUM|", 9)) continue;
		int end = i + 9;
		while (end < total && merged[end] != ';' && end - i < 399) ++end;
		if (end >= total || merged[end] != ';') { unfinished = i; break; }
		char payload[400];
		int payload_length = end - (i + 9);
		byte_copy(payload, merged + i + 9, payload_length);
		payload[payload_length] = '\0';
		parse_album_payload(payload);
		i = end;
	}
	if (unfinished >= 0) {
		album_tail_len_ = total - unfinished;
		if (album_tail_len_ > 511) album_tail_len_ = 511;
		byte_copy(album_tail_, merged + total - album_tail_len_, album_tail_len_);
	} else {
		album_tail_len_ = total > 8 ? 8 : total;
		byte_copy(album_tail_, merged + total - album_tail_len_, album_tail_len_);
	}
	suppress_album_markers(data, length);
	/* If only one or two prefix bytes ended the previous recv, changing those
	 * bytes would risk corrupting unrelated traffic. Terminating the continuation
	 * here still prevents the reassembled chat string from being displayed. */
	if (marker_crosses_boundary && length > 0) data[0] = 0;
}

static void log_line(const char* line) {
	char path[MAX_PATH];
	GetModuleFileNameA(NULL, path, MAX_PATH);
	char* slash = NULL;
	for (char* p = path; *p; ++p) if (*p == '\\') slash = p;
	if (slash) lstrcpyA(slash + 1, "hro_fishing_ui.log");
	HANDLE file = CreateFileA(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
		NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (file == INVALID_HANDLE_VALUE) return;
	DWORD written;
	WriteFile(file, line, (DWORD)lstrlenA(line), &written, NULL);
	WriteFile(file, "\r\n", 2, &written, NULL);
	CloseHandle(file);
}

static void apply_fishing_state(int state, int tension, int distance, int resistance,
	int action, int level, int exp, int next_exp, int gained_exp) {
	if (InterlockedCompareExchange(&marker_logged_, 1, 0) == 0)
		log_line("Fishing state received from map-server.");
	InterlockedExchange(&state_, state);
	InterlockedExchange(&tension_, tension);
	InterlockedExchange(&distance_, distance);
	InterlockedExchange(&resistance_, resistance);
	InterlockedExchange(&action_, action);
	InterlockedExchange(&fishing_level_, level);
	InterlockedExchange(&fishing_exp_, exp);
	InterlockedExchange(&fishing_next_exp_, next_exp);
	InterlockedExchange(&gained_exp_, gained_exp);
	if (state >= 4 && state <= 6) result_until_ = GetTickCount() + 2600;
}

static void parse_marker(char* data, int length) {
	parse_album_stream(data, length);
	parse_card_stream(data, length);
	parse_cooking_stream(data, length);
	suppress_storage_stream(data, length);
	for (int i = 0; i + 8 < length; ++i) {
		if (!bytes_equal((BYTE*)data + i, "HROFISH|", 8)) continue;
		int s, t, d, r, a, level, exp, next_exp, gained_exp;
		int position = i + 8;
		if (parse_number((BYTE*)data, length, &position, &s, '|') &&
			parse_number((BYTE*)data, length, &position, &t, '|') &&
			parse_number((BYTE*)data, length, &position, &d, '|') &&
			parse_number((BYTE*)data, length, &position, &r, '|') &&
			parse_number((BYTE*)data, length, &position, &a, '|') &&
			parse_number((BYTE*)data, length, &position, &level, '|') &&
			parse_number((BYTE*)data, length, &position, &exp, '|') &&
			parse_number((BYTE*)data, length, &position, &next_exp, '|') &&
			parse_number((BYTE*)data, length, &position, &gained_exp, ';')) {
			apply_fishing_state(s, t, d, r, a, level, exp, next_exp, gained_exp);
			for (int j = i; j < position; ++j) data[j] = 0;
		}
	}
	BYTE merged[8352];
	int copy = length > 8192 ? 8192 : length;
	byte_copy(merged, tail_, tail_len_);
	byte_copy(merged + tail_len_, data + length - copy, copy);
	int total = tail_len_ + copy;
	for (int i = 0; i + 8 < total; ++i) {
		if (!bytes_equal(merged + i, "HROFISH|", 8)) continue;
		int s, t, d, r, a, level, exp, next_exp, gained_exp;
		int position = i + 8;
		if (parse_number(merged, total, &position, &s, '|') &&
			parse_number(merged, total, &position, &t, '|') &&
			parse_number(merged, total, &position, &d, '|') &&
			parse_number(merged, total, &position, &r, '|') &&
			parse_number(merged, total, &position, &a, '|') &&
			parse_number(merged, total, &position, &level, '|') &&
			parse_number(merged, total, &position, &exp, '|') &&
			parse_number(merged, total, &position, &next_exp, '|') &&
			parse_number(merged, total, &position, &gained_exp, ';')) {
			apply_fishing_state(s, t, d, r, a, level, exp, next_exp, gained_exp);
		}
	}
	tail_len_ = total > 159 ? 159 : total;
	byte_copy(tail_, merged + total - tail_len_, tail_len_);
	for (int i = 0; i + 8 <= length; ++i)
		if (bytes_equal((BYTE*)data + i, "HROFISH|", 8)) data[i] = 0;
}

/* Card Album records arrive through ZC_NPC_CHAT (0x02c1). Merely
 * replacing HROCARD with a NUL hides the text, but the client still inserts an
 * empty chat line for every card and eventually evicts the visible history.
 * Remove complete Card Album packets from this recv block after parsing them.
 * Fragmented packets keep the old NUL-marker fallback and remain stream-safe. */
static int strip_card_chat_packets(char* data, int length) {
	/* Finish swallowing a Card Album packet that ended in the previous recv.
	 * parse_marker() runs first, so its continuation is still available to the
	 * stream parser before these bytes are removed from the game input. */
	if (card_chat_bytes_to_discard_ > 0) {
		int discarded = length < card_chat_bytes_to_discard_ ? length : card_chat_bytes_to_discard_;
		MoveMemory(data, data + discarded, length - discarded);
		length -= discarded;
		card_chat_bytes_to_discard_ -= discarded;
		if (length == 0 || card_chat_bytes_to_discard_ > 0) return length;
	}

	int offset = 0;
	while (offset + 5 <= length) {
		unsigned char* packet = (unsigned char*)data + offset;
		int packet_length = packet[2] | (packet[3] << 8);
		int marker_offset = -1;
		if (packet[0] == 0xc1 && packet[1] == 0x02)
			marker_offset = 12; /* dispbottom / clif_messagecolor */
		else if (packet[0] == 0x8e && packet[1] == 0x00)
			marker_offset = 4;  /* clif_displaymessage compatibility */
		if (marker_offset >= 0 && packet_length >= marker_offset + 8 &&
			offset + marker_offset < length) {
			static const char card_marker[] = "HROCARD|";
			static const char album_marker[] = "HROALBUM|";
			static const char cooking_marker[] = "HROCOOK|";
			static const char storage_marker[] = "HROSTORAGE|";
			int available = length - offset - marker_offset;
			int compared = available < 8 ? available : 8;
			BOOL is_card_packet = TRUE;
			for (int i = 0; i < compared; ++i) {
				unsigned char value = packet[marker_offset + i];
				/* parse_marker() has already replaced the first H with NUL. */
				if (i == 0 && value == 0) continue;
				if (value != (unsigned char)card_marker[i]) { is_card_packet = FALSE; break; }
			}
			int album_compared = available < 9 ? available : 9;
			BOOL is_album_packet = TRUE;
			for (int i = 0; i < album_compared; ++i) {
				unsigned char value = packet[marker_offset + i];
				if (i == 0 && value == 0) continue;
				if (value != (unsigned char)album_marker[i]) { is_album_packet = FALSE; break; }
			}
			int cooking_compared = available < 8 ? available : 8;
			BOOL is_cooking_packet = TRUE;
			for (int i = 0; i < cooking_compared; ++i) {
				unsigned char value = packet[marker_offset + i];
				if (i == 0 && value == 0) continue;
				if (value != (unsigned char)cooking_marker[i]) { is_cooking_packet = FALSE; break; }
			}
			int storage_compared = available < 11 ? available : 11;
			BOOL is_storage_packet = TRUE;
			for (int i = 0; i < storage_compared; ++i) {
				unsigned char value = packet[marker_offset + i];
				if (i == 0 && value == 0) continue;
				if (value != (unsigned char)storage_marker[i]) { is_storage_packet = FALSE; break; }
			}
			/* HROCARD, HROALBUM and HROSTORAGE share the HRO prefix.  A TCP
			 * receive may end after H, HR or HRO; hide that incomplete prefix
			 * while the stream tails retain it for reconstruction. */
			if (available > 0 && available < 4 &&
				is_card_packet && is_album_packet && is_cooking_packet && is_storage_packet)
				packet[marker_offset] = 0;
			if ((is_card_packet && compared >= 8) ||
				(is_album_packet && album_compared >= 9) ||
				(is_cooking_packet && cooking_compared >= 8) ||
				(is_storage_packet && storage_compared >= 11)) {
				int present = length - offset;
				if (present >= packet_length) {
					MoveMemory(data + offset, data + offset + packet_length,
						length - offset - packet_length);
					length -= packet_length;
					continue;
				}
				card_chat_bytes_to_discard_ = packet_length - present;
				length = offset;
				break;
			}
		}

		/* Packet sizes are variable throughout the RO protocol. We only rely on
		 * the length word when the current header is a known chat packet;
		 * otherwise search for the next complete HROCARD packet signature. */
		++offset;
	}
	return length;
}

static int WSAAPI hooked_recv(SOCKET s, char* buffer, int length, int flags) {
	InterlockedIncrement(&recv_calls_);
	for (;;) {
		int result = real_recv(s, buffer, length, flags);
		if (result <= 0) return result;
		parse_marker(buffer, result);
		result = strip_card_chat_packets(buffer, result);
		if (result > 0) return result;
		/* Returning zero means an orderly socket shutdown to the game client.
		 * A fully consumed HROCARD packet is not a shutdown: keep receiving until
		 * normal data or the real socket status can be returned. */
	}
}

static int WSAAPI hooked_wsarecv(SOCKET socket, LPWSABUF buffers, DWORD count,
	LPDWORD received, LPDWORD flags, LPWSAOVERLAPPED overlapped,
	LPWSAOVERLAPPED_COMPLETION_ROUTINE completion) {
	InterlockedIncrement(&wsarecv_calls_);
	for (;;) {
		int result = real_wsarecv(socket, buffers, count, received, flags, overlapped, completion);
		if (result != 0 || !received || *received == 0 || overlapped)
			return result;

		const DWORD original_length = *received;
		char* contiguous = (char*)HeapAlloc(GetProcessHeap(), 0, original_length);
		if (!contiguous)
			return result; // Never alter normal network data when allocation fails.

		DWORD copied = 0;
		for (DWORD i = 0; i < count && copied < original_length; ++i) {
			DWORD chunk = buffers[i].len < original_length - copied ? buffers[i].len : original_length - copied;
			if (chunk > 0) CopyMemory(contiguous + copied, buffers[i].buf, chunk);
			copied += chunk;
		}
		parse_marker(contiguous, (int)copied);
		DWORD kept = (DWORD)strip_card_chat_packets(contiguous, (int)copied);
		DWORD source = 0;
		for (DWORD i = 0; i < count && source < kept; ++i) {
			DWORD chunk = buffers[i].len < kept - source ? buffers[i].len : kept - source;
			if (chunk > 0) CopyMemory(buffers[i].buf, contiguous + source, chunk);
			source += chunk;
		}
		*received = kept;
		HeapFree(GetProcessHeap(), 0, contiguous);

		if (kept > 0)
			return result;
		// A block containing only internal HRO markers is not a socket close.
		// Keep receiving until normal game data is available, mirroring hooked_recv.
	}
}

static int replace_imports(HMODULE module, void* original, void* replacement) {
	if (!module || !original) return 0;
	BYTE* base = (BYTE*)module;
	IMAGE_DOS_HEADER* dos = (IMAGE_DOS_HEADER*)base;
	if (dos->e_magic != IMAGE_DOS_SIGNATURE) return 0;
	IMAGE_NT_HEADERS32* nt = (IMAGE_NT_HEADERS32*)(base + dos->e_lfanew);
	if (nt->Signature != IMAGE_NT_SIGNATURE) return 0;
	IMAGE_DATA_DIRECTORY directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
	if (!directory.VirtualAddress) return 0;
	int replaced = 0;
	IMAGE_IMPORT_DESCRIPTOR* descriptor = (IMAGE_IMPORT_DESCRIPTOR*)(base + directory.VirtualAddress);
	for (; descriptor->Name; ++descriptor) {
		const char* name = (const char*)(base + descriptor->Name);
		if (_stricmp(name, "ws2_32.dll") && _stricmp(name, "wsock32.dll")) continue;
		IMAGE_THUNK_DATA32* thunk = (IMAGE_THUNK_DATA32*)(base + descriptor->FirstThunk);
		for (; thunk->u1.Function; ++thunk) {
			if ((void*)(uintptr_t)thunk->u1.Function != original) continue;
			DWORD old;
			VirtualProtect(&thunk->u1.Function, sizeof(DWORD), PAGE_READWRITE, &old);
			thunk->u1.Function = (DWORD)(uintptr_t)replacement;
			VirtualProtect(&thunk->u1.Function, sizeof(DWORD), old, &old);
			replaced++;
		}
	}
	return replaced;
}

static void hook_recv(void) {
	HMODULE ws = GetModuleHandleA("ws2_32.dll");
	if (!ws) return;
	real_recv = (RecvFn)GetProcAddress(ws, "recv");
	real_wsarecv = (WSARecvFn)GetProcAddress(ws, "WSARecv");
	int recv_count = 0, wsarecv_count = 0;
	HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetCurrentProcessId());
	if (snapshot != INVALID_HANDLE_VALUE) {
		MODULEENTRY32 entry = {0};
		entry.dwSize = sizeof(entry);
		if (Module32First(snapshot, &entry)) do {
			recv_count += replace_imports(entry.hModule, (void*)real_recv, (void*)hooked_recv);
			wsarecv_count += replace_imports(entry.hModule, (void*)real_wsarecv, (void*)hooked_wsarecv);
		} while (Module32Next(snapshot, &entry));
		CloseHandle(snapshot);
	}
	char message[96];
	wsprintfA(message, "Network hooks installed: recv=%d, WSARecv=%d.", recv_count, wsarecv_count);
	log_line(message);
}

static void hook_raghikari_recv_pointer(void) {
	if (client_pointer_hooked_ || !real_recv) return;
	BYTE* base = (BYTE*)GetModuleHandleA(NULL);
	if (!base) return;
	IMAGE_DOS_HEADER* dos = (IMAGE_DOS_HEADER*)base;
	if (dos->e_magic != IMAGE_DOS_SIGNATURE) return;
	IMAGE_NT_HEADERS32* nt = (IMAGE_NT_HEADERS32*)(base + dos->e_lfanew);
	if (nt->Signature != IMAGE_NT_SIGNATURE ||
		nt->FileHeader.TimeDateStamp != 0x6837c229 ||
		nt->OptionalHeader.SizeOfImage != 0x01ff5000) return;
	RecvFn* client_recv = (RecvFn*)(base + 0x0119e778);
	if (*client_recv != real_recv) return;
	DWORD old;
	if (!VirtualProtect(client_recv, sizeof(*client_recv), PAGE_READWRITE, &old)) return;
	*client_recv = hooked_recv;
	VirtualProtect(client_recv, sizeof(*client_recv), old, &old);
	InterlockedExchange(&client_pointer_hooked_, 1);
	log_line("raghikari game receive pointer hooked.");
}

static BOOL CALLBACK find_window(HWND window, LPARAM output) {
	DWORD pid = 0;
	GetWindowThreadProcessId(window, &pid);
	if (pid == GetCurrentProcessId() && IsWindowVisible(window) && !GetWindow(window, GW_OWNER)) {
		char title[64] = {0};
		GetWindowTextA(window, title, sizeof(title));
		if (title[0]) {
			*(HWND*)output = window;
			return FALSE;
		}
	}
	return TRUE;
}

static void fill_color(HDC dc, RECT rect, COLORREF color) {
	HBRUSH brush = CreateSolidBrush(color);
	FillRect(dc, &rect, brush);
	DeleteObject(brush);
}

static void fill_round(HDC dc, RECT rect, int radius, COLORREF color) {
	HBRUSH brush = CreateSolidBrush(color);
	HPEN pen = CreatePen(PS_SOLID, 1, color);
	HBRUSH old_brush = (HBRUSH)SelectObject(dc, brush);
	HPEN old_pen = (HPEN)SelectObject(dc, pen);
	RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, radius, radius);
	SelectObject(dc, old_pen);
	SelectObject(dc, old_brush);
	DeleteObject(pen);
	DeleteObject(brush);
}

static void draw_panel(HDC dc, RECT area) {
	for (int y = area.top; y < area.bottom; ++y) {
		int shade = (y - area.top) * 16 / (area.bottom - area.top);
		RECT line = {area.left, y, area.right, y + 1};
		fill_color(dc, line, RGB(7 + shade / 2, 22 + shade, 35 + shade));
	}
	HPEN outer = CreatePen(PS_SOLID, 2, RGB(208, 166, 75));
	HPEN old_pen = (HPEN)SelectObject(dc, outer);
	HBRUSH hollow = (HBRUSH)GetStockObject(HOLLOW_BRUSH);
	HBRUSH old_brush = (HBRUSH)SelectObject(dc, hollow);
	RoundRect(dc, 1, 1, area.right - 1, area.bottom - 1, 16, 16);
	SelectObject(dc, old_brush);
	SelectObject(dc, old_pen);
	DeleteObject(outer);
	HPEN inner = CreatePen(PS_SOLID, 1, RGB(54, 122, 145));
	old_pen = (HPEN)SelectObject(dc, inner);
	old_brush = (HBRUSH)SelectObject(dc, hollow);
	RoundRect(dc, 5, 5, area.right - 5, area.bottom - 5, 13, 13);
	SelectObject(dc, old_brush);
	SelectObject(dc, old_pen);
	DeleteObject(inner);
	RECT header = {8, 8, area.right - 8, 43};
	fill_round(dc, header, 10, RGB(13, 43, 59));
	RECT accent = {20, 44, area.right - 20, 47};
	fill_color(dc, accent, RGB(208, 166, 75));
}

static void draw_segment(HDC dc, int left, int right, int top, int bottom,
	float value, float start, float end, COLORREF dim, COLORREF bright) {
	RECT segment = {left, top, right, bottom};
	fill_color(dc, segment, dim);
	if (value > start) {
		float filled = value < end ? value : end;
		int edge = left + (int)((right - left) * (filled - start) / (end - start));
		RECT active = {left, top, edge, bottom};
		fill_color(dc, active, bright);
	}
}

static void draw_tension_meter(HDC dc, float value) {
	if (value < 0) value = 0;
	if (value > 100) value = 100;
	RECT label = {28, 55, 472, 75};
	SetTextColor(dc, RGB(225, 238, 244));
	char tension_label[48];
	const char* behavior = resistance_ == 2 ? "AGGRESSIVE" : resistance_ == 3 ? "POWERFUL" : "CALM";
	wsprintfA(tension_label, "LINE TENSION   %s", behavior);
	DrawTextA(dc, tension_label, -1, &label, DT_LEFT | DT_SINGLELINE);
	char percent[16];
	wsprintfA(percent, "%d%%", (int)(value + .5f));
	DrawTextA(dc, percent, -1, &label, DT_RIGHT | DT_SINGLELINE);
	draw_segment(dc, 28, 139, 78, 105, value, 0, 25, RGB(20, 53, 70), RGB(48, 139, 187));
	draw_segment(dc, 141, 339, 78, 105, value, 25, 70, RGB(20, 65, 50), RGB(42, 187, 105));
	draw_segment(dc, 341, 407, 78, 105, value, 70, 85, RGB(76, 55, 25), RGB(235, 161, 45));
	draw_segment(dc, 409, 472, 78, 105, value, 85, 100, RGB(76, 29, 34), RGB(224, 57, 66));
	RECT frame = {27, 77, 473, 106};
	FrameRect(dc, &frame, (HBRUSH)GetStockObject(WHITE_BRUSH));
	int marker = 28 + (int)(444.0f * value / 100.0f);
	POINT needle[3] = {{marker, 108}, {marker - 6, 116}, {marker + 6, 116}};
	HBRUSH needle_brush = CreateSolidBrush(RGB(246, 238, 194));
	HBRUSH old = (HBRUSH)SelectObject(dc, needle_brush);
	Polygon(dc, needle, 3);
	SelectObject(dc, old);
	DeleteObject(needle_brush);
	RECT slack = {28, 109, 139, 125};
	RECT safe = {141, 109, 339, 125};
	RECT danger = {341, 109, 407, 125};
	RECT breaking = {409, 109, 472, 125};
	SetTextColor(dc, RGB(142, 180, 197));
	DrawTextA(dc, "SLACK", -1, &slack, DT_CENTER | DT_SINGLELINE);
	DrawTextA(dc, "SAFE", -1, &safe, DT_CENTER | DT_SINGLELINE);
	DrawTextA(dc, "DANGER", -1, &danger, DT_CENTER | DT_SINGLELINE);
	DrawTextA(dc, "BREAK", -1, &breaking, DT_CENTER | DT_SINGLELINE);
}

static void draw_fish(HDC dc, int x, int y, COLORREF color) {
	HBRUSH brush = CreateSolidBrush(color);
	HBRUSH old = (HBRUSH)SelectObject(dc, brush);
	Ellipse(dc, x - 10, y - 5, x + 9, y + 6);
	POINT tail[3] = {{x + 7, y}, {x + 16, y - 8}, {x + 16, y + 8}};
	Polygon(dc, tail, 3);
	SelectObject(dc, old);
	DeleteObject(brush);
	SetPixel(dc, x - 6, y - 1, RGB(15, 25, 30));
}

static void draw_header_emblem(HDC dc) {
	// Compact fish-and-hook crest drawn natively so the HUD needs no loose asset.
	HBRUSH body = CreateSolidBrush(RGB(78, 191, 207));
	HBRUSH old_brush = (HBRUSH)SelectObject(dc, body);
	HPEN outline = CreatePen(PS_SOLID, 2, RGB(190, 224, 224));
	HPEN old_pen = (HPEN)SelectObject(dc, outline);
	Ellipse(dc, 26, 16, 58, 35);
	POINT tail[3] = {{55, 25}, {68, 15}, {66, 36}};
	Polygon(dc, tail, 3);
	POINT fin[3] = {{41, 17}, {48, 10}, {51, 19}};
	Polygon(dc, fin, 3);
	HBRUSH eye = CreateSolidBrush(RGB(244, 194, 74));
	SelectObject(dc, eye);
	Ellipse(dc, 32, 20, 38, 26);
	SetPixel(dc, 34, 22, RGB(8, 18, 24));
	SelectObject(dc, old_brush);
	DeleteObject(eye);
	DeleteObject(body);
	SelectObject(dc, old_pen);
	DeleteObject(outline);

	HPEN hook = CreatePen(PS_SOLID, 2, RGB(220, 176, 75));
	old_pen = (HPEN)SelectObject(dc, hook);
	MoveToEx(dc, 73, 10, NULL);
	LineTo(dc, 73, 25);
	Arc(dc, 64, 20, 80, 38, 73, 25, 66, 30);
	MoveToEx(dc, 66, 30, NULL);
	LineTo(dc, 70, 27);
	SelectObject(dc, old_pen);
	DeleteObject(hook);
	HBRUSH bubble = CreateSolidBrush(RGB(89, 190, 220));
	old_brush = (HBRUSH)SelectObject(dc, bubble);
	Ellipse(dc, 18, 13, 22, 17);
	Ellipse(dc, 14, 22, 19, 27);
	SelectObject(dc, old_brush);
	DeleteObject(bubble);
}

static void draw_distance_meter(HDC dc, float raw_distance) {
	float value = raw_distance * (100.0f / 120.0f);
	if (value < 0) value = 0;
	if (value > 100) value = 100;
	RECT label = {28, 128, 472, 148};
	SetTextColor(dc, RGB(225, 238, 244));
	DrawTextA(dc, "FISH DISTANCE", -1, &label, DT_LEFT | DT_SINGLELINE);
	char percent[16];
	wsprintfA(percent, "%d%%", (int)(value + .5f));
	DrawTextA(dc, percent, -1, &label, DT_RIGHT | DT_SINGLELINE);
	RECT water = {28, 151, 472, 181};
	fill_color(dc, water, RGB(23, 60, 82));
	RECT catch_zone = {28, 151, 95, 181};
	fill_color(dc, catch_zone, RGB(38, 113, 83));
	for (int x = 38; x < 472; x += 18) {
		RECT ripple = {x, 174, x + 10, 176};
		fill_color(dc, ripple, RGB(62, 142, 174));
	}
	RECT frame = {27, 150, 473, 182};
	FrameRect(dc, &frame, (HBRUSH)GetStockObject(WHITE_BRUSH));
	HPEN hook_pen = CreatePen(PS_SOLID, 2, RGB(225, 225, 214));
	HPEN old_pen = (HPEN)SelectObject(dc, hook_pen);
	MoveToEx(dc, 43, 152, NULL);
	LineTo(dc, 43, 169);
	Arc(dc, 37, 162, 50, 179, 38, 169, 49, 170);
	SelectObject(dc, old_pen);
	DeleteObject(hook_pen);
	draw_fish(dc, 53 + (int)(400.0f * value / 100.0f), 165, RGB(247, 192, 72));
}

static const char* fish_style(int resistance) {
	if (resistance == 2) return "AGGRESSIVE FISH";
	if (resistance == 3) return "POWERFUL FISH";
	return "CALM FISH";
}

static void draw_progress_header(HDC dc, RECT area) {
	int level = (int)fishing_level_;
	int exp = (int)fishing_exp_;
	int next_exp = (int)fishing_next_exp_;
	if (level < 1) level = 1;
	if (next_exp < 1) next_exp = 250;
	char label[64];
	wsprintfA(label, "FISHING LV. %d     EXP %d / %d", level, exp, next_exp);
	RECT text = {88, 13, area.right - 22, 34};
	SetTextColor(dc, RGB(203, 221, 230));
	DrawTextA(dc, label, -1, &text, DT_RIGHT | DT_SINGLELINE);
	RECT track = {88, 36, area.right - 22, 40};
	fill_color(dc, track, RGB(25, 50, 64));
	int width = track.right - track.left;
	int filled = exp >= next_exp ? width : (int)((int64_t)width * exp / next_exp);
	if (filled < 0) filled = 0;
	RECT progress = {track.left, track.top, track.left + filled, track.bottom};
	fill_color(dc, progress, RGB(70, 186, 211));
}

static LRESULT CALLBACK hud_proc(HWND window, UINT message, WPARAM w, LPARAM l) {
	if (message == WM_ERASEBKGND) return 1;
	if (message != WM_PAINT) return DefWindowProcA(window, message, w, l);
	PAINTSTRUCT paint;
	HDC dc = BeginPaint(window, &paint);
	RECT area;
	GetClientRect(window, &area);
	int state = (int)state_;
	HBITMAP state_background = NULL;
	if (state == 3) {
		if (!fight_background_attempted_) {
			fight_background_attempted_ = 1;
			fight_background_ = load_fishing_background("fight_panel.bmp");
			log_line(fight_background_ ? "Fishing fight background loaded." :
				"Fishing fight background not found; using fallback panel.");
		}
		state_background = fight_background_;
	} else if (state == 1 || state == 2) {
		if (!waiting_background_attempted_) {
			waiting_background_attempted_ = 1;
			waiting_background_ = load_fishing_background("waiting_panel.bmp");
			log_line(waiting_background_ ? "Fishing waiting background loaded." :
				"Fishing waiting background not found; using fallback panel.");
		}
		state_background = waiting_background_;
	} else if (state >= 4 && state <= 6) {
		if (!result_background_attempted_) {
			result_background_attempted_ = 1;
			result_background_ = load_fishing_background("result_panel.bmp");
			log_line(result_background_ ? "Fishing result background loaded." :
				"Fishing result background not found; using fallback panel.");
		}
		state_background = result_background_;
	}
	if (state_background) {
			BITMAP bitmap;
			GetObject(state_background, sizeof(bitmap), &bitmap);
			HDC source = CreateCompatibleDC(dc);
			HBITMAP previous_bitmap = (HBITMAP)SelectObject(source, state_background);
			SetStretchBltMode(dc, HALFTONE);
			StretchBlt(dc, 0, 0, area.right, area.bottom, source, 0, 0,
				bitmap.bmWidth, bitmap.bmHeight, SRCCOPY);
			SelectObject(source, previous_bitmap);
			DeleteDC(source);
	} else draw_panel(dc, area);
	SetBkMode(dc, TRANSPARENT);
	HFONT font = CreateFontA(-15, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
		DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
		DEFAULT_PITCH, "Segoe UI");
	HFONT previous = (HFONT)SelectObject(dc, font);
	draw_header_emblem(dc);
	draw_progress_header(dc, area);
	if (state == 1 || state == 2) {
		RECT status = {18, 48, area.right - 18, area.bottom - 12};
		SetTextColor(dc, state == 2 ? RGB(255, 210, 72) : RGB(207, 228, 237));
		DrawTextA(dc, state == 2 ? "BITE!   SET THE HOOK" : "Waiting for a bite...",
			-1, &status, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
	} else if (state == 3) {
		float tension = shown_tension_;
		draw_tension_meter(dc, tension);
		draw_distance_meter(dc, shown_distance_);
		RECT hint = {28, 185, 472, 202};
		SetTextColor(dc, RGB(151, 183, 196));
		DrawTextA(dc, tension >= 75 ? "Ease the line before it breaks" : shown_distance_ <= 25 ? "Keep it steady inside the catch zone" : "Balance pressure and bring the fish closer", -1, &hint, DT_CENTER | DT_SINGLELINE);
		RECT left = {28, 205, 238, 231};
		RECT right = {262, 205, 472, 231};
		fill_round(dc, left, 8, action_ == 1 ? RGB(31, 139, 174) : RGB(22, 54, 69));
		fill_round(dc, right, 8, action_ == 2 ? RGB(31, 139, 174) : RGB(22, 54, 69));
		SetTextColor(dc, RGB(238, 244, 247));
		DrawTextA(dc, "REEL IN", -1, &left, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
		DrawTextA(dc, "GIVE LINE", -1, &right, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
		RECT action = {238, 205, 262, 231};
		SetTextColor(dc, tension >= 70 ? RGB(247, 104, 80) : RGB(247, 205, 77));
		DrawTextA(dc, tension >= 70 ? "!" : ">", -1, &action, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
	} else if (state >= 4 && state <= 6) {
		RECT result = {18, 51, area.right - 18, 84};
		RECT detail = {18, 82, area.right - 18, 109};
		const char* result_text = state == 4 ? "FISH CAUGHT!" : state == 5 ? "THE FISH ESCAPED" : "LINE SNAPPED";
		SetTextColor(dc, state == 4 ? RGB(91, 230, 151) : RGB(244, 104, 86));
		DrawTextA(dc, result_text, -1, &result, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
		if (state == 4) {
			char quality[48];
			wsprintfA(quality, "QUALITY %d     +%d EXP", (int)action_, (int)gained_exp_);
			SetTextColor(dc, RGB(247, 205, 77));
			DrawTextA(dc, quality, -1, &detail, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
		} else {
			SetTextColor(dc, RGB(190, 211, 220));
			DrawTextA(dc, state == 5 ? "Keep the fish closer." : "Release tension before it reaches red.", -1, &detail, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
		}
	}
	SelectObject(dc, previous);
	DeleteObject(font);
	EndPaint(window, &paint);
	return 0;
}

static DWORD read_u32(const BYTE* p) {
	return (DWORD)p[0] | ((DWORD)p[1] << 8) | ((DWORD)p[2] << 16) | ((DWORD)p[3] << 24);
}

static BOOL read_at(HANDLE file, uint64_t offset, void* output, DWORD length) {
	LARGE_INTEGER position;
	DWORD read = 0;
	position.QuadPart = (LONGLONG)offset;
	return SetFilePointerEx(file, position, NULL, FILE_BEGIN) &&
		ReadFile(file, output, length, &read, NULL) && read == length;
}

static BOOL grf_uncompress(BYTE* output, DWORD output_size, const BYTE* input, DWORD input_size) {
	mz_ulong actual = output_size;
	return mz_uncompress(output, &actual, input, input_size) == MZ_OK && actual == output_size;
}

static HBITMAP bitmap_from_memory(const BYTE* data, DWORD size) {
	if (!data || size < 54 || data[0] != 'B' || data[1] != 'M') return NULL;
	DWORD pixel_offset = read_u32(data + 10);
	DWORD info_size = read_u32(data + 14);
	if (info_size < 40 || pixel_offset >= size || 14 + info_size > size) return NULL;
	const BITMAPINFO* info = (const BITMAPINFO*)(data + 14);
	HDC screen = GetDC(NULL);
	HBITMAP image = CreateDIBitmap(screen, &info->bmiHeader, CBM_INIT, data + pixel_offset, info, DIB_RGB_COLORS);
	ReleaseDC(NULL, screen);
	return image;
}

static BOOL same_grf_path(const BYTE* entry, int length, const char* wanted) {
	int wanted_length = lstrlenA(wanted);
	if (length != wanted_length) return FALSE;
	for (int i = 0; i < length; ++i) {
		BYTE a = entry[i], b = (BYTE)wanted[i];
		if (a == '/') a = '\\';
		if (b == '/') b = '\\';
		if (a >= 'A' && a <= 'Z') a += 'a' - 'A';
		if (b >= 'A' && b <= 'Z') b += 'a' - 'A';
		if (a != b) return FALSE;
	}
	return TRUE;
}

static HBITMAP load_bitmap_from_grf(const char* grf_path, const char* wanted) {
	HANDLE file = CreateFileA(grf_path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
		NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (file == INVALID_HANDLE_VALUE) return NULL;
	BYTE header[46];
	if (!read_at(file, 0, header, 46) || memcmp(header, "Master of Magic", 15) != 0) { CloseHandle(file); return NULL; }
	uint64_t table_offset = (uint64_t)read_u32(header + 30) + 46;
	DWORD file_count = read_u32(header + 38) - read_u32(header + 34) - 7;
	BYTE table_header[8];
	if (!read_at(file, table_offset, table_header, 8)) { CloseHandle(file); return NULL; }
	DWORD packed_size = read_u32(table_header), table_size = read_u32(table_header + 4);
	if (!packed_size || !table_size || packed_size > 64 * 1024 * 1024 || table_size > 128 * 1024 * 1024) { CloseHandle(file); return NULL; }
	BYTE* packed = (BYTE*)HeapAlloc(GetProcessHeap(), 0, packed_size);
	BYTE* table = (BYTE*)HeapAlloc(GetProcessHeap(), 0, table_size);
	if (!packed || !table || !read_at(file, table_offset + 8, packed, packed_size) ||
		!grf_uncompress(table, table_size, packed, packed_size)) {
		if (packed) HeapFree(GetProcessHeap(), 0, packed);
		if (table) HeapFree(GetProcessHeap(), 0, table);
		CloseHandle(file); return NULL;
	}
	HeapFree(GetProcessHeap(), 0, packed);
	HBITMAP result = NULL;
	DWORD p = 0;
	for (DWORD i = 0; i < file_count && p < table_size; ++i) {
		DWORD name_start = p;
		while (p < table_size && table[p]) ++p;
		if (p >= table_size || p + 18 >= table_size) break;
		int name_length = (int)(p - name_start);
		++p;
		DWORD compressed = read_u32(table + p); p += 4;
		DWORD aligned = read_u32(table + p); p += 4;
		DWORD real_size = read_u32(table + p); p += 4;
		BYTE type = table[p++];
		uint64_t data_offset = (uint64_t)read_u32(table + p) + 46; p += 4;
		if (!(type & 1) || !same_grf_path(table + name_start, name_length, wanted)) continue;
		/* Collection BMPs are ordinary compressed GRF entries. Encrypted entries are not accepted. */
		if ((type & 6) || !aligned || !real_size || aligned > 16 * 1024 * 1024 || real_size > 32 * 1024 * 1024) break;
		BYTE* source = (BYTE*)HeapAlloc(GetProcessHeap(), 0, aligned);
		BYTE* decoded = (BYTE*)HeapAlloc(GetProcessHeap(), 0, real_size);
		if (source && decoded && read_at(file, data_offset, source, aligned)) {
			BOOL ok = real_size == compressed ? (memcpy(decoded, source, real_size), TRUE) :
				grf_uncompress(decoded, real_size, source, compressed);
			if (ok) result = bitmap_from_memory(decoded, real_size);
		}
		if (source) HeapFree(GetProcessHeap(), 0, source);
		if (decoded) HeapFree(GetProcessHeap(), 0, decoded);
		break;
	}
	HeapFree(GetProcessHeap(), 0, table);
	CloseHandle(file);
	return result;
}

static BOOL load_file_from_grf(const char* grf_path, const char* wanted, BYTE** output, DWORD* output_size) {
	*output = NULL; *output_size = 0;
	HANDLE file = CreateFileA(grf_path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
		NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (file == INVALID_HANDLE_VALUE) return FALSE;
	BYTE header[46];
	if (!read_at(file, 0, header, 46) || memcmp(header, "Master of Magic", 15) != 0) { CloseHandle(file); return FALSE; }
	uint64_t table_offset = (uint64_t)read_u32(header + 30) + 46;
	DWORD file_count = read_u32(header + 38) - read_u32(header + 34) - 7;
	BYTE table_header[8];
	if (!read_at(file, table_offset, table_header, 8)) { CloseHandle(file); return FALSE; }
	DWORD packed_size = read_u32(table_header), table_size = read_u32(table_header + 4);
	if (!packed_size || !table_size || packed_size > 64 * 1024 * 1024 || table_size > 128 * 1024 * 1024) { CloseHandle(file); return FALSE; }
	BYTE* packed = (BYTE*)HeapAlloc(GetProcessHeap(), 0, packed_size);
	BYTE* table = (BYTE*)HeapAlloc(GetProcessHeap(), 0, table_size);
	if (!packed || !table || !read_at(file, table_offset + 8, packed, packed_size) ||
		!grf_uncompress(table, table_size, packed, packed_size)) {
		if (packed) HeapFree(GetProcessHeap(), 0, packed);
		if (table) HeapFree(GetProcessHeap(), 0, table);
		CloseHandle(file); return FALSE;
	}
	HeapFree(GetProcessHeap(), 0, packed);
	BOOL result = FALSE;
	DWORD p = 0;
	for (DWORD i = 0; i < file_count && p < table_size; ++i) {
		DWORD name_start = p;
		while (p < table_size && table[p]) ++p;
		if (p >= table_size || p + 18 >= table_size) break;
		int name_length = (int)(p - name_start); ++p;
		DWORD compressed = read_u32(table + p); p += 4;
		DWORD aligned = read_u32(table + p); p += 4;
		DWORD real_size = read_u32(table + p); p += 4;
		BYTE type = table[p++];
		uint64_t data_offset = (uint64_t)read_u32(table + p) + 46; p += 4;
		if (!(type & 1) || !same_grf_path(table + name_start, name_length, wanted)) continue;
		if ((type & 6) || !aligned || !real_size || aligned > 32 * 1024 * 1024 || real_size > 64 * 1024 * 1024) break;
		BYTE* source = (BYTE*)HeapAlloc(GetProcessHeap(), 0, aligned);
		BYTE* decoded = (BYTE*)HeapAlloc(GetProcessHeap(), 0, real_size + 1);
		if (source && decoded && read_at(file, data_offset, source, aligned)) {
			BOOL ok = real_size == compressed ? (memcpy(decoded, source, real_size), TRUE) :
				grf_uncompress(decoded, real_size, source, compressed);
			if (ok) { decoded[real_size] = 0; *output = decoded; *output_size = real_size; decoded = NULL; result = TRUE; }
		}
		if (source) HeapFree(GetProcessHeap(), 0, source);
		if (decoded) HeapFree(GetProcessHeap(), 0, decoded);
		break;
	}
	HeapFree(GetProcessHeap(), 0, table);
	CloseHandle(file);
	return result;
}

static BOOL load_client_file(const char* wanted, BYTE** output, DWORD* output_size) {
	char root[MAX_PATH], path[MAX_PATH], grf_name[128];
	GetModuleFileNameA(NULL, root, MAX_PATH);
	char* slash = strrchr(root, '\\');
	if (slash) *(slash + 1) = 0;
	lstrcpynA(path, root, MAX_PATH); lstrcatA(path, wanted);
	HANDLE loose = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
		NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (loose != INVALID_HANDLE_VALUE) {
		DWORD size = GetFileSize(loose, NULL), read = 0;
		BYTE* data = size && size < 64 * 1024 * 1024 ? (BYTE*)HeapAlloc(GetProcessHeap(), 0, size + 1) : NULL;
		if (data && ReadFile(loose, data, size, &read, NULL) && read == size) {
			data[size] = 0; CloseHandle(loose); *output = data; *output_size = size; return TRUE;
		}
		if (data) HeapFree(GetProcessHeap(), 0, data);
		CloseHandle(loose);
	}
	lstrcpynA(path, root, MAX_PATH); lstrcatA(path, "DATA.ini");
	for (int index = 0; index < 32; ++index) {
		char key[16]; wsprintfA(key, "%d", index);
		if (!GetPrivateProfileStringA("Data", key, "", grf_name, sizeof(grf_name), path)) break;
		char archive[MAX_PATH]; lstrcpynA(archive, root, MAX_PATH); lstrcatA(archive, grf_name);
		if (load_file_from_grf(archive, wanted, output, output_size)) return TRUE;
	}
	return FALSE;
}

static BOOL find_card_resource(int id, char* output, int output_length) {
	if (!card_name_table_attempted_) {
		card_name_table_attempted_ = 1;
		static const char* candidates[] = {
			"data\\num2cardillustnametable.txt",
			"data\\luafiles514\\lua files\\datainfo\\num2cardillustnametable.txt",
			"data\\luafiles514\\lua files\\datainfo\\num2cardillustnametable.lua"
		};
		for (int i = 0; i < 3 && !card_name_table_; ++i)
			load_client_file(candidates[i], &card_name_table_, &card_name_table_size_);
		log_line(card_name_table_ ? "Card illustration table loaded." : "Card illustration table was not found.");
	}
	if (!card_name_table_) return FALSE;
	const char* data = (const char*)card_name_table_;
	DWORD p = 0;
	while (p < card_name_table_size_) {
		while (p < card_name_table_size_ && (data[p] == '\r' || data[p] == '\n' || data[p] == ' ' || data[p] == '\t')) ++p;
		DWORD line_start = p;
		while (p < card_name_table_size_ && data[p] != '\r' && data[p] != '\n') ++p;
		DWORD line_end = p;
		const char* cursor = data + line_start;
		const char* end = data + line_end;
		while (cursor < end && (*cursor == '[' || *cursor == ' ' || *cursor == '\t')) ++cursor;
		int found_id = 0;
		while (cursor < end && *cursor >= '0' && *cursor <= '9') { found_id = found_id * 10 + *cursor - '0'; ++cursor; }
		if (found_id != id) continue;
		const char* name_start = NULL; const char* name_end = NULL;
		while (cursor < end && *cursor != '#' && *cursor != '\"') ++cursor;
		if (cursor < end && *cursor == '#') {
			name_start = ++cursor; while (cursor < end && *cursor != '#') ++cursor; name_end = cursor;
		} else if (cursor < end && *cursor == '\"') {
			name_start = ++cursor; while (cursor < end && *cursor != '\"') ++cursor; name_end = cursor;
		}
		if (name_start && name_end > name_start) {
			int length = (int)(name_end - name_start); if (length >= output_length) length = output_length - 1;
			byte_copy(output, name_start, length); output[length] = 0; return TRUE;
		}
	}
	return FALSE;
}

static HBITMAP load_card_image(int id) {
	char resource[128];
	if (!find_card_resource(id, resource, sizeof(resource))) return NULL;
	char wanted[320], root[MAX_PATH], path[MAX_PATH], grf_name[128];
	lstrcpyA(wanted, "data\\texture\\\xC0\xAF\xC0\xFA\xC0\xCE\xC5\xCD\xC6\xE4\xC0\xCC\xBD\xBA\\cardbmp\\");
	lstrcatA(wanted, resource); lstrcatA(wanted, ".bmp");
	GetModuleFileNameA(NULL, root, MAX_PATH); char* slash = strrchr(root, '\\'); if (slash) *(slash + 1) = 0;
	lstrcpynA(path, root, MAX_PATH); lstrcatA(path, wanted);
	HBITMAP loose = (HBITMAP)LoadImageA(NULL, path, IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE | LR_CREATEDIBSECTION);
	if (loose) return loose;
	lstrcpynA(path, root, MAX_PATH); lstrcatA(path, "DATA.ini");
	for (int index = 0; index < 32; ++index) {
		char key[16]; wsprintfA(key, "%d", index);
		if (!GetPrivateProfileStringA("Data", key, "", grf_name, sizeof(grf_name), path)) break;
		char archive[MAX_PATH]; lstrcpynA(archive, root, MAX_PATH); lstrcatA(archive, grf_name);
		HBITMAP image = load_bitmap_from_grf(archive, wanted); if (image) return image;
	}
	return NULL;
}

static HBITMAP load_card_album_background(void) {
	char root[MAX_PATH], path[MAX_PATH], data_ini[MAX_PATH], grf_name[128];
	char wanted[256];
	lstrcpyA(wanted, "data\\texture\\\xC0\xAF\xC0\xFA\xC0\xCE\xC5\xCD\xC6\xE4\xC0\xCC\xBD\xBA\\hro_card_album\\card_album_book.bmp");
	GetModuleFileNameA(NULL, root, MAX_PATH);
	char* slash = strrchr(root, '\\'); if (slash) *(slash + 1) = 0;
	lstrcpynA(path, root, MAX_PATH);
	lstrcatA(path, wanted);
	HBITMAP loose = (HBITMAP)LoadImageA(NULL, path, IMAGE_BITMAP, 0, 0,
		LR_LOADFROMFILE | LR_CREATEDIBSECTION);
	if (loose) return loose;
	lstrcpynA(data_ini, root, MAX_PATH); lstrcatA(data_ini, "DATA.ini");
	for (int index = 0; index < 32; ++index) {
		char key[16]; wsprintfA(key, "%d", index);
		if (!GetPrivateProfileStringA("Data", key, "", grf_name, sizeof(grf_name), data_ini)) break;
		char archive[MAX_PATH]; lstrcpynA(archive, root, MAX_PATH); lstrcatA(archive, grf_name);
		HBITMAP image = load_bitmap_from_grf(archive, wanted);
		if (image) return image;
	}
	return NULL;
}

static BOOL contains_text_i(const char* text, const char* wanted) {
	if (!wanted || !wanted[0]) return TRUE;
	if (!text) return FALSE;
	int wanted_length = lstrlenA(wanted);
	for (const char* start = text; *start; ++start) {
		int i = 0;
		while (i < wanted_length && start[i]) {
			char a = start[i], b = wanted[i];
			if (a >= 'A' && a <= 'Z') a += 'a' - 'A';
			if (b >= 'A' && b <= 'Z') b += 'a' - 'A';
			if (a != b) break;
			++i;
		}
		if (i == wanted_length) return TRUE;
	}
	return FALSE;
}

static BOOL card_is_visible(int index) {
	if (index < 0 || index >= card_count_) return FALSE;
	BOOL discovered = card_entries_[index].registered_at[0] != 0;
	if (card_filter_ == 1 && !discovered) return FALSE;
	if (card_filter_ == 2 && discovered) return FALSE;
	if (card_search_[0]) {
		char id[16]; wsprintfA(id, "%d", card_entries_[index].id);
		if (!contains_text_i(card_entries_[index].name, card_search_) &&
			!contains_text_i(id, card_search_)) return FALSE;
	}
	return TRUE;
}

static int visible_card_count(void) {
	int count = 0;
	for (int i = 0; i < card_count_; ++i) if (card_is_visible(i)) ++count;
	return count;
}

static int visible_card_index(int position) {
	for (int i = 0; i < card_count_; ++i)
		if (card_is_visible(i) && position-- == 0) return i;
	return -1;
}

static void reset_card_page(void) {
	card_page_ = 0;
	card_selected_ = visible_card_index(0);
	if (card_selected_ < 0) card_selected_ = 0;
}

static HBITMAP load_collection_image(const char* resource_name) {
	if (!resource_name || !resource_name[0]) return NULL;
	WCHAR korean_name[64];
	char cp949_name[128], wanted[256], root[MAX_PATH], path[MAX_PATH], grf_name[128];
	if (!MultiByteToWideChar(CP_UTF8, 0, resource_name, -1, korean_name, 64)) return NULL;
	if (!WideCharToMultiByte(949, 0, korean_name, -1, cp949_name, 128, NULL, NULL)) return NULL;
	lstrcpyA(wanted, "data\\texture\\\xC0\xAF\xC0\xFA\xC0\xCE\xC5\xCD\xC6\xE4\xC0\xCC\xBD\xBA\\collection\\");
	lstrcatA(wanted, cp949_name);
	lstrcatA(wanted, ".bmp");
	GetModuleFileNameA(NULL, root, MAX_PATH);
	char* slash = strrchr(root, '\\');
	if (slash) *(slash + 1) = 0;
	lstrcpynA(path, root, MAX_PATH);
	lstrcatA(path, wanted);
	HBITMAP loose = (HBITMAP)LoadImageA(NULL, path, IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE | LR_CREATEDIBSECTION);
	if (loose) return loose;
	lstrcpynA(path, root, MAX_PATH);
	lstrcatA(path, "DATA.ini");
	for (int index = 0; index < 32; ++index) {
		char key[16]; wsprintfA(key, "%d", index);
		if (!GetPrivateProfileStringA("Data", key, "", grf_name, sizeof(grf_name), path)) break;
		char archive[MAX_PATH]; lstrcpynA(archive, root, MAX_PATH); lstrcatA(archive, grf_name);
		HBITMAP image = load_bitmap_from_grf(archive, wanted);
		if (image) return image;
	}
	return NULL;
}

static BOOL find_item_resource(int id, char* output, int output_length) {
	if (!item_info_table_attempted_) {
		item_info_table_attempted_ = 1;
		static const char* candidates[] = {
			"SystemEN\\LuaFiles514\\itemInfo.lua",
			"SystemEN\\LuaFiles514\\itemInfo.lub",
			"SystemEN\\LuaFiles514\\Lua Files\\Datainfo\\itemInfo.lua",
			"SystemEN\\LuaFiles514\\Lua Files\\Datainfo\\itemInfo.lub",
			"SystemEN\\LuaFiles514\\Lua Files\\itemInfo.lua",
			"SystemEN\\LuaFiles514\\Lua Files\\itemInfo.lub",
			"SystemEN\\itemInfo.lua",
			"SystemEN\\itemInfo.lub",
			"data\\luafiles514\\lua files\\datainfo\\iteminfo.lua",
			"data\\luafiles514\\lua files\\datainfo\\iteminfo.lub",
			"data\\luafiles514\\lua files\\iteminfo.lua",
			"data\\luafiles514\\lua files\\iteminfo.lub",
			"data\\lua files\\datainfo\\iteminfo.lua",
			"data\\lua files\\datainfo\\iteminfo.lub"
		};
		for (int i = 0; i < 14 && !item_info_table_; ++i) {
			BYTE* candidate = NULL; DWORD candidate_size = 0;
			if (!load_client_file(candidates[i], &candidate, &candidate_size)) continue;
			/* A compiled .lub may exist before a readable Lua source in another
			 * client folder. Keep searching unless this candidate really contains
			 * the itemInfo field names needed by the recipe book. */
			if (strstr((const char*)candidate, "identifiedDescriptionName") ||
				strstr((const char*)candidate, "identifiedResourceName")) {
				item_info_table_ = candidate; item_info_table_size_ = candidate_size;
			} else HeapFree(GetProcessHeap(), 0, candidate);
		}
		log_line(item_info_table_ ? "itemInfo table loaded for cooking icons." :
			"itemInfo table was not found; cooking icons will use Aegis names.");
	}
	if (!item_info_table_ || output_length < 2) return FALSE;
	const char* data = (const char*)item_info_table_;
	DWORD position = 0;
	while (position < item_info_table_size_) {
		if (data[position] != '[') { ++position; continue; }
		DWORD cursor = position + 1; int found_id = 0; BOOL has_digits = FALSE;
		while (cursor < item_info_table_size_ && (data[cursor] == ' ' || data[cursor] == '\t')) ++cursor;
		char id_quote = 0;
		if (cursor < item_info_table_size_ && (data[cursor] == '"' || data[cursor] == '\'')) id_quote = data[cursor++];
		while (cursor < item_info_table_size_ && data[cursor] >= '0' && data[cursor] <= '9') {
			has_digits = TRUE; found_id = found_id * 10 + data[cursor] - '0'; ++cursor;
		}
		if (id_quote && cursor < item_info_table_size_ && data[cursor] == id_quote) ++cursor;
		while (cursor < item_info_table_size_ && (data[cursor] == ' ' || data[cursor] == '\t')) ++cursor;
		if (!has_digits || cursor >= item_info_table_size_ || data[cursor] != ']') { ++position; continue; }
		if (found_id != id) { position = cursor + 1; continue; }
		DWORD block_end = cursor + 1;
		while (block_end < item_info_table_size_ && block_end < cursor + 8192) {
			if (data[block_end] == '[' && (block_end == 0 || data[block_end - 1] == '\n')) break;
			++block_end;
		}
		static const char field[] = "identifiedResourceName";
		for (DWORD field_pos = cursor + 1; field_pos + sizeof(field) - 1 < block_end; ++field_pos) {
			if (memcmp(data + field_pos, field, sizeof(field) - 1) != 0) continue;
			if (field_pos > cursor + 1) {
				char previous = data[field_pos - 1];
				if ((previous >= 'A' && previous <= 'Z') || (previous >= 'a' && previous <= 'z') || previous == '_') continue;
			}
			DWORD value = field_pos + sizeof(field) - 1;
			while (value < block_end && data[value] != '"' && data[value] != '\'') ++value;
			if (value >= block_end) return FALSE;
			char quote = data[value++]; DWORD end = value;
			while (end < block_end && data[end] != quote) ++end;
			if (end <= value || end >= block_end) return FALSE;
			int length = (int)(end - value); if (length >= output_length) length = output_length - 1;
			byte_copy(output, data + value, length); output[length] = 0; return TRUE;
		}
		return FALSE;
	}
	return FALSE;
}

static BOOL find_item_description_lua(int id, char* output, int output_length) {
	if (!english_item_info_table_attempted_) {
		english_item_info_table_attempted_ = 1;
		/* Recipe descriptions must come from the client's English System folder.
		 * The generic data GRFs commonly contain the original Korean table. */
		if (load_client_file("SystemEN\\LuaFiles514\\itemInfo.lua",
			&english_item_info_table_, &english_item_info_table_size_))
			log_line("English SystemEN itemInfo loaded for recipe descriptions.");
		else
			log_line("English SystemEN itemInfo was not found for recipe descriptions.");
	}
	if (!english_item_info_table_ || output_length < 2) return FALSE;
	const char* data = (const char*)english_item_info_table_;
	DWORD position = 0;
	while (position < english_item_info_table_size_) {
		if (data[position] != '[') { ++position; continue; }
		DWORD cursor = position + 1; int found_id = 0; BOOL has_digits = FALSE;
		while (cursor < english_item_info_table_size_ && (data[cursor] == ' ' || data[cursor] == '\t')) ++cursor;
		char id_quote = 0;
		if (cursor < english_item_info_table_size_ && (data[cursor] == '"' || data[cursor] == '\'')) id_quote = data[cursor++];
		while (cursor < english_item_info_table_size_ && data[cursor] >= '0' && data[cursor] <= '9') {
			has_digits = TRUE; found_id = found_id * 10 + data[cursor] - '0'; ++cursor;
		}
		if (id_quote && cursor < english_item_info_table_size_ && data[cursor] == id_quote) ++cursor;
		while (cursor < english_item_info_table_size_ && (data[cursor] == ' ' || data[cursor] == '\t')) ++cursor;
		if (!has_digits || cursor >= english_item_info_table_size_ || data[cursor] != ']') { ++position; continue; }
		if (found_id != id) { position = cursor + 1; continue; }
		DWORD block_end = cursor + 1;
		while (block_end < english_item_info_table_size_ && block_end < cursor + 16384) {
			if (data[block_end] == '[' && block_end > 0 && data[block_end - 1] == '\n') break;
			++block_end;
		}
		static const char field[] = "identifiedDescriptionName";
		DWORD field_position = cursor + 1;
		while (field_position + sizeof(field) - 1 < block_end) {
			if (memcmp(data + field_position, field, sizeof(field) - 1) == 0) {
				char previous = field_position > cursor + 1 ? data[field_position - 1] : 0;
				char following = data[field_position + sizeof(field) - 1];
				BOOL previous_is_identifier = (previous >= 'A' && previous <= 'Z') ||
					(previous >= 'a' && previous <= 'z') || previous == '_';
				BOOL following_is_identifier = (following >= 'A' && following <= 'Z') ||
					(following >= 'a' && following <= 'z') || following == '_';
				if (!previous_is_identifier && !following_is_identifier) break;
			}
			++field_position;
		}
		if (field_position + sizeof(field) - 1 >= block_end) return FALSE;
		DWORD description_end = field_position + sizeof(field) - 1;
		while (description_end < block_end && data[description_end] != '}') ++description_end;
		int written = 0;
		for (DWORD p = field_position + sizeof(field) - 1; p < description_end && written + 1 < output_length; ++p) {
			if (data[p] != '"' && data[p] != '\'') continue;
			char quote = data[p++];
			if (written > 0 && written + 1 < output_length) output[written++] = '\n';
			while (p < description_end && data[p] != quote && written + 1 < output_length) {
				if (data[p] == '^' && p + 6 < description_end) {
					BOOL color = TRUE;
					for (int digit = 1; digit <= 6; ++digit) {
						char value = data[p + digit];
						if (!((value >= '0' && value <= '9') || (value >= 'A' && value <= 'F') ||
							(value >= 'a' && value <= 'f'))) { color = FALSE; break; }
					}
					if (color) { p += 7; continue; }
				}
				if (data[p] == '\\' && p + 1 < description_end) {
					if (data[p + 1] == 'n') { output[written++] = '\n'; p += 2; continue; }
					if (data[p + 1] == '"' || data[p + 1] == '\\') ++p;
				}
				output[written++] = data[p++];
			}
		}
		while (written > 0 && (output[written - 1] == '\n' || output[written - 1] == ' ')) --written;
		output[written] = 0;
		return written > 0;
	}
	return FALSE;
}

static BOOL find_legacy_item_description(int id, char* output, int output_length) {
	if (!item_description_table_attempted_) {
		item_description_table_attempted_ = 1;
		static const char* candidates[] = {
			"SystemEN\\idnum2itemdesctable.txt",
			"SystemEN\\LuaFiles514\\idnum2itemdesctable.txt"
		};
		for (int i = 0; i < 2 && !item_description_table_; ++i)
			load_client_file(candidates[i], &item_description_table_, &item_description_table_size_);
	}
	if (!item_description_table_) return FALSE;
	const char* data = (const char*)item_description_table_;
	DWORD p = 0;
	while (p < item_description_table_size_) {
		while (p < item_description_table_size_ && (data[p] == '\r' || data[p] == '\n' || data[p] == ' ' || data[p] == '\t')) ++p;
		int found_id = 0; BOOL digits = FALSE;
		while (p < item_description_table_size_ && data[p] >= '0' && data[p] <= '9') {
			digits = TRUE; found_id = found_id * 10 + data[p] - '0'; ++p;
		}
		if (!digits || p >= item_description_table_size_ || data[p] != '#') {
			while (p < item_description_table_size_ && data[p] != '\n') ++p;
			continue;
		}
		DWORD start = ++p, end = start;
		while (end < item_description_table_size_ && data[end] != '#') ++end;
		if (found_id == id && end > start) {
			int written = 0;
			for (DWORD source = start; source < end && written + 1 < output_length; ++source) {
				if (data[source] == '^' && source + 6 < end) {
					BOOL color = TRUE;
					for (int digit = 1; digit <= 6; ++digit) {
						char value = data[source + digit];
						if (!((value >= '0' && value <= '9') || (value >= 'A' && value <= 'F') ||
							(value >= 'a' && value <= 'f'))) { color = FALSE; break; }
					}
					if (color) { source += 6; continue; }
				}
				output[written++] = data[source] == '\t' ? ' ' : data[source];
			}
			output[written] = 0; return written > 0;
		}
		p = end < item_description_table_size_ ? end + 1 : end;
	}
	return FALSE;
}

static BOOL find_item_description(int id, char* output, int output_length) {
	return find_item_description_lua(id, output, output_length) ||
		find_legacy_item_description(id, output, output_length);
}

static HBITMAP load_item_image(int item_id, const char* fallback_resource) {
	char resource_utf8[128] = {0}, resource[256] = {0};
	if (!find_item_resource(item_id, resource_utf8, sizeof(resource_utf8)))
		lstrcpynA(resource_utf8, fallback_resource ? fallback_resource : "", sizeof(resource_utf8));
	if (!resource_utf8[0]) return NULL;
	WCHAR wide_resource[128];
	if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, resource_utf8, -1, wide_resource, 128) &&
		WideCharToMultiByte(949, 0, wide_resource, -1, resource, sizeof(resource), NULL, NULL) == 0)
		lstrcpynA(resource, resource_utf8, sizeof(resource));
	if (!resource[0]) lstrcpynA(resource, resource_utf8, sizeof(resource));
	char wanted[320], root[MAX_PATH], path[MAX_PATH], grf_name[128];
	lstrcpyA(wanted, "data\\texture\\\xC0\xAF\xC0\xFA\xC0\xCE\xC5\xCD\xC6\xE4\xC0\xCC\xBD\xBA\\item\\");
	lstrcatA(wanted, resource);
	lstrcatA(wanted, ".bmp");
	GetModuleFileNameA(NULL, root, MAX_PATH);
	char* slash = strrchr(root, '\\');
	if (slash) *(slash + 1) = 0;
	lstrcpynA(path, root, MAX_PATH); lstrcatA(path, wanted);
	HBITMAP loose = (HBITMAP)LoadImageA(NULL, path, IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE | LR_CREATEDIBSECTION);
	if (loose) return loose;
	lstrcpynA(path, root, MAX_PATH); lstrcatA(path, "DATA.ini");
	for (int index = 0; index < 32; ++index) {
		char key[16]; wsprintfA(key, "%d", index);
		if (!GetPrivateProfileStringA("Data", key, "", grf_name, sizeof(grf_name), path)) break;
		char archive[MAX_PATH]; lstrcpynA(archive, root, MAX_PATH); lstrcatA(archive, grf_name);
		HBITMAP image = load_bitmap_from_grf(archive, wanted);
		if (image) return image;
	}
	return NULL;
}

static void draw_item_image(HDC dc, RECT area, HBITMAP* image, int* attempted, int item_id, const char* resource_name) {
	if (!*attempted && cooking_image_load_budget_ > 0) {
		--cooking_image_load_budget_;
		*attempted = 1;
		*image = load_item_image(item_id, resource_name);
	}
	if (!*image) {
		RECT placeholder = {area.left + 5, area.top + 5, area.right - 5, area.bottom - 5};
		fill_round(dc, placeholder, 7, RGB(214, 199, 165));
		SetTextColor(dc, RGB(112, 87, 56));
		DrawTextA(dc, "?", -1, &placeholder, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
		return;
	}
	BITMAP bitmap; GetObject(*image, sizeof(bitmap), &bitmap);
	int maximum_width = area.right - area.left, maximum_height = area.bottom - area.top;
	int width = bitmap.bmWidth, height = bitmap.bmHeight;
	/* Item BMPs are deliberately small. Never enlarge them: scaling a 24x24
	 * icon to the full recipe-row slot produces the blocky result that the
	 * collection artwork is intended to avoid. Only shrink oversized assets. */
	if (width > maximum_width) { height = height * maximum_width / width; width = maximum_width; }
	if (height > maximum_height) { width = width * maximum_height / height; height = maximum_height; }
	int x = area.left + (maximum_width - width) / 2;
	int y = area.top + (maximum_height - height) / 2;
	HDC source = CreateCompatibleDC(dc); HBITMAP previous = (HBITMAP)SelectObject(source, *image);
	TransparentBlt(dc, x, y, width, height, source, 0, 0, bitmap.bmWidth, bitmap.bmHeight, RGB(255, 0, 255));
	SelectObject(source, previous); DeleteDC(source);
}

static void draw_recipe_collection(HDC dc, RECT area, CookingRecipe* recipe) {
	if (!recipe->collection_attempted && cooking_image_load_budget_ > 0) {
		--cooking_image_load_budget_;
		recipe->collection_attempted = 1;
		char resource[128] = {0};
		if (!find_item_resource(recipe->product_id, resource, sizeof(resource)))
			lstrcpynA(resource, recipe->resource_name, sizeof(resource));
		recipe->collection_image = load_collection_image(resource);
	}
	if (!recipe->collection_image) {
		draw_item_image(dc, area, &recipe->image, &recipe->image_attempted,
			recipe->product_id, recipe->resource_name);
		return;
	}
	BITMAP bitmap; GetObject(recipe->collection_image, sizeof(bitmap), &bitmap);
	int maximum_width = area.right - area.left;
	int maximum_height = area.bottom - area.top;
	int width = bitmap.bmWidth, height = bitmap.bmHeight;
	if (width > maximum_width) { height = height * maximum_width / width; width = maximum_width; }
	if (height > maximum_height) { width = width * maximum_height / height; height = maximum_height; }
	int x = area.left + (maximum_width - width) / 2;
	int y = area.top + (maximum_height - height) / 2;
	HDC source = CreateCompatibleDC(dc);
	HBITMAP previous = (HBITMAP)SelectObject(source, recipe->collection_image);
	TransparentBlt(dc, x, y, width, height, source, 0, 0, bitmap.bmWidth, bitmap.bmHeight, RGB(255, 0, 255));
	SelectObject(source, previous); DeleteDC(source);
}

static HBITMAP load_album_background(void) {
	char wanted[256], root[MAX_PATH], path[MAX_PATH], grf_name[128];
	lstrcpyA(wanted, "data\\texture\\\xC0\xAF\xC0\xFA\xC0\xCE\xC5\xCD\xC6\xE4\xC0\xCC\xBD\xBA\\hro_fishing\\album_book.bmp");
	GetModuleFileNameA(NULL, root, MAX_PATH);
	char* slash = strrchr(root, '\\');
	if (slash) *(slash + 1) = 0;
	lstrcpynA(path, root, MAX_PATH);
	lstrcatA(path, wanted);
	HBITMAP loose = (HBITMAP)LoadImageA(NULL, path, IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE | LR_CREATEDIBSECTION);
	if (loose) return loose;
	lstrcpynA(path, root, MAX_PATH);
	lstrcatA(path, "DATA.ini");
	for (int index = 0; index < 32; ++index) {
		char key[16]; wsprintfA(key, "%d", index);
		if (!GetPrivateProfileStringA("Data", key, "", grf_name, sizeof(grf_name), path)) break;
		char archive[MAX_PATH]; lstrcpynA(archive, root, MAX_PATH); lstrcatA(archive, grf_name);
		HBITMAP image = load_bitmap_from_grf(archive, wanted);
		if (image) return image;
	}
	return NULL;
}

static HBITMAP load_fishing_background(const char* filename) {
	char wanted[256], root[MAX_PATH], path[MAX_PATH], grf_name[128];
	lstrcpyA(wanted, "data\\texture\\\xC0\xAF\xC0\xFA\xC0\xCE\xC5\xCD\xC6\xE4\xC0\xCC\xBD\xBA\\hro_fishing\\");
	lstrcatA(wanted, filename);
	GetModuleFileNameA(NULL, root, MAX_PATH);
	char* slash = strrchr(root, '\\');
	if (slash) *(slash + 1) = 0;
	lstrcpynA(path, root, MAX_PATH);
	lstrcatA(path, wanted);
	HBITMAP loose = (HBITMAP)LoadImageA(NULL, path, IMAGE_BITMAP, 0, 0,
		LR_LOADFROMFILE | LR_CREATEDIBSECTION);
	if (loose) return loose;
	lstrcpynA(path, root, MAX_PATH);
	lstrcatA(path, "DATA.ini");
	for (int index = 0; index < 32; ++index) {
		char key[16]; wsprintfA(key, "%d", index);
		if (!GetPrivateProfileStringA("Data", key, "", grf_name, sizeof(grf_name), path)) break;
		char archive[MAX_PATH];
		lstrcpynA(archive, root, MAX_PATH);
		lstrcatA(archive, grf_name);
		HBITMAP image = load_bitmap_from_grf(archive, wanted);
		if (image) return image;
	}
	return NULL;
}

static HBITMAP create_locked_collection_image(HBITMAP source) {
	BITMAP bitmap;
	if (!source || !GetObject(source, sizeof(bitmap), &bitmap) || bitmap.bmWidth <= 0 || bitmap.bmHeight <= 0)
		return NULL;
	BITMAPINFO info;
	ZeroMemory(&info, sizeof(info));
	info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	info.bmiHeader.biWidth = bitmap.bmWidth;
	info.bmiHeader.biHeight = -bitmap.bmHeight;
	info.bmiHeader.biPlanes = 1;
	info.bmiHeader.biBitCount = 32;
	info.bmiHeader.biCompression = BI_RGB;
	HDC screen = GetDC(NULL);
	SIZE_T pixel_bytes = (SIZE_T)bitmap.bmWidth * (SIZE_T)bitmap.bmHeight * 4;
	BYTE* pixels = (BYTE*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, pixel_bytes);
	if (!screen || !pixels || !GetDIBits(screen, source, 0, bitmap.bmHeight, pixels, &info, DIB_RGB_COLORS)) {
		if (pixels) HeapFree(GetProcessHeap(), 0, pixels);
		if (screen) ReleaseDC(NULL, screen);
		return NULL;
	}
	for (int i = 0; i < bitmap.bmWidth * bitmap.bmHeight; ++i) {
		BYTE* pixel = pixels + i * 4;
		/* Keep the magenta transparency key untouched. */
		if (pixel[2] > 245 && pixel[1] < 10 && pixel[0] > 245) continue;
		/* Preserve the collection BMP's neutral background instead of turning it
		 * into a dark rectangle around the locked silhouette. */
		if (pixel[2] > 242 && pixel[1] > 242 && pixel[0] > 242) continue;
		int grey = (pixel[2] * 30 + pixel[1] * 59 + pixel[0] * 11) / 100;
		grey = grey * 62 / 100;
		pixel[0] = (BYTE)grey;
		pixel[1] = (BYTE)grey;
		pixel[2] = (BYTE)grey;
	}
	BYTE* target_pixels = NULL;
	HBITMAP locked = CreateDIBSection(screen, &info, DIB_RGB_COLORS,
		(void**)&target_pixels, NULL, 0);
	if (locked && target_pixels) memcpy(target_pixels, pixels, pixel_bytes);
	else if (locked) { DeleteObject(locked); locked = NULL; }
	HeapFree(GetProcessHeap(), 0, pixels);
	ReleaseDC(NULL, screen);
	return locked;
}

static void draw_album_fish(HDC dc, RECT image_area, int id, BOOL discovered) {
	int x = (image_area.left + image_area.right) / 2;
	int y = (image_area.top + image_area.bottom) / 2;
	HBITMAP image = NULL;
	for (int i = 0; i < album_count_; ++i)
		if (album_entries_[i].id == id) {
			if (!album_entries_[i].collection_attempted && album_image_load_budget_ > 0) {
				--album_image_load_budget_;
				album_entries_[i].collection_attempted = 1;
				album_entries_[i].collection_image = load_collection_image(album_entries_[i].resource_name);
				if (album_entries_[i].collection_image)
					album_entries_[i].locked_image = create_locked_collection_image(album_entries_[i].collection_image);
				char message[128];
				wsprintfA(message, album_entries_[i].collection_image ?
					"Album image loaded: %s" : "Album image not found in DATA.ini GRFs: %s",
					album_entries_[i].resource_name);
				log_line(message);
			}
			image = discovered ? album_entries_[i].collection_image : album_entries_[i].locked_image;
			break;
		}
	if (image) {
		BITMAP bitmap;
		GetObject(image, sizeof(bitmap), &bitmap);
			int draw_width = bitmap.bmWidth, draw_height = bitmap.bmHeight;
			int maximum_width = image_area.right - image_area.left - 12;
			int maximum_height = image_area.bottom - image_area.top - 8;
			if (draw_width > maximum_width) {
				draw_height = draw_height * maximum_width / draw_width;
				draw_width = maximum_width;
			}
			if (draw_height > maximum_height) {
				draw_width = draw_width * maximum_height / draw_height;
				draw_height = maximum_height;
			}
			// Frame follows the real bitmap aspect ratio instead of using the same
			// square placeholder for every collection image.
			RECT image_frame = {x - draw_width / 2 - 4, y - draw_height / 2 - 3,
				x + (draw_width + 1) / 2 + 4, y + (draw_height + 1) / 2 + 3};
			fill_round(dc, image_frame, 5, RGB(224, 234, 231));
			HBRUSH image_border = CreateSolidBrush(RGB(73, 130, 142));
			FrameRect(dc, &image_frame, image_border);
			DeleteObject(image_border);
			HDC source = CreateCompatibleDC(dc);
			HBITMAP previous = (HBITMAP)SelectObject(source, image);
			TransparentBlt(dc, x - draw_width / 2, y - draw_height / 2, draw_width, draw_height, source, 0, 0,
				bitmap.bmWidth, bitmap.bmHeight, RGB(255, 0, 255));
			SelectObject(source, previous);
		DeleteDC(source);
		if (!discovered) {
			HFONT question_font = CreateFontA(-25, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
				DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
				DEFAULT_PITCH, "Segoe UI");
			HFONT previous_font = (HFONT)SelectObject(dc, question_font);
			SetTextColor(dc, RGB(102, 88, 70));
			SetBkMode(dc, TRANSPARENT);
			RECT question = {image_area.right - 32, image_area.top + 2, image_area.right - 5, image_area.top + 31};
			DrawTextA(dc, "?", -1, &question, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
			SelectObject(dc, previous_font);
			DeleteObject(question_font);
		}
		return;
	}
	if (!discovered) {
		// Dedicated undiscovered-species illustration drawn by the DLL. It does
		// not require an extra BMP beside the client or inside the GRF.
		RECT mystery = {x - 36, y - 24, x + 36, y + 24};
		fill_round(dc, mystery, 10, RGB(20, 48, 60));
		HBRUSH mystery_border = CreateSolidBrush(RGB(65, 91, 101));
		FrameRect(dc, &mystery, mystery_border);
		DeleteObject(mystery_border);
		HPEN wave_pen = CreatePen(PS_SOLID, 2, RGB(45, 91, 105));
		HPEN previous_pen = (HPEN)SelectObject(dc, wave_pen);
		MoveToEx(dc, x - 29, y + 16, NULL); LineTo(dc, x - 10, y + 12);
		LineTo(dc, x + 9, y + 17); LineTo(dc, x + 29, y + 13);
		SelectObject(dc, previous_pen);
		DeleteObject(wave_pen);
		HFONT question_font = CreateFontA(-35, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
			DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
			DEFAULT_PITCH, "Segoe UI");
		HFONT previous_font = (HFONT)SelectObject(dc, question_font);
		SetTextColor(dc, RGB(113, 143, 151));
		RECT question = {x - 24, y - 28, x + 24, y + 15};
		DrawTextA(dc, "?", -1, &question, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
		SelectObject(dc, previous_font);
		DeleteObject(question_font);
		return;
	}
	COLORREF color = RGB(45, 59, 67);
	HBRUSH brush = CreateSolidBrush(color);
	HBRUSH old_brush = (HBRUSH)SelectObject(dc, brush);
	HPEN pen = CreatePen(PS_SOLID, 2, discovered ? RGB(211, 231, 230) : RGB(75, 88, 95));
	HPEN old_pen = (HPEN)SelectObject(dc, pen);
	Ellipse(dc, x - 33, y - 16, x + 25, y + 17);
	POINT tail[3] = {{x + 20, y}, {x + 46, y - 22}, {x + 44, y + 22}};
	Polygon(dc, tail, 3);
	POINT fin[3] = {{x - 5, y - 14}, {x + 8, y - 28}, {x + 13, y - 12}};
	Polygon(dc, fin, 3);
	if (discovered) {
		HBRUSH eye = CreateSolidBrush(RGB(239, 190, 70));
		SelectObject(dc, eye);
		Ellipse(dc, x - 23, y - 7, x - 14, y + 2);
		SetPixel(dc, x - 20, y - 4, RGB(9, 17, 22));
		SelectObject(dc, brush);
		DeleteObject(eye);
	}
	SelectObject(dc, old_pen);
	SelectObject(dc, old_brush);
	DeleteObject(pen);
	DeleteObject(brush);
}


static void draw_quality_stars(HDC dc, RECT area, int quality) {
	const double pi = 3.14159265358979323846;
	int star_size = 18, gap = 5, label_width = 88, label_gap = 8;
	int stars_width = star_size * 5 + gap * 4;
	int total = label_width + label_gap + stars_width;
	int start_x = area.left + (area.right - area.left - total) / 2;
	int cy = (area.top + area.bottom) / 2;
	RECT label_area = {start_x, area.top, start_x + label_width, area.bottom};
	SetTextColor(dc, RGB(57, 45, 31));
	DrawTextA(dc, "Fish Quality:", -1, &label_area, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
	start_x += label_width + label_gap;
	for (int star = 0; star < 5; ++star) {
		POINT points[10]; int cx = start_x + star * (star_size + gap) + star_size / 2;
		for (int i = 0; i < 10; ++i) {
			double angle = -pi / 2.0 + i * pi / 5.0;
			double radius = (i & 1) ? star_size * 0.22 : star_size * 0.48;
			points[i].x = cx + (LONG)(cos(angle) * radius);
			points[i].y = cy + (LONG)(sin(angle) * radius);
		}
		HBRUSH fill = CreateSolidBrush(star < quality ? RGB(244,181,38) : RGB(196,181,149));
		HPEN edge = CreatePen(PS_SOLID,1,star < quality ? RGB(151,92,19) : RGB(132,116,86));
		HGDIOBJ old_fill=SelectObject(dc,fill), old_edge=SelectObject(dc,edge);
		Polygon(dc,points,10); SelectObject(dc,old_fill); SelectObject(dc,old_edge);
		DeleteObject(fill); DeleteObject(edge);
	}
}

static void draw_album_window(HDC dc, RECT area) {
	/* The first frame must never wait for GRF table scans. Later timer-driven
	 * paints load at most one collection image each, so the book appears at
	 * once and fills progressively without freezing its window. */
	album_image_load_budget_ = album_first_paint_ ? 0 : 1;
	album_first_paint_ = 0;
	if (!album_background_attempted_) {
		album_background_attempted_ = 1;
		album_background_ = load_album_background();
		log_line(album_background_ ? "Fishing Album book background loaded." :
			"Fishing Album book background not found; using fallback panel.");
	}
	if (album_background_) {
		BITMAP bitmap;
		GetObject(album_background_, sizeof(bitmap), &bitmap);
		HDC source = CreateCompatibleDC(dc);
		HBITMAP previous = (HBITMAP)SelectObject(source, album_background_);
		SetStretchBltMode(dc, HALFTONE);
		StretchBlt(dc, 0, 0, area.right, area.bottom, source, 0, 0, bitmap.bmWidth, bitmap.bmHeight, SRCCOPY);
		SelectObject(source, previous);
		DeleteDC(source);
	} else draw_panel(dc, area);
	SetBkMode(dc, TRANSPARENT);
	HFONT normal_font = CreateFontA(-15, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
		DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
		DEFAULT_PITCH, "Segoe UI");
	HFONT card_font = CreateFontA(-14, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
		DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
		DEFAULT_PITCH, "Segoe UI");
	HFONT count_font = CreateFontA(-13, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
		DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
		DEFAULT_PITCH, "Segoe UI");
	HFONT date_font = CreateFontA(-13, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
		DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
		DEFAULT_PITCH, "Segoe UI");
	HFONT old_font = (HFONT)SelectObject(dc, normal_font);
	RECT close_box = {683, 14, 710, 41};
	fill_round(dc, close_box, 7, RGB(104, 44, 47));
	SetTextColor(dc, RGB(255, 221, 216));
	DrawTextA(dc, "X", -1, &close_box, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

	int discovered = 0;
	for (int i = 0; i < album_count_; ++i) if (album_entries_[i].catches > 0) ++discovered;
	char summary[64];
	wsprintfA(summary, "Discovered %d / %d", discovered, album_expected_ > 0 ? album_expected_ : album_count_);
	RECT summary_area = {40, 57, 205, 78};
	SetTextColor(dc, RGB(82, 55, 31));
	DrawTextA(dc, summary, -1, &summary_area, DT_LEFT | DT_SINGLELINE);
	int pages = album_count_ > 0 ? (album_count_ + 5) / 6 : 1;
	char page_text[32];
	wsprintfA(page_text, "<     %d / %d     >", album_page_ + 1, pages);
	RECT page_area = {210, 57, 332, 78};
	SetTextColor(dc, RGB(73, 43, 22));
	DrawTextA(dc, page_text, -1, &page_area, DT_CENTER | DT_SINGLELINE);

	int start = album_page_ * 6;
	for (int slot = 0; slot < 6; ++slot) {
		int index = start + slot;
		if (index >= album_count_) break;
		int column = slot % 2, row = slot / 2;
		int left = 40 + column * 156, top = 84 + row * 120;
		RECT card = {left, top, left + 142, top + 112};
		fill_round(dc, card, 8, index == album_selected_ ? RGB(220, 195, 139) : RGB(232, 216, 175));
		RECT card_frame = {left + 1, top + 1, left + 141, top + 111};
		HBRUSH card_border = CreateSolidBrush(index == album_selected_ ? RGB(151, 91, 29) : RGB(139, 111, 69));
		FrameRect(dc, &card_frame, card_border);
		DeleteObject(card_border);
		BOOL found = album_entries_[index].catches > 0;
		RECT image_area = {left + 5, top + 4, left + 137, top + 62};
		draw_album_fish(dc, image_area, album_entries_[index].id, found);
		SelectObject(dc, card_font);
		RECT name = {left + 7, top + 64, left + 135, top + 91};
		SetTextColor(dc, found ? RGB(61, 39, 23) : RGB(114, 96, 71));
		DrawTextA(dc, found ? album_entries_[index].fish_name : "???", -1, &name,
			DT_CENTER | DT_WORDBREAK | DT_WORD_ELLIPSIS);
		char count[32];
		wsprintfA(count, found ? "Caught: %d" : "Not discovered", album_entries_[index].catches);
		SelectObject(dc, count_font);
		RECT count_area = {left + 6, top + 93, left + 136, top + 109};
		SetTextColor(dc, found ? RGB(35, 112, 82) : RGB(126, 105, 77));
		DrawTextA(dc, count, -1, &count_area, DT_CENTER | DT_SINGLELINE);
		SelectObject(dc, normal_font);
	}

	RECT detail = {380, 75, 682, 458};
	fill_round(dc, detail, 11, RGB(232, 216, 175));
	HBRUSH detail_border = CreateSolidBrush(RGB(139, 111, 69));
	FrameRect(dc, &detail, detail_border);
	DeleteObject(detail_border);
	if (album_count_ > 0 && album_selected_ < album_count_) {
		FishingAlbumEntry* entry = &album_entries_[album_selected_];
		if (entry->catches > 0) {
			RECT fish_title = {395, 86, 667, 126};
			SetTextColor(dc, RGB(73, 43, 22));
			DrawTextA(dc, entry->fish_name, -1, &fish_title, DT_CENTER | DT_WORDBREAK | DT_WORD_ELLIPSIS);
			RECT large_image = {407, 128, 655, 246};
			draw_album_fish(dc, large_image, entry->id, TRUE);
			char line[128];
			RECT line_area = {403, 258, 659, 280};
			SetTextColor(dc, RGB(57, 45, 31));
			wsprintfA(line, "Caught: %d", entry->catches);
			DrawTextA(dc, line, -1, &line_area, DT_CENTER | DT_SINGLELINE);
			line_area.top = 290; line_area.bottom = 312;
			wsprintfA(line, "Longest: %d.%02d cm", entry->size / 100, entry->size % 100);
			DrawTextA(dc, line, -1, &line_area, DT_CENTER | DT_SINGLELINE);
			line_area.top = 316; line_area.bottom = 338;
			wsprintfA(line, "Heaviest: %d.%03d kg", entry->weight / 1000, entry->weight % 1000);
			DrawTextA(dc, line, -1, &line_area, DT_CENTER | DT_SINGLELINE);
			line_area.top = 340; line_area.bottom = 368;
			draw_quality_stars(dc, line_area, entry->quality);
			line_area.top = 377; line_area.bottom = 399;
			wsprintfA(line, "Map: %s", entry->map_name[0] ? entry->map_name : "Unknown");
			DrawTextA(dc, line, -1, &line_area, DT_CENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
			line_area.top = 403; line_area.bottom = 425;
			wsprintfA(line, "Last catch: %s", entry->caught_at[0] ? entry->caught_at : "Unknown");
			SelectObject(dc, date_font);
			DrawTextA(dc, line, -1, &line_area, DT_CENTER | DT_SINGLELINE);
			SelectObject(dc, normal_font);
		} else {
			RECT mystery_image = {410, 118, 652, 270};
			draw_album_fish(dc, mystery_image, entry->id, FALSE);
			RECT unknown = {405, 285, 657, 360};
			SetTextColor(dc, RGB(113, 91, 64));
			DrawTextA(dc, "Unknown catch\r\n\r\nObtain this catch to reveal its fishing record.", -1, &unknown, DT_CENTER | DT_WORDBREAK);
		}
	}
	SelectObject(dc, old_font);
	DeleteObject(normal_font);
	DeleteObject(card_font);
	DeleteObject(count_font);
	DeleteObject(date_font);
}

static LRESULT CALLBACK album_proc(HWND window, UINT message, WPARAM w, LPARAM l) {
	if (message == WM_ERASEBKGND) return 1;
	if (message == WM_TIMER && w == 1) {
		if (album_open_ && IsWindowVisible(window)) {
			for (int i = 0; i < album_count_; ++i)
				if (!album_entries_[i].collection_attempted) {
					InvalidateRect(window, NULL, FALSE);
					break;
				}
		}
		return 0;
	}
	if (message == WM_KEYDOWN && w == VK_ESCAPE) {
		InterlockedExchange(&album_open_, 0);
		ShowWindow(window, SW_HIDE);
		return 0;
	}
	if (message == WM_LBUTTONDOWN) {
		int x = LOWORD(l), y = HIWORD(l);
		if (x >= 680 && y <= 46) {
			InterlockedExchange(&album_open_, 0);
			ShowWindow(window, SW_HIDE);
			return 0;
		}
		if (y >= 50 && y <= 78) {
			int pages = album_count_ > 0 ? (album_count_ + 5) / 6 : 1;
			if (x >= 205 && x < 255 && album_page_ > 0) --album_page_;
			if (x > 290 && x <= 340 && album_page_ + 1 < pages) ++album_page_;
			album_selected_ = album_page_ * 6;
			InvalidateRect(window, NULL, FALSE);
			return 0;
		}
		if (y >= 84 && y < 436) {
			int column = (x - 40) / 156, row = (y - 84) / 120;
			if (x >= 40 && column >= 0 && column < 2 && row >= 0 && row < 3 && (x - 40) % 156 < 142 && (y - 84) % 120 < 112) {
				int index = album_page_ * 6 + row * 2 + column;
				if (index < album_count_) album_selected_ = index;
				InvalidateRect(window, NULL, FALSE);
			}
		}
		return 0;
	}
	if (message == WM_PAINT) {
		PAINTSTRUCT paint;
		HDC dc = BeginPaint(window, &paint);
		RECT area;
		GetClientRect(window, &area);
		HDC buffer_dc = CreateCompatibleDC(dc);
		HBITMAP buffer_bitmap = CreateCompatibleBitmap(dc, area.right, area.bottom);
		HBITMAP old_bitmap = (HBITMAP)SelectObject(buffer_dc, buffer_bitmap);
		draw_album_window(buffer_dc, area);
		BitBlt(dc, 0, 0, area.right, area.bottom, buffer_dc, 0, 0, SRCCOPY);
		SelectObject(buffer_dc, old_bitmap);
		DeleteObject(buffer_bitmap);
		DeleteDC(buffer_dc);
		EndPaint(window, &paint);
		return 0;
	}
	return DefWindowProcA(window, message, w, l);
}

static void draw_card_art(HDC dc, RECT area, CardAlbumEntry* entry, BOOL discovered) {
	if (!entry->image_attempted && card_image_load_budget_ > 0) {
		--card_image_load_budget_;
		entry->image_attempted = 1;
		entry->image = load_card_image(entry->id);
		if (entry->image) entry->locked_image = create_locked_collection_image(entry->image);
		char message[128];
		wsprintfA(message, entry->image ? "Card art loaded: %d" : "Card art not found: %d", entry->id);
		log_line(message);
	}
	HBITMAP image = discovered ? entry->image : entry->locked_image;
	if (!image) {
		fill_round(dc, area, 7, discovered ? RGB(80, 70, 61) : RGB(58, 57, 58));
		HFONT font = CreateFontA(-34, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
			DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
			DEFAULT_PITCH, "Segoe UI");
		HFONT old = (HFONT)SelectObject(dc, font);
		SetTextColor(dc, discovered ? RGB(221, 188, 116) : RGB(126, 119, 107));
		DrawTextA(dc, discovered ? "CARD" : "?", -1, &area, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
		SelectObject(dc, old); DeleteObject(font);
		return;
	}
	BITMAP bitmap; GetObject(image, sizeof(bitmap), &bitmap);
	int max_width = area.right - area.left, max_height = area.bottom - area.top;
	int width = bitmap.bmWidth, height = bitmap.bmHeight;
	if (width * max_height > height * max_width) { height = height * max_width / width; width = max_width; }
	else { width = width * max_height / height; height = max_height; }
	int x = (area.left + area.right - width) / 2, y = (area.top + area.bottom - height) / 2;
	HDC source = CreateCompatibleDC(dc); HBITMAP previous = (HBITMAP)SelectObject(source, image);
	SetStretchBltMode(dc, HALFTONE);
	StretchBlt(dc, x, y, width, height, source, 0, 0, bitmap.bmWidth, bitmap.bmHeight, SRCCOPY);
	SelectObject(source, previous); DeleteDC(source);
}

static void draw_card_album_window(HDC dc, RECT area) {
	card_image_load_budget_ = card_first_paint_ ? 0 : 1;
	card_first_paint_ = 0;
	if (!card_album_background_attempted_) {
		card_album_background_attempted_ = 1;
		card_album_background_ = load_card_album_background();
		log_line(card_album_background_ ? "Card Album book background loaded." :
			"Card Album background missing; using clean fallback.");
	}
	if (card_album_background_) {
		BITMAP bitmap; GetObject(card_album_background_, sizeof(bitmap), &bitmap);
		HDC source = CreateCompatibleDC(dc);
		HBITMAP previous = (HBITMAP)SelectObject(source, card_album_background_);
		SetStretchBltMode(dc, HALFTONE);
		StretchBlt(dc, 0, 0, area.right, area.bottom, source, 0, 0,
			bitmap.bmWidth, bitmap.bmHeight, SRCCOPY);
		SelectObject(source, previous); DeleteDC(source);
	} else {
		fill_color(dc, area, RGB(66, 45, 30));
		RECT left_page = {18, 14, 356, 486}, right_page = {364, 14, 702, 486};
		fill_round(dc, left_page, 12, RGB(239, 224, 187));
		fill_round(dc, right_page, 12, RGB(239, 224, 187));
	}
	SetBkMode(dc, TRANSPARENT);
	HFONT title_font = CreateFontA(-25, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
		DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH, "Segoe UI");
	HFONT normal_font = CreateFontA(-15, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
		DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH, "Segoe UI");
	HFONT small_font = CreateFontA(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
		DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH, "Segoe UI");
	HFONT old_font = (HFONT)SelectObject(dc, title_font);
	SetTextColor(dc, RGB(74, 43, 25)); RECT title = {35, 27, 344, 53};
	DrawTextA(dc, "CARD ALBUM", -1, &title, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
	SelectObject(dc, normal_font);
	RECT close_box = {660, 23, 682, 45}; fill_round(dc, close_box, 5, RGB(91, 38, 31));
	HBRUSH close_border = CreateSolidBrush(RGB(190, 141, 63));
	FrameRect(dc, &close_box, close_border); DeleteObject(close_border);
	SetTextColor(dc, RGB(255, 221, 216)); DrawTextA(dc, "X", -1, &close_box, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

	RECT search_box = {38, 61, 341, 87};
	fill_round(dc, search_box, 6, card_search_active_ ? RGB(255, 250, 230) : RGB(245, 235, 207));
	HBRUSH border = CreateSolidBrush(card_search_active_ ? RGB(166, 107, 39) : RGB(154, 124, 78));
	FrameRect(dc, &search_box, border); DeleteObject(border);
	SelectObject(dc, small_font); SetTextColor(dc, card_search_[0] ? RGB(63, 45, 31) : RGB(128, 110, 84));
	RECT search_text = {48, 66, 333, 85};
	DrawTextA(dc, card_search_[0] ? card_search_ : "Search name or ID...", -1, &search_text,
		DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

	const char* filters[3] = {"All", "Found", "Missing"};
	const int filter_left[3] = {72, 151, 237};
	const int filter_right[3] = {142, 228, 307};
	for (int i = 0; i < 3; ++i) {
		RECT button = {filter_left[i], 94, filter_right[i], 118};
		fill_round(dc, button, 6, card_filter_ == i ? RGB(151, 86, 42) : RGB(222, 199, 153));
		SetTextColor(dc, card_filter_ == i ? RGB(255, 241, 202) : RGB(77, 51, 31));
		HFONT filter_font = CreateFontA(-11, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
			DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH, "Segoe UI");
		HFONT previous_filter = (HFONT)SelectObject(dc, filter_font);
		DrawTextA(dc, filters[i], -1, &button, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
		SelectObject(dc, previous_filter); DeleteObject(filter_font);
	}

	int discovered = 0;
	for (int i = 0; i < card_count_; ++i) if (card_entries_[i].registered_at[0]) ++discovered;
	char summary[80]; wsprintfA(summary, "Discovered %d / %d", discovered,
		card_expected_ > 0 ? card_expected_ : card_count_);
	SelectObject(dc, normal_font); SetTextColor(dc, RGB(79, 51, 31));
	RECT summary_area = {35, 124, 344, 143};
	DrawTextA(dc, summary, -1, &summary_area, DT_CENTER | DT_SINGLELINE);
	RECT progress_track = {39, 147, 340, 156}; fill_round(dc, progress_track, 4, RGB(190, 169, 126));
	int total = card_expected_ > 0 ? card_expected_ : card_count_;
	int filled = total > 0 ? (progress_track.right - progress_track.left) * discovered / total : 0;
	RECT progress = {progress_track.left, progress_track.top, progress_track.left + filled, progress_track.bottom};
	fill_round(dc, progress, 3, RGB(166, 99, 39));
	char percent[24]; wsprintfA(percent, total > 0 ? "%d.%02d%%" : "0.00%%",
		total > 0 ? discovered * 100 / total : 0,
		total > 0 ? (discovered * 10000 / total) % 100 : 0);
	SelectObject(dc, small_font); SetTextColor(dc, RGB(91, 63, 39));
	RECT percent_area = {286, 124, 340, 143};
	DrawTextA(dc, percent, -1, &percent_area, DT_RIGHT | DT_SINGLELINE);

	int visible = visible_card_count();
	int pages = visible > 0 ? (visible + 5) / 6 : 1;
	if (card_page_ >= pages) card_page_ = pages - 1;
	int start = card_page_ * 6;
	for (int slot = 0; slot < 6; ++slot) {
		int index = visible_card_index(start + slot); if (index < 0) break;
		int column = slot % 3, row = slot / 3;
		int left = 37 + column * 102, top = 166 + row * 133;
		RECT cell = {left, top, left + 94, top + 125};
		fill_round(dc, cell, 7, index == card_selected_ ? RGB(231, 190, 101) : RGB(234, 220, 185));
		RECT frame = {left + 1, top + 1, left + 93, top + 124};
		HBRUSH cell_border = CreateSolidBrush(index == card_selected_ ? RGB(177, 92, 25) : RGB(133, 101, 61));
		FrameRect(dc, &frame, cell_border); DeleteObject(cell_border);
		if (index == card_selected_) {
			RECT inner_frame = {left + 3, top + 3, left + 91, top + 122};
			HBRUSH selected_border = CreateSolidBrush(RGB(199, 121, 34));
			FrameRect(dc, &inner_frame, selected_border); DeleteObject(selected_border);
		}
		BOOL found = card_entries_[index].registered_at[0] != 0;
		RECT image_area = {left + 9, top + 7, left + 85, top + 89};
		draw_card_art(dc, image_area, &card_entries_[index], found);
		SelectObject(dc, small_font);
		RECT name = {left + 5, top + 91, left + 89, top + 122};
		SetTextColor(dc, found ? RGB(57, 36, 24) : RGB(105, 91, 70));
		DrawTextA(dc, found ? card_entries_[index].name : "Missing", -1, &name,
			DT_CENTER | DT_VCENTER | DT_WORDBREAK | DT_WORD_ELLIPSIS);
	}

	RECT previous_button = {36, 435, 108, 462}, next_button = {270, 435, 342, 462};
	fill_round(dc, previous_button, 6, card_page_ > 0 ? RGB(151, 86, 42) : RGB(188, 166, 125));
	fill_round(dc, next_button, 6, card_page_ + 1 < pages ? RGB(151, 86, 42) : RGB(188, 166, 125));
	SelectObject(dc, small_font); SetTextColor(dc, RGB(255, 241, 202));
	DrawTextA(dc, "Previous", -1, &previous_button, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
	DrawTextA(dc, "Next", -1, &next_button, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
	char page_text[32]; wsprintfA(page_text, "%d / %d", card_page_ + 1, pages);
	RECT page_box = {145, 435, 233, 462};
	fill_round(dc, page_box, 5, card_page_input_active_ ? RGB(255, 250, 230) : RGB(235, 218, 181));
	border = CreateSolidBrush(card_page_input_active_ ? RGB(166, 107, 39) : RGB(154, 124, 78));
	FrameRect(dc, &page_box, border); DeleteObject(border);
	SetTextColor(dc, RGB(74, 48, 29));
	DrawTextA(dc, card_page_input_active_ && card_page_input_[0] ? card_page_input_ : page_text,
		-1, &page_box, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

	if (visible > 0 && card_selected_ < card_count_ && card_is_visible(card_selected_)) {
		CardAlbumEntry* entry = &card_entries_[card_selected_]; BOOL found = entry->registered_at[0] != 0;
		SelectObject(dc, title_font); SetTextColor(dc, RGB(69, 39, 24));
		RECT card_title = {398, 50, 670, 88};
		DrawTextA(dc, found ? entry->name : "Undiscovered card", -1, &card_title, DT_CENTER | DT_WORDBREAK | DT_WORD_ELLIPSIS);
		RECT large_frame = {435, 99, 634, 307};
		fill_round(dc, large_frame, 8, RGB(225, 208, 170));
		border = CreateSolidBrush(found ? RGB(158, 100, 38) : RGB(139, 119, 85));
		FrameRect(dc, &large_frame, border); DeleteObject(border);
		RECT large_image = {442, 106, 627, 300}; draw_card_art(dc, large_image, entry, found);
		SelectObject(dc, small_font); char line[128]; RECT line_area = {395, 319, 673, 348};
		if (found) {
			RECT id_badge = {470, 319, 598, 348};
			fill_round(dc, id_badge, 6, RGB(219, 195, 148));
			wsprintfA(line, "ID  %d", entry->id); DrawTextA(dc, line, -1, &id_badge, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
			SetTextColor(dc, RGB(72, 51, 34));
			RECT separator = {414, 356, 654, 358}; fill_color(dc, separator, RGB(202, 175, 125));
			line_area.top = 365; line_area.bottom = 382;
			DrawTextA(dc, "FIRST OBTAINED", -1, &line_area, DT_CENTER | DT_SINGLELINE);
			line_area.top = 384; line_area.bottom = 403;
			DrawTextA(dc, entry->registered_at, -1, &line_area, DT_CENTER | DT_SINGLELINE);
			line_area.top = 416; line_area.bottom = 433;
			separator.top = 409; separator.bottom = 411; fill_color(dc, separator, RGB(202, 175, 125));
			DrawTextA(dc, "LAST OBTAINED", -1, &line_area, DT_CENTER | DT_SINGLELINE);
			line_area.top = 435; line_area.bottom = 454;
			DrawTextA(dc, entry->last_obtained_at[0] ? entry->last_obtained_at : entry->registered_at,
				-1, &line_area, DT_CENTER | DT_SINGLELINE);
		} else {
			line_area.top = 335; line_area.bottom = 395;
			SetTextColor(dc, RGB(105, 83, 58));
			DrawTextA(dc, "Obtain this card to reveal it in your account album.",
				-1, &line_area, DT_CENTER | DT_WORDBREAK);
		}
	}
	SelectObject(dc, old_font); DeleteObject(title_font); DeleteObject(normal_font); DeleteObject(small_font);
}

static LRESULT CALLBACK card_album_proc(HWND window, UINT message, WPARAM w, LPARAM l) {
	if (message == WM_ERASEBKGND) return 1;
	if (message == WM_TIMER && w == 1) {
		if (card_album_open_ && IsWindowVisible(window)) {
			for (int slot = 0; slot < 6; ++slot) {
				int index = visible_card_index(card_page_ * 6 + slot);
				if (index >= 0 && !card_entries_[index].image_attempted) {
					InvalidateRect(window, NULL, FALSE); break;
				}
			}
		}
		return 0;
	}
	if (message == WM_KEYDOWN && w == VK_ESCAPE) {
		if (card_search_active_ || card_page_input_active_) {
			card_search_active_ = 0; card_page_input_active_ = 0;
			InvalidateRect(window, NULL, FALSE); return 0;
		}
		InterlockedExchange(&card_album_open_, 0); ShowWindow(window, SW_HIDE); return 0;
	}
	if (message == WM_CHAR && card_search_active_) {
		int length = lstrlenA(card_search_);
		if (w == VK_BACK && length > 0) card_search_[length - 1] = 0;
		else if (w >= 32 && w < 127 && length < (int)sizeof(card_search_) - 1) {
			card_search_[length] = (char)w; card_search_[length + 1] = 0;
		}
		reset_card_page(); InvalidateRect(window, NULL, FALSE); return 0;
	}
	if (message == WM_CHAR && card_page_input_active_) {
		int length = lstrlenA(card_page_input_);
		if (w == VK_BACK && length > 0) card_page_input_[length - 1] = 0;
		else if (w >= '0' && w <= '9' && length < (int)sizeof(card_page_input_) - 1) {
			card_page_input_[length] = (char)w; card_page_input_[length + 1] = 0;
		} else if (w == VK_RETURN) {
			int requested = 0, pages = (visible_card_count() + 5) / 6;
			if (pages < 1) pages = 1;
			if (sscanf(card_page_input_, "%d", &requested) == 1) {
				if (requested < 1) requested = 1;
				if (requested > pages) requested = pages;
				card_page_ = requested - 1;
				card_selected_ = visible_card_index(card_page_ * 6);
			}
			card_page_input_[0] = 0; card_page_input_active_ = 0;
		}
		InvalidateRect(window, NULL, FALSE); return 0;
	}
	if (message == WM_LBUTTONDOWN) {
		int x = LOWORD(l), y = HIWORD(l);
		if (x >= 655 && x <= 688 && y >= 18 && y <= 50) { InterlockedExchange(&card_album_open_, 0); ShowWindow(window, SW_HIDE); return 0; }
		if (x >= 38 && x <= 341 && y >= 61 && y <= 87) {
			card_search_active_ = 1; card_page_input_active_ = 0;
			SetFocus(window); InvalidateRect(window, NULL, FALSE); return 0;
		}
		if (y >= 94 && y <= 118 && x >= 72 && x <= 307) {
			int choice = x <= 142 ? 0 : x >= 151 && x <= 228 ? 1 : x >= 237 ? 2 : -1;
			if (choice >= 0 && choice < 3) {
				card_filter_ = choice; card_search_active_ = 0;
				card_page_input_active_ = 0; reset_card_page();
				InvalidateRect(window, NULL, FALSE);
			}
			return 0;
		}
		int pages = (visible_card_count() + 5) / 6; if (pages < 1) pages = 1;
		if (x >= 36 && x <= 108 && y >= 435 && y <= 462 && card_page_ > 0) {
			--card_page_; card_selected_ = visible_card_index(card_page_ * 6);
			card_search_active_ = card_page_input_active_ = 0;
			InvalidateRect(window, NULL, FALSE); return 0;
		}
		if (x >= 270 && x <= 342 && y >= 435 && y <= 462 && card_page_ + 1 < pages) {
			++card_page_; card_selected_ = visible_card_index(card_page_ * 6);
			card_search_active_ = card_page_input_active_ = 0;
			InvalidateRect(window, NULL, FALSE); return 0;
		}
		if (x >= 145 && x <= 233 && y >= 435 && y <= 462) {
			card_page_input_[0] = 0; card_page_input_active_ = 1;
			card_search_active_ = 0; SetFocus(window);
			InvalidateRect(window, NULL, FALSE); return 0;
		}
		if (x >= 37 && x < 343 && y >= 166 && y < 432) {
			int column = (x - 37) / 102, row = (y - 166) / 133;
			if (column >= 0 && column < 3 && row >= 0 && row < 2 &&
				(x - 37) % 102 < 94 && (y - 166) % 133 < 125) {
				int index = visible_card_index(card_page_ * 6 + row * 3 + column);
				if (index >= 0) card_selected_ = index;
			}
			card_search_active_ = card_page_input_active_ = 0;
			InvalidateRect(window, NULL, FALSE);
		}
		return 0;
	}
	if (message == WM_PAINT) {
		PAINTSTRUCT paint; HDC dc = BeginPaint(window, &paint); RECT area; GetClientRect(window, &area);
		HDC buffer_dc = CreateCompatibleDC(dc); HBITMAP bitmap = CreateCompatibleBitmap(dc, area.right, area.bottom);
		HBITMAP old = (HBITMAP)SelectObject(buffer_dc, bitmap); draw_card_album_window(buffer_dc, area);
		BitBlt(dc, 0, 0, area.right, area.bottom, buffer_dc, 0, 0, SRCCOPY);
		SelectObject(buffer_dc, old); DeleteObject(bitmap); DeleteDC(buffer_dc); EndPaint(window, &paint); return 0;
	}
	return DefWindowProcA(window, message, w, l);
}

static BOOL cooking_recipe_visible(int index) {
	return index >= 0 && index < cooking_recipe_count_ &&
		(cooking_category_ < 0 || cooking_recipes_[index].category == cooking_category_);
}

static int visible_cooking_count(void) {
	int count = 0;
	for (int i = 0; i < cooking_recipe_count_; ++i) if (cooking_recipe_visible(i)) ++count;
	return count;
}

static int visible_cooking_index(int position) {
	for (int i = 0; i < cooking_recipe_count_; ++i)
		if (cooking_recipe_visible(i) && position-- == 0) return i;
	return -1;
}

static void reset_cooking_page(void) {
	cooking_page_ = 0;
	cooking_selected_ = visible_cooking_index(0);
	if (cooking_selected_ < 0) cooking_selected_ = 0;
	cooking_item_details_open_ = 0;
}

static BOOL cooking_recipe_can_craft(const CookingRecipe* recipe) {
	if (!recipe || cooking_mode_ < 1 || !recipe->unlocked || cooking_request_pending_) return FALSE;
	for (int index = 0; index < recipe->ingredient_count; ++index)
		if (recipe->ingredients[index].owned < recipe->ingredients[index].required) return FALSE;
	return TRUE;
}

static void cooking_send_virtual_key(WORD key) {
	INPUT input[2];
	ZeroMemory(input, sizeof(input));
	input[0].type = input[1].type = INPUT_KEYBOARD;
	input[0].ki.wVk = input[1].ki.wVk = key;
	input[1].ki.dwFlags = KEYEVENTF_KEYUP;
	SendInput(2, input, sizeof(INPUT));
}

static void cooking_send_unicode_text(const char* text) {
	for (int index = 0; text[index]; ++index) {
		INPUT input[2];
		ZeroMemory(input, sizeof(input));
		input[0].type = input[1].type = INPUT_KEYBOARD;
		input[0].ki.wScan = input[1].ki.wScan = (WORD)(unsigned char)text[index];
		input[0].ki.dwFlags = KEYEVENTF_UNICODE;
		input[1].ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
		SendInput(2, input, sizeof(INPUT));
		Sleep(8);
	}
}

static BOOL cooking_focus_game(void) {
	if (!game_ || !IsWindow(game_)) return FALSE;
	DWORD game_thread = GetWindowThreadProcessId(game_, NULL);
	DWORD current_thread = GetCurrentThreadId();
	BOOL attached = game_thread != current_thread && AttachThreadInput(current_thread, game_thread, TRUE);
	SetForegroundWindow(game_);
	BringWindowToTop(game_);
	SetFocus(game_);
	if (attached) AttachThreadInput(current_thread, game_thread, FALSE);
	return GetForegroundWindow() == game_;
}

static DWORD WINAPI cooking_request_thread(void* parameter) {
	CookingRequest* request = (CookingRequest*)parameter;
	const int recipe_id = request->recipe_id;
	const LONG request_serial = request->serial;
	HeapFree(GetProcessHeap(), 0, request);
	char command[40];
	wsprintfA(command, "@hrocook %d", recipe_id);
	char log_message[96];
	wsprintfA(log_message, "Recipe Book request %ld: %s", request_serial, command);
	log_line(log_message);
	if (!cooking_focus_game()) {
		log_line("Recipe Book request failed: game window did not receive focus.");
		if (request_serial == InterlockedCompareExchange(&cooking_request_serial_, 0, 0)) {
			cooking_result_ = 0;
			cooking_result_received_ = 1;
			cooking_request_pending_ = 0;
			if (cooking_book_) InvalidateRect(cooking_book_, NULL, FALSE);
		}
		return 0;
	}
	// Give the recipe a visible preparation phase before asking the server to
	// consume ingredients. The server revalidates the inventory at completion.
	DWORD progress_started = (DWORD)InterlockedCompareExchange(&cooking_progress_start_, 0, 0);
	while (request_serial == InterlockedCompareExchange(&cooking_request_serial_, 0, 0) &&
		cooking_request_pending_) {
		DWORD elapsed = GetTickCount() - progress_started;
		if (elapsed >= COOKING_PROGRESS_DURATION_MS) break;
		if (cooking_book_) InvalidateRect(cooking_book_, NULL, FALSE);
		Sleep(33);
	}
	if (request_serial != InterlockedCompareExchange(&cooking_request_serial_, 0, 0) ||
		!cooking_request_pending_) return 0;
	if (cooking_book_) InvalidateRect(cooking_book_, NULL, FALSE);

	// The Recipe Book click leaves keyboard focus outside the RO chat input.
	// Enter opens chat; Escape must not be sent because RO uses it for Game Options.
	Sleep(120);
	cooking_send_virtual_key(VK_RETURN);
	Sleep(120);
	cooking_send_unicode_text(command);
	Sleep(80);
	cooking_send_virtual_key(VK_RETURN);

	// Only this exact request may time itself out. An older worker must never
	// cancel a later repeated cooking attempt that is already in progress.
	Sleep(5000);
	if (request_serial == InterlockedCompareExchange(&cooking_request_serial_, 0, 0) &&
		cooking_request_pending_) {
		log_line("Recipe Book request timed out without a server result.");
		cooking_result_ = 0;
		cooking_result_received_ = 1;
		cooking_request_pending_ = 0;
		InterlockedExchange(&cooking_progress_start_, 0);
		if (cooking_book_) InvalidateRect(cooking_book_, NULL, FALSE);
	}
	return 0;
}

static void cooking_request_recipe(int recipe_id) {
	if (recipe_id < 1 || cooking_request_pending_) return;
	CookingRequest* request = (CookingRequest*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(CookingRequest));
	if (!request) return;
	request->recipe_id = recipe_id;
	request->serial = InterlockedIncrement(&cooking_request_serial_);
	cooking_request_pending_ = 1;
	InterlockedExchange(&cooking_progress_start_, (LONG)GetTickCount());
	cooking_result_ = 0;
	cooking_result_received_ = 0;
	cooking_requested_category_ = cooking_category_;
	HANDLE thread = CreateThread(NULL, 0, cooking_request_thread, request, 0, NULL);
	if (thread) CloseHandle(thread);
	else {
		HeapFree(GetProcessHeap(), 0, request);
		cooking_request_pending_ = 0;
		InterlockedExchange(&cooking_progress_start_, 0);
	}
}

static void draw_client_item_description(HDC dc, const char* text, RECT* area) {
	WCHAR wide[1024];
	int converted;

	if (!text || !text[0]) return;
	converted = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1,
		wide, sizeof(wide) / sizeof(wide[0]));
	if (!converted)
		converted = MultiByteToWideChar(949, 0, text, -1,
			wide, sizeof(wide) / sizeof(wide[0]));
	if (!converted)
		converted = MultiByteToWideChar(CP_ACP, 0, text, -1,
			wide, sizeof(wide) / sizeof(wide[0]));
	if (converted)
		DrawTextW(dc, wide, -1, area, DT_LEFT | DT_TOP | DT_WORDBREAK | DT_EDITCONTROL);
}

static void draw_cooking_book_window(HDC dc, RECT area) {
	cooking_image_load_budget_ = cooking_first_paint_ ? 0 : 2;
	cooking_first_paint_ = 0;
	/* Use the same ornamental book asset as Card Album so the recetario is a
	 * single integrated book rather than a floating panel drawn over one. */
	if (!card_album_background_attempted_) {
		card_album_background_attempted_ = 1;
		card_album_background_ = load_card_album_background();
		log_line(card_album_background_ ? "Recipe Book background loaded from Card Album asset." :
			"Recipe Book background missing; using clean fallback.");
	}
	if (card_album_background_) {
		BITMAP bitmap; GetObject(card_album_background_, sizeof(bitmap), &bitmap);
		HDC source = CreateCompatibleDC(dc);
		HBITMAP previous = (HBITMAP)SelectObject(source, card_album_background_);
		SetStretchBltMode(dc, HALFTONE);
		StretchBlt(dc, 0, 0, area.right, area.bottom, source, 0, 0,
			bitmap.bmWidth, bitmap.bmHeight, SRCCOPY);
		SelectObject(source, previous); DeleteDC(source);
	} else {
		fill_color(dc, area, RGB(66, 45, 30));
		RECT left_page = {18, 14, 356, 486}, right_page = {364, 14, 702, 486};
		fill_round(dc, left_page, 12, RGB(239, 224, 187));
		fill_round(dc, right_page, 12, RGB(239, 224, 187));
	}
	SetBkMode(dc, TRANSPARENT);
	HFONT title_font = CreateFontA(-21, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
		OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH, "Segoe UI");
	HFONT normal_font = CreateFontA(-15, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
		OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH, "Segoe UI");
	HFONT small_font = CreateFontA(-13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
		OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH, "Segoe UI");
	HFONT old_font = (HFONT)SelectObject(dc, title_font);
	RECT title_icon = {102, 24, 134, 56};
	draw_item_image(dc, title_icon, &cooking_title_icon_, &cooking_title_icon_attempted_, 5026, "Chef_Hat");
	SetTextColor(dc, RGB(74, 43, 25));
	RECT title = {140, 27, 314, 53}; DrawTextA(dc, "Recipe Book", -1, &title, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
	RECT close_box = {660, 23, 682, 45}; fill_round(dc, close_box, 5, RGB(91, 38, 31));
	HBRUSH close_border = CreateSolidBrush(RGB(190, 141, 63));
	FrameRect(dc, &close_box, close_border); DeleteObject(close_border);
	SetTextColor(dc, RGB(255, 221, 216)); DrawTextA(dc, "X", -1, &close_box, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

	SelectObject(dc, small_font);
	RECT category_box = {45, 61, 334, 87};
	fill_round(dc, category_box, 5, RGB(222, 199, 153));
	HBRUSH category_border = CreateSolidBrush(RGB(154, 124, 78));
	FrameRect(dc, &category_box, category_border); DeleteObject(category_border);
	SetTextColor(dc, RGB(64, 42, 23));
	RECT category_label = {55, 61, 299, 87};
	const char* selected_category = cooking_category_ < 0 ? "All recipes" : cooking_categories_[cooking_category_];
	DrawTextA(dc, selected_category, -1, &category_label, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	RECT category_arrow = {301, 61, 328, 87};
	DrawTextA(dc, cooking_category_dropdown_ ? "^" : "v", -1, &category_arrow, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

	int visible = visible_cooking_count();
	int pages = visible > 0 ? (visible + 5) / 6 : 1;
	if (cooking_page_ >= pages) cooking_page_ = pages - 1;
	SelectObject(dc, normal_font);
	for (int slot = 0; slot < 6; ++slot) {
		int index = visible_cooking_index(cooking_page_ * 6 + slot);
		if (index < 0) break;
		CookingRecipe* recipe = &cooking_recipes_[index];
		int row = slot, top = 95 + row * 55;
		RECT cell = {45, top, 334, top + 48};
		fill_round(dc, cell, 5, index == cooking_selected_ ? RGB(225, 190, 111) : RGB(239, 226, 190));
		HBRUSH cell_border = CreateSolidBrush(index == cooking_selected_ ? RGB(177, 92, 25) : RGB(210, 185, 137));
		FrameRect(dc, &cell, cell_border); DeleteObject(cell_border);
		RECT recipe_rule = {91, top + 44, 323, top + 45};
		fill_color(dc, recipe_rule, RGB(210, 185, 137));
		RECT icon = {51, top + 4, 84, top + 43};
		draw_item_image(dc, icon, &recipe->image, &recipe->image_attempted, recipe->product_id, recipe->resource_name);
		SetTextColor(dc, recipe->unlocked ? RGB(62, 42, 25) : RGB(119, 101, 76));
		RECT name = {91, top + 4, 323, top + 25};
		DrawTextA(dc, recipe->unlocked ? recipe->name : "Locked recipe", -1, &name, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
		SelectObject(dc, small_font);
		char state[64]; wsprintfA(state, recipe->unlocked ? "%d.%02d%% success" : "Recipe not learned",
			recipe->success_rate / 100, recipe->success_rate % 100);
		RECT state_area = {91, top + 26, 323, top + 43};
		SetTextColor(dc, recipe->unlocked ? RGB(39, 116, 81) : RGB(143, 65, 57));
		DrawTextA(dc, state, -1, &state_area, DT_LEFT | DT_SINGLELINE);
		SelectObject(dc, normal_font);
	}

	RECT previous = {45, 435, 117, 462}, next = {262, 435, 334, 462};
	fill_round(dc, previous, 6, cooking_page_ > 0 ? RGB(42, 91, 106) : RGB(111, 124, 122));
	fill_round(dc, next, 6, cooking_page_ + 1 < pages ? RGB(42, 91, 106) : RGB(111, 124, 122));
	SelectObject(dc, small_font); SetTextColor(dc, RGB(241, 238, 218));
	DrawTextA(dc, "Previous", -1, &previous, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
	DrawTextA(dc, "Next", -1, &next, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
	char page[32]; wsprintfA(page, "%d / %d", cooking_page_ + 1, pages);
	RECT page_area = {145, 435, 234, 462}; SetTextColor(dc, RGB(77, 53, 31));
	DrawTextA(dc, page, -1, &page_area, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

	if (cooking_recipe_count_ > 0 && cooking_selected_ < cooking_recipe_count_) {
		CookingRecipe* recipe = &cooking_recipes_[cooking_selected_];
		RECT product_name = {405, 50, 663, 86};
		SelectObject(dc, title_font); SetTextColor(dc, RGB(71, 43, 23));
		DrawTextA(dc, recipe->unlocked ? recipe->name : "Locked recipe", -1, &product_name,
			DT_CENTER | DT_VCENTER | DT_WORDBREAK);
		if (cooking_item_details_open_) {
			if (!recipe->description_attempted) {
				recipe->description_attempted = 1;
				if (!find_item_description(recipe->product_id, recipe->description, sizeof(recipe->description)))
					lstrcpynA(recipe->description, "No client item description is available.", sizeof(recipe->description));
			}
			RECT detail_icon = {494, 92, 574, 172};
			draw_recipe_collection(dc, detail_icon, recipe);
			RECT detail_separator = {405, 184, 663, 186}; fill_color(dc, detail_separator, RGB(188, 154, 96));
			SelectObject(dc, small_font); SetTextColor(dc, RGB(68, 47, 31));
			RECT description = {410, 198, 658, 402};
			draw_client_item_description(dc, recipe->description, &description);
			RECT back_button = {469, 414, 599, 441}; fill_round(dc, back_button, 6, RGB(151, 86, 42));
			SetTextColor(dc, RGB(255, 241, 202));
			DrawTextA(dc, "Back to recipe", -1, &back_button, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
		} else {
			RECT product_icon = {493, 88, 573, 168};
			draw_recipe_collection(dc, product_icon, recipe);
			SelectObject(dc, small_font); char line[128];
			wsprintfA(line, "Produces: %d    Success: %d.%02d%%", recipe->amount, recipe->success_rate / 100, recipe->success_rate % 100);
			RECT info = {405, 172, 663, 194}; SetTextColor(dc, RGB(67, 85, 63)); DrawTextA(dc, line, -1, &info, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
			BOOL can_craft = cooking_recipe_can_craft(recipe);
			if (cooking_request_pending_) {
				RECT progress_bar = {408, 197, 660, 220};
				fill_round(dc, progress_bar, 5, RGB(191, 174, 142));
				DWORD started = (DWORD)InterlockedCompareExchange(&cooking_progress_start_, 0, 0);
				DWORD elapsed = started ? GetTickCount() - started : 0;
				int progress = elapsed >= COOKING_PROGRESS_DURATION_MS ? 100 :
					(int)(elapsed * 100 / COOKING_PROGRESS_DURATION_MS);
				RECT progress_fill = progress_bar;
				progress_fill.right = progress_fill.left +
					(progress_fill.right - progress_fill.left) * progress / 100;
				if (progress_fill.right > progress_fill.left)
					fill_round(dc, progress_fill, 5, RGB(151, 86, 42));
				HBRUSH progress_border = CreateSolidBrush(RGB(120, 76, 41));
				FrameRect(dc, &progress_bar, progress_border); DeleteObject(progress_border);
				char progress_label[48];
				wsprintfA(progress_label, progress < 100 ? "Preparing dish... %d%%" : "Finishing dish...", progress);
				SetTextColor(dc, RGB(255, 241, 202));
				DrawTextA(dc, progress_label, -1, &progress_bar, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
			} else {
				RECT details_button = {408, 197, 528, 220}; fill_round(dc, details_button, 5, RGB(222, 199, 153));
				HBRUSH details_border = CreateSolidBrush(RGB(154, 124, 78));
				FrameRect(dc, &details_button, details_border); DeleteObject(details_border);
				SetTextColor(dc, RGB(74, 48, 29));
				DrawTextA(dc, "Item details", -1, &details_button, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
				RECT cook_button = {536, 197, 660, 220};
				fill_round(dc, cook_button, 5, can_craft ? RGB(151, 86, 42) : RGB(166, 153, 130));
				SetTextColor(dc, can_craft ? RGB(255, 241, 202) : RGB(226, 216, 196));
				const char* cook_label = cooking_mode_ < 1 ? "Chef required" : !recipe->unlocked ? "Locked" :
					can_craft ? "Cook" : "Missing items";
				DrawTextA(dc, cook_label, -1, &cook_button, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
			}
			RECT separator = {405, 228, 663, 230}; fill_color(dc, separator, RGB(188, 154, 96));
			SelectObject(dc, normal_font); SetTextColor(dc, RGB(76, 47, 25));
			RECT ingredients_title = {408, 237, 660, 258}; DrawTextA(dc, "Required ingredients", -1, &ingredients_title, DT_LEFT | DT_SINGLELINE);
			SelectObject(dc, small_font);
			int shown = recipe->ingredient_count > 6 ? 6 : recipe->ingredient_count;
			for (int i = 0; i < shown; ++i) {
				CookingIngredient* ingredient = &recipe->ingredients[i];
				int top = 263 + i * 29;
				RECT ingredient_icon = {410, top, 438, top + 27};
				draw_item_image(dc, ingredient_icon, &ingredient->image, &ingredient->image_attempted, ingredient->item_id, ingredient->resource_name);
				RECT ingredient_name = {446, top, 585, top + 27};
				SetTextColor(dc, ingredient->owned >= ingredient->required ? RGB(37, 118, 73) : RGB(174, 54, 45));
				DrawTextA(dc, ingredient->name, -1, &ingredient_name, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
				wsprintfA(line, "%d / %d", ingredient->owned, ingredient->required);
				RECT amounts = {590, top, 660, top + 27}; DrawTextA(dc, line, -1, &amounts, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
			}
			RECT failure = {408, 440, 660, 459};
			const char* footer = cooking_request_pending_ ? "Preparing dish..." : "Select Cook to prepare this dish.";
			COLORREF footer_color = RGB(92, 72, 48);
			if (cooking_result_received_ && cooking_result_ == 0) { footer = "The dish could not be prepared."; footer_color = RGB(165, 55, 45); }
			else if (cooking_result_received_ && cooking_result_ == 1) { footer = "Dish prepared successfully."; footer_color = RGB(35, 125, 72); }
			else if (cooking_result_received_ && cooking_result_ == 2) { footer = "Cooking failed; ingredients consumed."; footer_color = RGB(165, 55, 45); }
			else if (cooking_result_received_ && cooking_result_ == 3) { footer = "Cooking failed; ingredients preserved."; footer_color = RGB(165, 90, 35); }
			else if (cooking_result_received_ && cooking_result_ == 4) { footer = "You no longer have the required items."; footer_color = RGB(165, 55, 45); }
			SetTextColor(dc, footer_color);
			DrawTextA(dc, footer, -1, &failure, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
		}
	}
	if (cooking_category_dropdown_) {
		const int total_options = cooking_category_count_ + 1;
		const int visible_options = total_options < 10 ? total_options : 10;
		RECT dropdown_shadow = {48, 93, 338, 97 + visible_options * 28};
		fill_round(dc, dropdown_shadow, 5, RGB(91, 57, 31));
		RECT dropdown = {45, 90, 334, 94 + visible_options * 28};
		fill_round(dc, dropdown, 5, RGB(247, 235, 202));
		SelectObject(dc, small_font);
		for (int row = 0; row < visible_options; ++row) {
			int option = cooking_category_scroll_ + row;
			if (option >= total_options) break;
			RECT option_box = {48, 93 + row * 28, 321, 120 + row * 28};
			if (option - 1 == cooking_category_) fill_color(dc, option_box, RGB(225, 190, 111));
			SetTextColor(dc, RGB(65, 43, 25));
			RECT option_text = {57, 93 + row * 28, 312, 120 + row * 28};
			DrawTextA(dc, option == 0 ? "All recipes" : cooking_categories_[option - 1], -1,
				&option_text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		}
		if (total_options > visible_options) {
			RECT scroll_track = {322, 95, 330, 90 + visible_options * 28};
			fill_round(dc, scroll_track, 3, RGB(211, 190, 148));
			int thumb_height = (scroll_track.bottom - scroll_track.top) * visible_options / total_options;
			int maximum_scroll = total_options - visible_options;
			int thumb_top = scroll_track.top + (scroll_track.bottom - scroll_track.top - thumb_height) *
				cooking_category_scroll_ / maximum_scroll;
			RECT thumb = {322, thumb_top, 330, thumb_top + thumb_height};
			fill_round(dc, thumb, 3, RGB(139, 91, 46));
		}
	}
	SelectObject(dc, old_font); DeleteObject(title_font); DeleteObject(normal_font); DeleteObject(small_font);
}

static LRESULT CALLBACK cooking_book_proc(HWND window, UINT message, WPARAM w, LPARAM l) {
	if (message == WM_ERASEBKGND) return 1;
	if (message == WM_TIMER && w == 1) {
		if (cooking_book_open_ && IsWindowVisible(window)) InvalidateRect(window, NULL, FALSE);
		return 0;
	}
	if (message == WM_KEYDOWN && w == VK_ESCAPE) {
		if (cooking_category_dropdown_) {
			cooking_category_dropdown_ = 0; InvalidateRect(window, NULL, FALSE); return 0;
		}
		if (cooking_item_details_open_) {
			cooking_item_details_open_ = 0; InvalidateRect(window, NULL, FALSE); return 0;
		}
		cooking_mode_ = 0; InterlockedExchange(&cooking_book_open_, 0); ShowWindow(window, SW_HIDE); return 0;
	}
	if (message == WM_MOUSEWHEEL && cooking_category_dropdown_) {
		const int total_options = cooking_category_count_ + 1;
		const int visible_options = total_options < 10 ? total_options : 10;
		const int maximum_scroll = total_options - visible_options;
		if (GET_WHEEL_DELTA_WPARAM(w) < 0 && cooking_category_scroll_ < maximum_scroll)
			++cooking_category_scroll_;
		else if (GET_WHEEL_DELTA_WPARAM(w) > 0 && cooking_category_scroll_ > 0)
			--cooking_category_scroll_;
		InvalidateRect(window, NULL, FALSE); return 0;
	}
	if (message == WM_LBUTTONDOWN) {
		int x = LOWORD(l), y = HIWORD(l);
		if (x >= 655 && x <= 687 && y >= 18 && y <= 50) { cooking_mode_ = 0; InterlockedExchange(&cooking_book_open_, 0); ShowWindow(window, SW_HIDE); return 0; }
		if (x >= 45 && x <= 334 && y >= 61 && y <= 87) {
			cooking_category_dropdown_ = !cooking_category_dropdown_;
			if (cooking_category_dropdown_) {
				int selected_option = cooking_category_ + 1;
				if (selected_option < cooking_category_scroll_) cooking_category_scroll_ = selected_option;
				if (selected_option >= cooking_category_scroll_ + 10) cooking_category_scroll_ = selected_option - 9;
			}
			InvalidateRect(window, NULL, FALSE); return 0;
		}
		if (cooking_category_dropdown_) {
			const int total_options = cooking_category_count_ + 1;
			const int visible_options = total_options < 10 ? total_options : 10;
			if (x >= 45 && x <= 334 && y >= 90 && y < 94 + visible_options * 28) {
				int row = (y - 93) / 28;
				if (row < 0) row = 0;
				int option = cooking_category_scroll_ + row;
				if (option < total_options) {
					cooking_category_ = option - 1; cooking_category_dropdown_ = 0; reset_cooking_page();
				}
				InvalidateRect(window, NULL, FALSE); return 0;
			}
			cooking_category_dropdown_ = 0;
			InvalidateRect(window, NULL, FALSE); return 0;
		}
		if (!cooking_item_details_open_ && x >= 408 && x <= 528 && y >= 197 && y <= 220) {
			cooking_item_details_open_ = 1; InvalidateRect(window, NULL, FALSE); return 0;
		}
		if (!cooking_item_details_open_ && x >= 536 && x <= 660 && y >= 197 && y <= 220 &&
			cooking_selected_ >= 0 && cooking_selected_ < cooking_recipe_count_) {
			CookingRecipe* selected = &cooking_recipes_[cooking_selected_];
			if (cooking_recipe_can_craft(selected)) {
				cooking_request_recipe(selected->id);
				InvalidateRect(window, NULL, FALSE);
			}
			return 0;
		}
		if (cooking_item_details_open_ && x >= 469 && x <= 599 && y >= 414 && y <= 441) {
			cooking_item_details_open_ = 0; InvalidateRect(window, NULL, FALSE); return 0;
		}
		int pages = (visible_cooking_count() + 5) / 6; if (pages < 1) pages = 1;
		if (x >= 45 && x <= 117 && y >= 435 && y <= 462 && cooking_page_ > 0) { --cooking_page_; cooking_item_details_open_ = 0; }
		else if (x >= 262 && x <= 334 && y >= 435 && y <= 462 && cooking_page_ + 1 < pages) { ++cooking_page_; cooking_item_details_open_ = 0; }
		else if (x >= 45 && x <= 334 && y >= 95 && y < 425) {
			int slot = (y - 95) / 55; int index = visible_cooking_index(cooking_page_ * 6 + slot);
			if (index >= 0) { cooking_selected_ = index; cooking_item_details_open_ = 0; }
		}
		InvalidateRect(window, NULL, FALSE); return 0;
	}
	if (message == WM_PAINT) {
		PAINTSTRUCT paint; HDC dc = BeginPaint(window, &paint); RECT area; GetClientRect(window, &area);
		HDC buffer_dc = CreateCompatibleDC(dc); HBITMAP bitmap = CreateCompatibleBitmap(dc, area.right, area.bottom);
		HBITMAP old = (HBITMAP)SelectObject(buffer_dc, bitmap); draw_cooking_book_window(buffer_dc, area);
		BitBlt(dc, 0, 0, area.right, area.bottom, buffer_dc, 0, 0, SRCCOPY);
		SelectObject(buffer_dc, old); DeleteObject(bitmap); DeleteDC(buffer_dc); EndPaint(window, &paint); return 0;
	}
	return DefWindowProcA(window, message, w, l);
}

static DWORD WINAPI hud_thread(void* unused) {
	(void)unused;
	hook_recv();
	while (!game_) {
		EnumWindows(find_window, (LPARAM)&game_);
		Sleep(100);
	}
	log_line("HikariRO game window found.");
	WNDCLASSA wc = {0};
	wc.lpfnWndProc = hud_proc;
	wc.hInstance = GetModuleHandleA("ddraw.dll");
	wc.lpszClassName = "HROFishingHUD";
	RegisterClassA(&wc);
	hud_ = CreateWindowExA(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT | WS_EX_LAYERED,
		wc.lpszClassName, "", WS_POPUP, 0, 0, 500, 237, game_, NULL, wc.hInstance, NULL);
	SetLayeredWindowAttributes(hud_, 0, 246, LWA_ALPHA);
	SetWindowLongPtrA(hud_, GWLP_HWNDPARENT, (LONG_PTR)game_);
	log_line(hud_ ? "Fishing HUD window created." : "ERROR: HUD window creation failed.");
	WNDCLASSA album_class = {0};
	album_class.lpfnWndProc = album_proc;
	album_class.hInstance = wc.hInstance;
	album_class.hCursor = LoadCursor(NULL, IDC_HAND);
	album_class.lpszClassName = "HROFishingAlbum";
	RegisterClassA(&album_class);
	album_ = CreateWindowExA(WS_EX_TOOLWINDOW | WS_EX_LAYERED,
		album_class.lpszClassName, "Fishing Album", WS_POPUP, 0, 0, 720, 490,
		game_, NULL, album_class.hInstance, NULL);
	SetLayeredWindowAttributes(album_, 0, 250, LWA_ALPHA);
	SetTimer(album_, 1, 120, NULL);
	SetWindowLongPtrA(album_, GWLP_HWNDPARENT, (LONG_PTR)game_);
	log_line(album_ ? "Fishing Album window created." : "ERROR: Fishing Album creation failed.");
	WNDCLASSA card_class = {0};
	card_class.lpfnWndProc = card_album_proc;
	card_class.hInstance = wc.hInstance;
	card_class.hCursor = LoadCursor(NULL, IDC_HAND);
	card_class.lpszClassName = "HROCardAlbum";
	RegisterClassA(&card_class);
	card_album_ = CreateWindowExA(WS_EX_TOOLWINDOW | WS_EX_LAYERED,
		card_class.lpszClassName, "HikariRO Card Album", WS_POPUP, 0, 0, 720, 500,
		game_, NULL, card_class.hInstance, NULL);
	SetLayeredWindowAttributes(card_album_, 0, 255, LWA_ALPHA);
	SetTimer(card_album_, 1, 120, NULL);
	SetWindowLongPtrA(card_album_, GWLP_HWNDPARENT, (LONG_PTR)game_);
	log_line(card_album_ ? "Card Album window created." : "ERROR: Card Album creation failed.");
	WNDCLASSA cooking_class = {0};
	cooking_class.lpfnWndProc = cooking_book_proc;
	cooking_class.hInstance = wc.hInstance;
	cooking_class.hCursor = LoadCursor(NULL, IDC_HAND);
	cooking_class.lpszClassName = "HROCookingBook";
	RegisterClassA(&cooking_class);
	cooking_book_ = CreateWindowExA(WS_EX_TOOLWINDOW | WS_EX_LAYERED,
		cooking_class.lpszClassName, "Recipe Book", WS_POPUP, 0, 0, 720, 500,
		game_, NULL, cooking_class.hInstance, NULL);
	SetLayeredWindowAttributes(cooking_book_, 0, 255, LWA_ALPHA);
	SetTimer(cooking_book_, 1, 120, NULL);
	SetWindowLongPtrA(cooking_book_, GWLP_HWNDPARENT, (LONG_PTR)game_);
	log_line(cooking_book_ ? "Cooking Recipe Book window created." : "ERROR: Recipe Book creation failed.");
	MSG message;
	for (;;) {
		while (PeekMessageA(&message, NULL, 0, 0, PM_REMOVE)) {
			TranslateMessage(&message);
			DispatchMessageA(&message);
		}
		hook_raghikari_recv_pointer();
		if (!IsWindow(game_) || !IsWindowVisible(game_)) {
			HWND current = NULL;
			EnumWindows(find_window, (LPARAM)&current);
			if (current && current != game_) {
				game_ = current;
				SetWindowLongPtrA(hud_, GWLP_HWNDPARENT, (LONG_PTR)game_);
				SetWindowLongPtrA(album_, GWLP_HWNDPARENT, (LONG_PTR)game_);
				SetWindowLongPtrA(card_album_, GWLP_HWNDPARENT, (LONG_PTR)game_);
				SetWindowLongPtrA(cooking_book_, GWLP_HWNDPARENT, (LONG_PTR)game_);
				log_line("HikariRO game window updated.");
			}
		}
		int state = (int)state_;
		if (state >= 4 && state <= 6 && GetTickCount() >= result_until_) {
			InterlockedExchange(&state_, 0);
			state = 0;
		}
		if (state && IsWindowVisible(game_) && !IsIconic(game_)) {
			shown_tension_ += ((float)tension_ - shown_tension_) * .16f;
			shown_distance_ += ((float)distance_ - shown_distance_) * .16f;
			RECT client;
			POINT origin = {0, 0};
			GetClientRect(game_, &client);
			ClientToScreen(game_, &origin);
			int width = state == 3 ? 500 : 430;
			int height = state == 3 ? 237 : (state >= 4 ? 118 : 94);
			int x = origin.x + (client.right - width) / 2;
			SetWindowPos(hud_, HWND_TOPMOST, x, origin.y + 105, width, height,
				SWP_NOACTIVATE | SWP_SHOWWINDOW);
			InvalidateRect(hud_, NULL, FALSE);
		} else {
			ShowWindow(hud_, SW_HIDE);
		}
		if (album_open_ && IsWindowVisible(game_) && !IsIconic(game_)) {
			RECT client;
			POINT origin = {0, 0};
			GetClientRect(game_, &client);
			ClientToScreen(game_, &origin);
			int album_x = origin.x + (client.right - 720) / 2;
			int album_y = origin.y + (client.bottom - 490) / 2;
			RECT current;
			GetWindowRect(album_, &current);
			if (!IsWindowVisible(album_) || current.left != album_x || current.top != album_y) {
				BOOL was_visible = IsWindowVisible(album_);
				SetWindowPos(album_, HWND_TOPMOST, album_x, album_y, 720, 490,
					SWP_NOACTIVATE | SWP_SHOWWINDOW);
				if (!was_visible) InvalidateRect(album_, NULL, FALSE);
			}
		} else {
			ShowWindow(album_, SW_HIDE);
		}
		if (card_album_open_ && IsWindowVisible(game_) && !IsIconic(game_)) {
			RECT client; POINT origin = {0, 0}; GetClientRect(game_, &client); ClientToScreen(game_, &origin);
			int x = origin.x + (client.right - 720) / 2;
			int y = origin.y + (client.bottom - 500) / 2;
			RECT current; GetWindowRect(card_album_, &current);
			if (!IsWindowVisible(card_album_) || current.left != x || current.top != y) {
				BOOL was_visible = IsWindowVisible(card_album_);
				SetWindowPos(card_album_, HWND_TOPMOST, x, y, 720, 500, SWP_NOACTIVATE | SWP_SHOWWINDOW);
				if (!was_visible) InvalidateRect(card_album_, NULL, FALSE);
			}
		} else ShowWindow(card_album_, SW_HIDE);
		if (cooking_book_open_ && IsWindowVisible(game_) && !IsIconic(game_)) {
			RECT client; POINT origin = {0, 0}; GetClientRect(game_, &client); ClientToScreen(game_, &origin);
			int x = origin.x + (client.right - 720) / 2;
			int y = origin.y + (client.bottom - 500) / 2;
			RECT current; GetWindowRect(cooking_book_, &current);
			if (!IsWindowVisible(cooking_book_) || current.left != x || current.top != y) {
				BOOL was_visible = IsWindowVisible(cooking_book_);
				SetWindowPos(cooking_book_, HWND_TOPMOST, x, y, 720, 500, SWP_NOACTIVATE | SWP_SHOWWINDOW);
				if (!was_visible) InvalidateRect(cooking_book_, NULL, FALSE);
			}
		} else ShowWindow(cooking_book_, SW_HIDE);
		Sleep(33);
	}
}

static void load_ddraw(void) {
	char path[MAX_PATH];
	GetSystemDirectoryA(path, MAX_PATH);
	lstrcatA(path, "\\ddraw.dll");
	real_ddraw = LoadLibraryA(path);
	real_create = (CreateFn)GetProcAddress(real_ddraw, "DirectDrawCreateEx");
	real_enum = (EnumFn)GetProcAddress(real_ddraw, "DirectDrawEnumerateExA");
}

__declspec(dllexport) HRESULT WINAPI DirectDrawCreateEx(GUID* guid, void** output, REFIID iid, void* outer) {
	if (!real_create) load_ddraw();
	return real_create ? real_create(guid, output, iid, outer) : E_FAIL;
}

__declspec(dllexport) HRESULT WINAPI DirectDrawEnumerateExA(void* callback, void* context, DWORD flags) {
	if (!real_enum) load_ddraw();
	return real_enum ? real_enum(callback, context, flags) : E_FAIL;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved) {
	(void)reserved;
	if (reason == DLL_PROCESS_ATTACH) {
		DisableThreadLibraryCalls(instance);
		log_line("HRO Fishing UI + Card Album + Cooking Recipe Book DLL V26.2 loaded.");
		load_ddraw();
		CreateThread(NULL, 0, hud_thread, NULL, 0, NULL);
	}
	return TRUE;
}
