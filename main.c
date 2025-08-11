#include "include/raylib.h"
#include "include/microui.h"
#include "include/murl.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

char manifest_raw[] = {
#embed "manifest.csv"
};

char mechanics_raw[] = {
#embed "mechanics.csv"
};

#define new(a, n, t)    (t *)alloc(a, n, sizeof(t), _Alignof(t))

typedef struct {
        char *beg;
        char *end;
} Arena;

void *alloc(Arena *a, ptrdiff_t count, ptrdiff_t size, ptrdiff_t align)
{
        ptrdiff_t padding = -(uintptr_t)a->beg & (align - 1);
        ptrdiff_t available = a->end - a->beg - padding;
        if (available < 0 || count > available/size) {
                abort();  // one possible out-of-memory policy
        }
        void *p = a->beg + padding;
        a->beg += padding + count*size;
        return memset(p, 0, count*size);
}

#define S(s)    (Str){s, sizeof(s)-1}
#define print(s) printf("%.*s", s.len, s.data)

typedef struct 
{
        char *data;
        ptrdiff_t len;
} Str;

char *to_c_perm(Arena *perm, Str s)
{
	char *ret = new(perm, s.len + 1, char);
	memcpy(ret, s.data, s.len);
	ret[s.len] = '\0';
	return ret;
}

char *to_c(Arena scratch, Str s)
{
	return to_c_perm(&scratch, s);
}

Str span(char *beg, char *end)
{
        Str r = {0};
        r.data = beg;
        r.len  = beg ? end-beg : 0;
        return r;
}

_Bool equals(Str a, Str b)
{
        return a.len==b.len && (!a.len || !memcmp(a.data, b.data, a.len));
}

Str trimleft(Str s)
{
        for (; s.len && *s.data<=' '; s.data++, s.len--) {}
        return s;
}

Str trimright(Str s)
{
        for (; s.len && s.data[s.len-1]<=' '; s.len--) {}
        return s;
}

Str substring(Str s, ptrdiff_t i)
{
        if (i) {
                s.data += i;
                s.len  -= i;
        }
        return s;
}

typedef struct {
        Str   head;
        Str   tail;
        _Bool ok;
} Cut;

Cut cut(Str s, char delim)
{
        Cut r = {0};
        if (!s.len) return r;

        char *beg = s.data;
        char *end = s.data + s.len;
        char *cut = beg;
        _Bool in_string = 0;

        while (cut < end) {
                if (*cut == '"') {
                        in_string = !in_string;  // toggle quoted state
                } else if (*cut == delim && !in_string) {
                        break;  // only cut on delimiter outside of quotes
                }
                cut++;
        }

        r.ok   = cut < end;
        r.head = span(beg, cut);
        r.tail = span(cut + r.ok, end);  // skip delimiter if found
        return r;
}

typedef struct TextureMap TextureMap;
struct TextureMap
{
	TextureMap *child[4];
	Str key;
	Texture value;
};

TextureMap *arcs_textures = {0};
Texture default_texture = {0};

uint64_t hash(Str s)
{
	uint64_t h = 0x100;
	for (ptrdiff_t i = 0; i < s.len; i++)
	{
		h ^= s.data[i];
		h *= 1111111111111111111u;
	}
	return h;
}

Texture *get_texture(TextureMap **m, Str key, Arena *perm)
{
	for (uint64_t h = hash(key); *m; h <<= 2)
	{
		if (equals(key, (*m)->key))
		{
			Texture *tex = &(*m)->value;
			if (tex->id == 0 && key.len > 0)
			{
				char *path = new(perm, 1 + key.len + sizeof("cards/content/card-images/arcs/en-US/") + sizeof(".png"), char);
				sprintf(path, "cards/content/card-images/arcs/en-US/%s.png", to_c(*perm, key));
				*tex = LoadTexture(path);
			}
			return tex;
		}
		m = &(*m)->child[h >> 62];
	}
	if (!perm)
	{
		return &default_texture;
	}

	*m = new(perm, 1, TextureMap);
	(*m)->key = key;
	return &(*m)->value;
}

typedef enum 
{
        Act_None,
        Act_A,
        Act_B,
        Act_C,
} Act;

typedef enum 
{
        ElementType_None,
        ElementType_Fate,
        ElementType_GameItem,
} ElementType;

typedef struct StringLink StringLink;
struct StringLink
{
        Str str;
        StringLink *next;
};

typedef struct Element Element;
struct Element
{
        ElementType type;
        Element *parent_fate;
        Act act;
        Str name;
	Str id;
        Str note;
        StringLink *mechanics;
        StringLink *mechanics_end;
};

Str *mechanics_names = 0;
int mechanics_len = 0;

int main(void)
{
        Arena perm = {0};
        perm.beg = malloc(1 << 30);
        perm.end = perm.beg + (1 << 30);

	SetConfigFlags(FLAG_WINDOW_RESIZABLE);
	InitWindow(800, 600, "ArcsViz");

	default_texture = LoadTexture("cards/content/card-images/arcs/en-US/EVENT01.png");
	FilePathList arcs_images = LoadDirectoryFilesEx("cards/content/card-images/arcs/en-US", ".png", false);
	for (int i = 0; i < arcs_images.count; i++)
	{
		char *filename = (char*) GetFileNameWithoutExt(arcs_images.paths[i]);
		char *name = new(&perm, strlen(filename), char);
		memcpy(name, filename, strlen(filename) * sizeof(*name));
		get_texture(&arcs_textures, (Str) {name, strlen(filename)}, &perm);
	}
	UnloadDirectoryFiles(arcs_images);

        Str manifest = {0};
        manifest.data = manifest_raw;
        manifest.len  = sizeof manifest_raw;

        Str mechanics = {0};
        mechanics.data = mechanics_raw;
        mechanics.len  = sizeof mechanics_raw;

        ElementType current_type = ElementType_None;
        Element *elements = new(&perm, 2000, Element);
        Element *element_curr = elements;
        int elements_len = 0;

        Element *current_fate      = 0;
        Element *current_game_item = 0;

        Cut c = {0};
	c.tail = mechanics;
	_Bool header_skipped = false;
	while (c.tail.len)
	{
		c = cut(c.tail, '\n');
		if (header_skipped)
		{
			mechanics_len += 1;
		}
		else
		{
			header_skipped = true;
		}
	}

	c.tail = mechanics;
	header_skipped = false;
	mechanics_names = new(&perm, mechanics_len, Str);
	{
		int index = 0;
		while (c.tail.len)
		{
			c = cut(c.tail, '\n');
			if (header_skipped)
			{
				Cut mechanic = cut(c.head, ',');
				mechanics_names[index] = mechanic.head;
				index += 1;
			}
			else
			{
				header_skipped = true;
			}
		}
	}

        c.tail = manifest;

        Str *cells_temp = new(&perm, 6, Str);
        while (c.tail.len)
        {
                Str *cells_curr = cells_temp;
                int cells_len = 0;

                c = cut(c.tail, '\n');

                Cut line = {0};
                line.tail = c.head;
                while (line.tail.len)
                {
                        line = cut(line.tail, ',');
                        Str elm = line.head;
                        *cells_curr = elm;
                        cells_curr++;
                        cells_len++;
                }

                if (cells_len > 1) // non empty row?
                {
                        if (equals(cells_temp[1], S("A")) ||
                            equals(cells_temp[1], S("B")) ||
                            equals(cells_temp[1], S("C"))) // Fate?
                        {
                                current_fate = element_curr;
                                element_curr++;

                                current_fate->type = ElementType_Fate;
                                current_fate->name = cells_temp[0];
                                if (equals(cells_temp[1], S("A")))
                                {
                                        current_fate->act = Act_A;
                                }
                                else if (equals(cells_temp[1], S("B")))
                                {
                                        current_fate->act = Act_B;
                                }
                                else if (equals(cells_temp[1], S("C")))
                                {
                                        current_fate->act = Act_C;
                                }
                        }
                        else // Found game item
                        {
                                if (cells_temp[1].len > 0)
                                {
                                        current_game_item = element_curr;
                                        element_curr++;

                                        current_game_item->type = ElementType_GameItem;
                                        current_game_item->parent_fate = current_fate;
                                        current_game_item->name = cells_temp[1];
					current_game_item->id   = cells_temp[3];
                                        current_game_item->note = cells_temp[4];

                                        current_game_item->mechanics = new(&perm, 1, StringLink);
                                        current_game_item->mechanics_end = current_game_item->mechanics;

                                        current_game_item->mechanics_end->str = cells_temp[2];
                                        current_game_item->mechanics_end->next = 0;
                                }
                                else if (cells_temp[2].len > 0) // add mechanic to current game item
                                {
                                        current_game_item->mechanics_end->next = new(&perm, 1, StringLink);
                                        current_game_item->mechanics_end = current_game_item->mechanics_end->next;

                                        current_game_item->mechanics_end->str  = cells_temp[2];
                                        current_game_item->mechanics_end->next = 0;
                                }
                        }
                }
        }

	// Precompute mechanic co-occurrence table
	int **pair_counts = new(&perm, mechanics_len, int*);
	for (int i = 0; i < mechanics_len; i++) {
		pair_counts[i] = new(&perm, mechanics_len, int);
		for (int j = 0; j < mechanics_len; j++) {
			pair_counts[i][j] = 0;
		}
	}

	for (int e = 0; e < element_curr - elements; e++) {
		Element *element = &elements[e];
		int mech_indices[128];
		int mech_count = 0;
		for (StringLink *mech = element->mechanics; mech; mech = mech->next) {
			for (int mi = 0; mi < mechanics_len; mi++) {
				if (equals(mechanics_names[mi], mech->str)) {
					mech_indices[mech_count++] = mi;
					break;
				}
			}
		}
		for (int a = 0; a < mech_count; a++) {
			for (int b = 0; b < mech_count; b++) {
				pair_counts[mech_indices[a]][mech_indices[b]]++;
			}
		}
	}

	// Detail view state
	int selected_i = -1;
	int selected_j = -1;
	_Bool detail_open = false;

	mu_Context *ctx = malloc(sizeof(mu_Context));
	mu_init(ctx);
	murl_setup_font(ctx);

	while (!WindowShouldClose())
	{
		murl_handle_input(ctx);
		mu_begin(ctx);

		if (mu_begin_window_ex(ctx, "Arcz", mu_rect(20, 20, 1600, 1000), MU_OPT_NOCLOSE))
		{
			int *mechanics_widths = new(&perm, mechanics_len + 1, int);
			mechanics_widths[0] = 100;
			for (int i = 1; i < mechanics_len + 1; i++) {
				mechanics_widths[i] = 170; 
			}

			mu_layout_row(ctx, mechanics_len + 1, mechanics_widths, 0);
			mu_label(ctx, "Mechanic Name");
			for (int i = 0; i < mechanics_len; i++) {
				mu_label(ctx, to_c(perm, mechanics_names[i]));
			}

			int button_number = 0;
			for (int i = 0; i < mechanics_len; i++) {
				mu_label(ctx, to_c(perm, mechanics_names[i]));
				for (int j = 0; j < mechanics_len; j++) {
					if (i == j) {
						mu_label(ctx, "_");
					} else {
						char formatbuffer[100];
						sprintf(formatbuffer, "%d", pair_counts[i][j]);

						button_number += 1;
						mu_push_id(ctx, &button_number, sizeof button_number);

						mu_Style old_style = *ctx->style;
						float t = (float)pair_counts[i][j] / 20.0f; // 20 = scale factor, adjust as needed
						if (t > 1.0f) t = 1.0f;
						mu_Color tint = (mu_Color){
							(unsigned char)(255 * t),            // R increases with count
								(unsigned char)(0),
								(unsigned char)(255 * (1.0f - t)),    // B decreases with count
								255};
						if (pair_counts[i][j] > 0)
							ctx->style->colors[6] = tint;
						if (mu_button(ctx, formatbuffer)) {
							selected_i = i;
							selected_j = j;
							detail_open = true;
						}
						*ctx->style = old_style;

						mu_pop_id(ctx);
					}
				}
			}

			mu_end_window(ctx);
		}

		// Detail window
		if (detail_open) {
			char titlebuf[1024] = {0};
			{
				Arena scratch = perm;
				snprintf(titlebuf, sizeof(titlebuf), "%s x %s",
						to_c_perm(&scratch, mechanics_names[selected_i]),
						to_c_perm(&scratch, mechanics_names[selected_j]));
			}

			if (mu_begin_window(ctx, titlebuf, mu_rect(200, 40, 600, 600))) {
				for (int e = 0; e < element_curr - elements; e++) {
					Element *el = &elements[e];
					if (el->type != ElementType_GameItem)
						continue;

					_Bool has_i = false, has_j = false;
					for (StringLink *mech = el->mechanics; mech; mech = mech->next) {
						if (equals(mechanics_names[selected_i], mech->str)) has_i = true;
						if (equals(mechanics_names[selected_j], mech->str)) has_j = true;
					}
					if (!(has_i && has_j)) continue;

					mu_push_id(ctx, &el->id, sizeof(el->id));
					if (mu_begin_treenode(ctx, to_c(perm, el->name))) {
						int old_height = ctx->style->size.y;

						ctx->style->size.y = 384;
						mu_Rect r = mu_layout_next(ctx);
						Texture *tex = get_texture(&arcs_textures, el->id, &perm);

						int clipped = mu_check_clip(ctx, r);
						if (clipped == MU_CLIP_ALL) {
							ctx->style->size.y = old_height;
							goto skip_image;
						}
						mu_Rect unclipped_rect = mu_get_clip_rect(ctx);
						if (clipped == MU_CLIP_PART) {
							mu_set_clip(ctx, mu_get_clip_rect(ctx));
						}


						mu_Command *cmd = mu_push_command(ctx, MU_COMMAND_IMAGE, sizeof(mu_ImageCommand));
						cmd->image.img = tex;
						cmd->image.rect = (mu_Rect) {r.x, r.y, r.w, r.h};

						if (clipped) {
							mu_set_clip(ctx, unclipped_rect);
						}

skip_image:
						ctx->style->size.y = old_height; 

						for (StringLink *mech = el->mechanics; mech; mech = mech->next) {
							mu_label(ctx, to_c(perm, mech->str));
						}
						mu_end_treenode(ctx);
					}
					mu_pop_id(ctx);
				}
				mu_end_window(ctx);
			}
		}

		mu_end(ctx);

		BeginDrawing();
		ClearBackground(BLACK);
		murl_render(ctx);
		EndDrawing();
	}

        return 0;
}
