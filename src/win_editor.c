/* LikeEdit: vi-inspired, not Vim compatible. Windows console + conio fallback. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <conio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <stdint.h>
#include <ctype.h>
#include "editor_core.h"
#include "syntax.h"

#define MAX_PROMPT 4096
#define TAB_WIDTH 4

typedef enum { MODE_COMMAND, MODE_EDIT, MODE_PICKER } EditorMode;
typedef enum { PROMPT_NONE, PROMPT_EX, PROMPT_OPEN, PROMPT_SAVE } PromptMode;
typedef struct {
    EdWorkspace ws;
    EditorMode mode;
    PromptMode prompt_mode;
    char prompt[MAX_PROMPT];
    size_t prompt_len;
    char message[512];
    int running;
    int pending_g;
    int pending_d;
    int show_numbers;
    size_t picker_index;
    size_t picker_top;
    unsigned pending_high_surrogate;
    HANDLE input;
    HANDLE output;
    DWORD old_input_mode;
    DWORD old_output_mode;
    UINT old_output_codepage;
    CONSOLE_CURSOR_INFO old_cursor;
    int has_old_cursor;
    SMALL_RECT viewport;
    int width;
    int height;
    CHAR_INFO *screen;
} EditorApp;

#define FG_WHITE (FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE)
#define FG_LIGHT (FG_WHITE | FOREGROUND_INTENSITY)
#define BG_BLUE (BACKGROUND_BLUE)
#define BG_GRAY (BACKGROUND_RED | BACKGROUND_GREEN | BACKGROUND_BLUE)
#define COLOR_NORMAL FG_LIGHT
#define COLOR_GUTTER (FOREGROUND_RED | FOREGROUND_GREEN)
#define COLOR_STATUS (FG_LIGHT | BG_BLUE)
#define COLOR_PICKER (FG_LIGHT | BG_BLUE)
#define COLOR_SELECTED (FG_LIGHT | BACKGROUND_GREEN | BACKGROUND_BLUE)
#define COLOR_DIM (FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE)

static WORD syntax_attr(unsigned color) {
    switch (color) {
    case ED_COLOR_KEYWORD: return FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY;
    case ED_COLOR_STRING: return FOREGROUND_GREEN | FOREGROUND_INTENSITY;
    case ED_COLOR_NUMBER: return FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY;
    case ED_COLOR_COMMENT: return FOREGROUND_GREEN;
    case ED_COLOR_DIRECTIVE: return FOREGROUND_RED | FOREGROUND_BLUE | FOREGROUND_INTENSITY;
    case ED_COLOR_TYPE: return FOREGROUND_BLUE | FOREGROUND_GREEN | FOREGROUND_INTENSITY;
    case ED_COLOR_OPERATOR: return FOREGROUND_RED | FOREGROUND_INTENSITY;
    default: return COLOR_NORMAL;
    }
}

static void app_message(EditorApp *a, const char *message) {
    snprintf(a->message, sizeof(a->message), "%s", message);
}

static int app_any_dirty(const EditorApp *a) {
    size_t i;
    for (i = 0; i < a->ws.count; ++i) if (a->ws.items[i]->changed) return 1;
    return 0;
}

static int utf8_decode(const char *s, size_t n, size_t at, unsigned *cp) {
    size_t next = ed_utf8_next(s, n, at), k;
    unsigned char lead = (unsigned char)s[at];
    if (next - at == 1) { *cp = lead; return 1; }
    *cp = lead & (next - at == 2 ? 0x1F : (next - at == 3 ? 0x0F : 0x07));
    for (k = at + 1; k < next; ++k) *cp = (*cp << 6) | ((unsigned char)s[k] & 0x3F);
    return (int)(next - at);
}

static int visual_width(unsigned codepoint, int current) {
    if (codepoint == '\t') return TAB_WIDTH - current % TAB_WIDTH;
    if (codepoint < 32 || codepoint == 127) return 2; /* display control chars as ^X */
    /* BMP full-width CJK/ideographs are approximated by 2 cells. */
    if ((codepoint >= 0x1100 && codepoint <= 0x115F) ||
        (codepoint >= 0x2E80 && codepoint <= 0xA4CF) ||
        (codepoint >= 0xAC00 && codepoint <= 0xD7A3) ||
        (codepoint >= 0xF900 && codepoint <= 0xFAFF) ||
        (codepoint >= 0xFE10 && codepoint <= 0xFE6F) ||
        (codepoint >= 0xFF01 && codepoint <= 0xFF60)) return 2;
    return 1;
}

static size_t visual_column(const EdLine *line, size_t byte_end) {
    size_t pos = 0, column = 0;
    if (byte_end > line->length) byte_end = line->length;
    while (pos < byte_end) {
        unsigned cp;
        pos += (size_t)utf8_decode(line->text, line->length, pos, &cp);
        column += (size_t)visual_width(cp, (int)column);
    }
    return column;
}

static void cell(EditorApp *a, int x, int y, wchar_t glyph, WORD attributes) {
    CHAR_INFO *slot;
    if (x < 0 || y < 0 || x >= a->width || y >= a->height) return;
    slot = &a->screen[(size_t)y * (size_t)a->width + (size_t)x];
    slot->Char.UnicodeChar = glyph;
    slot->Attributes = attributes;
}

static void background(EditorApp *a, int start, int end, WORD attr) {
    int x, y;
    for (y = start; y < end && y < a->height; ++y)
        for (x = 0; x < a->width; ++x) cell(a, x, y, L' ', attr);
}

static void draw_utf8(EditorApp *a, int x, int y, const char *s, WORD color, int maxwidth) {
    size_t pos, len = strlen(s);
    int offset = 0;
    for (pos = 0; pos < len && offset < maxwidth;) {
        unsigned cp;
        pos += (size_t)utf8_decode(s, len, pos, &cp);
        if (cp == '\t') cp = ' ';
        if (cp < 32 || cp > 0xFFFF || (cp >= 0xD800 && cp <= 0xDFFF)) cp = '?';
        cell(a, x + offset, y, (wchar_t)cp, color);
        offset++;
    }
}

static void format_status(EditorApp *a, char *out, size_t n) {
    EdBuffer *b = ed_workspace_current(&a->ws);
    if (!b) { snprintf(out, n, "LikeEdit | no buffers"); return; }
    snprintf(out, n, " %s | %s%s | %s | Ln %llu, Col %llu | %llu file(s) | Ctrl+Shift+T: files ",
        a->mode == MODE_EDIT ? "EDIT" : a->mode == MODE_COMMAND ? "COMMAND" : "FILES",
        ed_buffer_name(b), b->changed ? " [+]" : "",
        ed_syntax_name(ed_syntax_detect(b->path)),
        (unsigned long long)(b->row + 1),
        (unsigned long long)(ed_utf8_count(b->lines[b->row].text, b->col) + 1),
        (unsigned long long)a->ws.count);
}

static void update_scroll(EdBuffer *b, int viewrows, int viewcols) {
    size_t x;
    if (viewrows < 1 || viewcols < 1) return;
    if (b->row < b->scroll_row) b->scroll_row = b->row;
    if (b->row >= b->scroll_row + (size_t)viewrows) b->scroll_row = b->row - (size_t)viewrows + 1;
    x = visual_column(&b->lines[b->row], b->col);
    if (x < b->scroll_col) b->scroll_col = x;
    if (x >= b->scroll_col + (size_t)viewcols) b->scroll_col = x - (size_t)viewcols + 1;
}

static int draw_source(EditorApp *a, EdBuffer *b, int viewrows, int gutter) {
    int y, block_comment = 0;
    size_t line_index;
    EdLanguage lang = ed_syntax_detect(b->path);
    /* Propagate lexical block-comment state from the top of the file. */
    for (line_index = 0; line_index < b->scroll_row && line_index < b->line_count; ++line_index) {
        const EdLine *line = &b->lines[line_index];
        unsigned char *temporary = (unsigned char *)malloc(line->length ? line->length : 1);
        if (!temporary) return 0;
        ed_syntax_line(lang, line->text, line->length, temporary, &block_comment);
        free(temporary);
    }
    for (y = 0; y < viewrows; ++y) {
        size_t line_no = b->scroll_row + (size_t)y;
        EdLine *line;
        unsigned char *colors;
        size_t pos;
        int visual = 0;
        if (line_no >= b->line_count) {
            if (gutter) cell(a, gutter - 2, y, L'~', COLOR_GUTTER);
            continue;
        }
        line = &b->lines[line_no];
        if (gutter) {
            char number[32];
            snprintf(number, sizeof(number), "%4llu ", (unsigned long long)(line_no + 1));
            draw_utf8(a, 0, y, number, COLOR_GUTTER, gutter);
        }
        colors = (unsigned char *)malloc(line->length ? line->length : 1);
        if (!colors) return 0;
        ed_syntax_line(lang, line->text, line->length, colors, &block_comment);
        for (pos = 0; pos < line->length;) {
            unsigned cp, j;
            size_t advance = (size_t)utf8_decode(line->text, line->length, pos, &cp);
            WORD attr = syntax_attr(colors[pos]);
            int width = visual_width(cp, visual);
            for (j = 0; j < (unsigned)width; ++j) {
                int cx = visual + (int)j - (int)b->scroll_col;
                wchar_t glyph;
                if (cx < 0 || cx >= a->width - gutter) continue;
                glyph = j ? L' ' : cp == '\t' ? L' ' :
                    cp < 32 ? L'^' : cp > 0xFFFF || (cp >= 0xD800 && cp <= 0xDFFF) ? L'?' : (wchar_t)cp;
                cell(a, gutter + cx, y, glyph, attr);
            }
            visual += width;
            pos += advance;
            if (visual > a->width + (int)b->scroll_col) break;
        }
        free(colors);
    }
    return 1;
}

static void draw_picker(EditorApp *a) {
    int top = a->height / 2, list_start = top + 2, list_rows = a->height - top - 3;
    int y;
    if (list_rows < 1) list_rows = 1;
    if (a->picker_index < a->picker_top) a->picker_top = a->picker_index;
    if (a->picker_index >= a->picker_top + (size_t)list_rows)
        a->picker_top = a->picker_index - (size_t)list_rows + 1;
    background(a, top, a->height, COLOR_PICKER);
    {
        char title[128];
        snprintf(title, sizeof(title), " FILE SELECTOR - %llu open files ", (unsigned long long)a->ws.count);
        draw_utf8(a, 1, top, title, COLOR_PICKER, a->width - 2);
    }
    draw_utf8(a, 2, top + 1, "Up/Down or j/k: move  |  Enter: select and EDIT  |  Esc: cancel", COLOR_PICKER, a->width - 3);
    for (y = 0; y < list_rows && list_start + y < a->height - 1; ++y) {
        size_t index = a->picker_top + (size_t)y;
        char line[1024];
        WORD attr;
        if (index >= a->ws.count) break;
        attr = index == a->picker_index ? COLOR_SELECTED : COLOR_PICKER;
        if (index == a->picker_index) {
            int x;
            for (x = 0; x < a->width; ++x) cell(a, x, list_start + y, L' ', attr);
        }
        snprintf(line, sizeof(line), " %c %3llu  %s%s", index == a->picker_index ? '>' : ' ',
            (unsigned long long)(index + 1),
            a->ws.items[index]->path ? a->ws.items[index]->path : ed_buffer_name(a->ws.items[index]),
            a->ws.items[index]->changed ? "   [modified]" : "");
        draw_utf8(a, 1, list_start + y, line, attr, a->width - 2);
    }
    draw_utf8(a, 2, a->height - 1, "Ctrl+Shift+T (or Ctrl+T / :buffers) opens this selector from command mode", COLOR_PICKER, a->width - 3);
}

static void render(EditorApp *a) {
    CONSOLE_SCREEN_BUFFER_INFO console;
    COORD size, origin = {0, 0}, caret;
    SMALL_RECT region;
    CHAR_INFO *next;
    EdBuffer *b = ed_workspace_current(&a->ws);
    int gutter, viewrows, cursorx, cursory;
    char status[1024];
    CONSOLE_CURSOR_INFO ci;
    if (!GetConsoleScreenBufferInfo(a->output, &console)) return;
    a->viewport = console.srWindow;
    a->width = console.srWindow.Right - console.srWindow.Left + 1;
    a->height = console.srWindow.Bottom - console.srWindow.Top + 1;
    if (a->width < 1 || a->height < 1) return;
    if ((size_t)a->width > SIZE_MAX / (size_t)a->height / sizeof(CHAR_INFO)) return;
    next = (CHAR_INFO *)realloc(a->screen, (size_t)a->width * (size_t)a->height * sizeof(CHAR_INFO));
    if (!next) return;
    a->screen = next;
    background(a, 0, a->height, COLOR_NORMAL);
    if (a->width < 32 || a->height < 9) {
        draw_utf8(a, 0, 0, "Enlarge terminal to at least 32 x 9", FG_LIGHT, a->width);
        cursorx = cursory = 0;
    } else {
        gutter = a->show_numbers ? 6 : 0;
        viewrows = a->mode == MODE_PICKER ? a->height / 2 : a->height - 2;
        if (b) {
            update_scroll(b, viewrows, a->width - gutter);
            if (!draw_source(a, b, viewrows, gutter)) app_message(a, "Not enough memory to color text");
        }
        if (a->mode == MODE_PICKER) {
            draw_picker(a);
            cursorx = 2;
            cursory = a->height - 1;
        } else {
            background(a, a->height - 2, a->height - 1, COLOR_STATUS);
            format_status(a, status, sizeof(status));
            draw_utf8(a, 0, a->height - 2, status, COLOR_STATUS, a->width);
            if (a->prompt_mode != PROMPT_NONE) {
                const char *prefix = a->prompt_mode == PROMPT_EX ? ":" :
                    a->prompt_mode == PROMPT_OPEN ? "Open: " : "Save as: ";
                draw_utf8(a, 0, a->height - 1, prefix, COLOR_NORMAL, a->width);
                draw_utf8(a, (int)strlen(prefix), a->height - 1, a->prompt,
                          COLOR_NORMAL, a->width - (int)strlen(prefix));
                cursorx = (int)strlen(prefix) + (int)ed_utf8_count(a->prompt, a->prompt_len);
                cursory = a->height - 1;
            } else {
                draw_utf8(a, 0, a->height - 1,
                    a->message[0] ? a->message :
                    (a->mode == MODE_COMMAND ? "COMMAND: i insert, : commands, Ctrl+Shift+T files" : "EDIT: Esc command, Ctrl+S save"),
                    COLOR_DIM, a->width);
                cursorx = b ? gutter + (int)visual_column(&b->lines[b->row], b->col) - (int)b->scroll_col : 0;
                cursory = b ? (int)(b->row - b->scroll_row) : 0;
            }
        }
    }
    size.X = (SHORT)a->width;
    size.Y = (SHORT)a->height;
    region = a->viewport;
    WriteConsoleOutputW(a->output, a->screen, size, origin, &region);
    caret.X = (SHORT)(a->viewport.Left + (cursorx < 0 ? 0 : cursorx >= a->width ? a->width - 1 : cursorx));
    caret.Y = (SHORT)(a->viewport.Top + (cursory < 0 ? 0 : cursory >= a->height ? a->height - 1 : cursory));
    ci.dwSize = a->mode == MODE_COMMAND && a->prompt_mode == PROMPT_NONE ? 85 : 20;
    ci.bVisible = a->mode != MODE_PICKER;
    SetConsoleCursorInfo(a->output, &ci);
    SetConsoleCursorPosition(a->output, caret);
}

static void start_prompt(EditorApp *a, PromptMode kind) {
    a->prompt_mode = kind;
    a->prompt[0] = '\0';
    a->prompt_len = 0;
    a->pending_d = a->pending_g = 0;
    a->message[0] = '\0';
}

static void save_current(EditorApp *a, const char *path) {
    EdBuffer *b = ed_workspace_current(&a->ws);
    char error[256] = {0};
    if (!b) return;
    if (!b->path && (!path || !*path)) { start_prompt(a, PROMPT_SAVE); return; }
    if (ed_buffer_save(b, path, error, sizeof(error))) app_message(a, "File written successfully");
    else app_message(a, error);
}

static void open_file(EditorApp *a, const char *path) {
    char error[256] = {0};
    if (!path || !*path) { app_message(a, "Provide a filename"); return; }
    if (ed_workspace_open(&a->ws, path, error, sizeof(error))) {
        a->mode = MODE_EDIT;
        app_message(a, "File opened; now in EDIT mode");
    } else app_message(a, error);
}

static void open_picker(EditorApp *a) {
    if (a->mode != MODE_COMMAND) return;
    a->picker_index = a->ws.active;
    a->picker_top = 0;
    a->mode = MODE_PICKER;
    a->pending_g = a->pending_d = 0;
}

static void close_current_buffer(EditorApp *a, int force) {
    EdBuffer *b = ed_workspace_current(&a->ws);
    if (b && b->changed && !force) { app_message(a, "Unsaved changes; use :bd! to discard"); return; }
    ed_workspace_close(&a->ws, a->ws.active);
    if (a->ws.count == 0) ed_workspace_new(&a->ws);
    a->mode = MODE_COMMAND;
    app_message(a, "Buffer closed");
}

static char *skip_space(char *s) {
    while (*s && isspace((unsigned char)*s)) s++;
    return s;
}

static void command(EditorApp *a, char *command_text) {
    char *cmd = skip_space(command_text);
    size_t len = strlen(cmd);
    EdBuffer *b = ed_workspace_current(&a->ws);
    while (len && isspace((unsigned char)cmd[len - 1])) cmd[--len] = '\0';
    if (!strcmp(cmd, "w")) save_current(a, NULL);
    else if (!strncmp(cmd, "w ", 2)) save_current(a, skip_space(cmd + 2));
    else if (!strcmp(cmd, "q") || !strcmp(cmd, "wq") || !strcmp(cmd, "x")) {
        if (cmd[0] != 'q' && b && (b->changed || !b->path)) {
            char error[256] = {0};
            if (!b->path) { app_message(a, "Name the file before :wq/:x (:w filename)"); return; }
            if (!ed_buffer_save(b, NULL, error, sizeof(error))) { app_message(a, error); return; }
        }
        if (app_any_dirty(a)) app_message(a, "Unsaved buffers; save files or use :q! to quit");
        else a->running = 0;
    }
    else if (!strcmp(cmd, "q!")) a->running = 0;
    else if (!strncmp(cmd, "e ", 2)) open_file(a, skip_space(cmd + 2));
    else if (!strcmp(cmd, "new")) {
        if (ed_workspace_new(&a->ws)) { a->mode = MODE_EDIT; app_message(a, "New buffer created"); }
        else app_message(a, "Out of memory");
    }
    else if (!strcmp(cmd, "bn")) {
        if (a->ws.count) a->ws.active = (a->ws.active + 1) % a->ws.count;
        app_message(a, "Switched to next buffer");
    }
    else if (!strcmp(cmd, "bp")) {
        if (a->ws.count) a->ws.active = (a->ws.active + a->ws.count - 1) % a->ws.count;
        app_message(a, "Switched to previous buffer");
    }
    else if (!strcmp(cmd, "bd") || !strcmp(cmd, "bd!")) close_current_buffer(a, cmd[2] == '!');
    else if (!strcmp(cmd, "buffers") || !strcmp(cmd, "ls")) open_picker(a);
    else if (!strcmp(cmd, "set number")) { a->show_numbers = 1; app_message(a, "Line numbers enabled"); }
    else if (!strcmp(cmd, "set nonumber")) { a->show_numbers = 0; app_message(a, "Line numbers disabled"); }
    else if (!strcmp(cmd, "help")) app_message(a, "i=edit Esc=command :w=save :e FILE=open :buffers=select :q=quit :help=help");
    else if (!*cmd) app_message(a, "");
    else app_message(a, "Unknown command. Type :help");
}

static void append_prompt(EditorApp *a, unsigned codepoint) {
    char bytes[4];
    size_t count = ed_utf8_encode(codepoint, bytes);
    if (!count || codepoint < 32 || a->prompt_len + count >= sizeof(a->prompt)) return;
    memcpy(a->prompt + a->prompt_len, bytes, count);
    a->prompt_len += count;
    a->prompt[a->prompt_len] = '\0';
}

static void handle_prompt(EditorApp *a, WORD vk, unsigned codepoint) {
    if (vk == VK_ESCAPE) { a->prompt_mode = PROMPT_NONE; return; }
    if (vk == VK_BACK) {
        if (a->prompt_len) {
            a->prompt_len = ed_utf8_prev(a->prompt, a->prompt_len);
            a->prompt[a->prompt_len] = '\0';
        }
        return;
    }
    if (vk == VK_RETURN) {
        PromptMode kind = a->prompt_mode;
        a->prompt_mode = PROMPT_NONE;
        if (kind == PROMPT_EX) command(a, a->prompt);
        if (kind == PROMPT_OPEN) open_file(a, a->prompt);
        if (kind == PROMPT_SAVE) save_current(a, a->prompt);
        return;
    }
    append_prompt(a, codepoint);
}

static void cursor_keys(EdBuffer *b, WORD vk, int rows) {
    int i;
    switch (vk) {
    case VK_LEFT: ed_buffer_move_left(b); break;
    case VK_RIGHT: ed_buffer_move_right(b); break;
    case VK_UP: ed_buffer_move_up(b); break;
    case VK_DOWN: ed_buffer_move_down(b); break;
    case VK_HOME: ed_buffer_home(b); break;
    case VK_END: ed_buffer_end(b); break;
    case VK_PRIOR:
        for (i = 0; i < rows - 1; ++i) ed_buffer_move_up(b);
        break;
    case VK_NEXT:
        for (i = 0; i < rows - 1; ++i) ed_buffer_move_down(b);
        break;
    default: break;
    }
}

static void handle_picker(EditorApp *a, WORD vk, unsigned codepoint) {
    if (vk == VK_ESCAPE) { a->mode = MODE_COMMAND; return; }
    if (vk == VK_UP || codepoint == 'k') {
        if (a->picker_index) a->picker_index--;
    } else if (vk == VK_DOWN || codepoint == 'j') {
        if (a->picker_index + 1 < a->ws.count) a->picker_index++;
    } else if (vk == VK_HOME) a->picker_index = 0;
    else if (vk == VK_END) a->picker_index = a->ws.count - 1;
    else if (vk == VK_RETURN) {
        ed_workspace_switch(&a->ws, a->picker_index);
        a->mode = MODE_EDIT;
        app_message(a, "Selected buffer; now in EDIT mode");
    }
}

static void handle_edit(EditorApp *a, WORD vk, unsigned cp) {
    EdBuffer *b = ed_workspace_current(&a->ws);
    char bytes[4];
    size_t len;
    if (!b) return;
    if (vk == VK_ESCAPE) { a->mode = MODE_COMMAND; app_message(a, "COMMAND mode"); return; }
    if (vk == VK_LEFT || vk == VK_RIGHT || vk == VK_UP || vk == VK_DOWN ||
        vk == VK_HOME || vk == VK_END || vk == VK_PRIOR || vk == VK_NEXT) {
        cursor_keys(b, vk, a->height - 2);
        return;
    }
    if (vk == VK_RETURN) { if (!ed_buffer_split(b)) app_message(a, "Out of memory"); return; }
    if (vk == VK_BACK) { if (!ed_buffer_backspace(b)) app_message(a, "Out of memory"); return; }
    if (vk == VK_DELETE) { if (!ed_buffer_delete(b)) app_message(a, "Out of memory"); return; }
    if (vk == VK_TAB) { if (!ed_buffer_insert(b, "\t", 1)) app_message(a, "Out of memory"); return; }
    if (cp < 32 || cp == 127) return;
    len = ed_utf8_encode(cp, bytes);
    if (len && !ed_buffer_insert(b, bytes, len)) app_message(a, "Cannot insert text");
}

static void handle_command(EditorApp *a, WORD vk, unsigned cp) {
    EdBuffer *b = ed_workspace_current(&a->ws);
    if (!b) return;
    if (vk == VK_LEFT || vk == VK_RIGHT || vk == VK_UP || vk == VK_DOWN ||
        vk == VK_HOME || vk == VK_END || vk == VK_PRIOR || vk == VK_NEXT) {
        cursor_keys(b, vk, a->height - 2); a->pending_g = a->pending_d = 0; return;
    }
    if (cp == ':') { start_prompt(a, PROMPT_EX); return; }
    if (a->pending_d) {
        a->pending_d = 0;
        if (cp == 'd') {
            if (!ed_buffer_delete_line(b)) app_message(a, "Out of memory");
        }
        return;
    }
    if (a->pending_g) {
        a->pending_g = 0;
        if (cp == 'g') ed_buffer_go(b, 0);
        return;
    }
    switch (cp) {
    case 'i': a->mode = MODE_EDIT; break;
    case 'a': if (b->col < b->lines[b->row].length) ed_buffer_move_right(b); a->mode = MODE_EDIT; break;
    case 'I': ed_buffer_home(b); a->mode = MODE_EDIT; break;
    case 'A': ed_buffer_end(b); a->mode = MODE_EDIT; break;
    case 'h': ed_buffer_move_left(b); break;
    case 'j': ed_buffer_move_down(b); break;
    case 'k': ed_buffer_move_up(b); break;
    case 'l': ed_buffer_move_right(b); break;
    case '0': ed_buffer_home(b); break;
    case '$': ed_buffer_end(b); break;
    case 'G': ed_buffer_go(b, b->line_count - 1); break;
    case 'g': a->pending_g = 1; break;
    case 'd': a->pending_d = 1; break;
    case 'x': if (!ed_buffer_delete(b)) app_message(a, "Out of memory"); break;
    case 'o':
        if (ed_buffer_insert_line(b, b->row + 1)) a->mode = MODE_EDIT;
        else app_message(a, "Out of memory");
        break;
    case 'O':
        if (ed_buffer_insert_line(b, b->row)) a->mode = MODE_EDIT;
        else app_message(a, "Out of memory");
        break;
    default: break;
    }
}

static void handle_key(EditorApp *a, WORD vk, DWORD modifiers, unsigned cp) {
    int ctrl = (modifiers & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)) != 0;
    if (a->mode == MODE_PICKER) { handle_picker(a, vk, cp); return; }
    if (a->prompt_mode != PROMPT_NONE) { handle_prompt(a, vk, cp); return; }
    if (ctrl) {
        if (vk == 'T' && a->mode == MODE_COMMAND) { open_picker(a); return; }
        if (vk == 'S') { save_current(a, NULL); return; }
        if (vk == 'O') { start_prompt(a, PROMPT_OPEN); return; }
        if (vk == 'N') {
            if (ed_workspace_new(&a->ws)) { a->mode = MODE_EDIT; app_message(a, "New buffer created"); }
            else app_message(a, "Out of memory");
            return;
        }
        if (vk == 'Q') {
            if (app_any_dirty(a)) app_message(a, "Unsaved buffers; use :q! to discard or save first");
            else a->running = 0;
            return;
        }
        return;
    }
    if (a->mode == MODE_EDIT) handle_edit(a, vk, cp);
    else handle_command(a, vk, cp);
}

static void key_event(EditorApp *a, const KEY_EVENT_RECORD *event) {
    unsigned cp = event->uChar.UnicodeChar;
    unsigned saved;
    unsigned repetitions, k;
    if (!event->bKeyDown) return;
    if (cp >= 0xD800 && cp <= 0xDBFF) {
        a->pending_high_surrogate = cp;
        return;
    }
    saved = a->pending_high_surrogate;
    a->pending_high_surrogate = 0;
    if (cp >= 0xDC00 && cp <= 0xDFFF && saved)
        cp = 0x10000 + ((saved - 0xD800) << 10) + (cp - 0xDC00);
    else if (cp >= 0xDC00 && cp <= 0xDFFF) return;
    repetitions = event->wRepeatCount ? event->wRepeatCount : 1;
    if (repetitions > 256) repetitions = 256;
    for (k = 0; k < repetitions && a->running; ++k)
        handle_key(a, event->wVirtualKeyCode, event->dwControlKeyState, cp);
}

/* Legacy conio input fallback for consoles which cannot provide input records.
   Ctrl+T remains the fallback when Ctrl+Shift+T is intercepted by the host. */
static void conio_fallback(EditorApp *a) {
    int ch = _getwch(), vk = 0;
    if (ch == 0 || ch == 0xE0) {
        switch (_getwch()) {
        case 72: vk = VK_UP; break;
        case 80: vk = VK_DOWN; break;
        case 75: vk = VK_LEFT; break;
        case 77: vk = VK_RIGHT; break;
        case 71: vk = VK_HOME; break;
        case 79: vk = VK_END; break;
        case 83: vk = VK_DELETE; break;
        case 73: vk = VK_PRIOR; break;
        case 81: vk = VK_NEXT; break;
        default: return;
        }
        handle_key(a, (WORD)vk, 0, 0);
    } else {
        DWORD ctrl = 0;
        if (ch == 20) { vk = 'T'; ctrl = LEFT_CTRL_PRESSED; }
        else if (ch == 19) { vk = 'S'; ctrl = LEFT_CTRL_PRESSED; }
        else if (ch == 15) { vk = 'O'; ctrl = LEFT_CTRL_PRESSED; }
        else if (ch == 14) { vk = 'N'; ctrl = LEFT_CTRL_PRESSED; }
        else if (ch == 17) { vk = 'Q'; ctrl = LEFT_CTRL_PRESSED; }
        else if (ch == 27) vk = VK_ESCAPE;
        else if (ch == 13) vk = VK_RETURN;
        else if (ch == 8) vk = VK_BACK;
        else if (ch == 9) vk = VK_TAB;
        handle_key(a, (WORD)vk, ctrl, (unsigned)ch);
    }
}

static int utf8_arg(const wchar_t *wide, char **out) {
    int bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide, -1, NULL, 0, NULL, NULL);
    char *utf8;
    if (!bytes) return 0;
    utf8 = (char *)malloc((size_t)bytes);
    if (!utf8) return 0;
    if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide, -1, utf8, bytes, NULL, NULL)) {
        free(utf8); return 0;
    }
    *out = utf8;
    return 1;
}

int wmain(int argc, wchar_t **argv) {
    EditorApp a;
    int i, result = 0;
    INPUT_RECORD record;
    DWORD received;
    memset(&a, 0, sizeof(a));
    ed_workspace_init(&a.ws);
    a.mode = MODE_COMMAND;
    a.running = 1;
    a.show_numbers = 1;
    a.input = GetStdHandle(STD_INPUT_HANDLE);
    a.output = GetStdHandle(STD_OUTPUT_HANDLE);
    if (a.input == INVALID_HANDLE_VALUE || a.output == INVALID_HANDLE_VALUE ||
        !GetConsoleMode(a.input, &a.old_input_mode) ||
        !GetConsoleMode(a.output, &a.old_output_mode)) {
        fputs("LikeEdit needs an interactive Windows console (cmd.exe, PowerShell, Windows Terminal).\n", stderr);
        return 1;
    }
    a.has_old_cursor = GetConsoleCursorInfo(a.output, &a.old_cursor) != 0;
    a.old_output_codepage = GetConsoleOutputCP();
    SetConsoleMode(a.input, (a.old_input_mode | ENABLE_WINDOW_INPUT | ENABLE_EXTENDED_FLAGS) &
                   ~(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT | ENABLE_QUICK_EDIT_MODE | ENABLE_VIRTUAL_TERMINAL_INPUT));
    SetConsoleOutputCP(CP_UTF8);
    for (i = 1; i < argc; ++i) {
        char *path = NULL, error[256] = {0};
        if (!utf8_arg(argv[i], &path)) { app_message(&a, "Invalid argument encoding"); continue; }
        if (!ed_workspace_open(&a.ws, path, error, sizeof(error))) app_message(&a, error);
        free(path);
    }
    if (!a.ws.count && !ed_workspace_new(&a.ws)) {
        fputs("Unable to allocate text buffer.\n", stderr);
        result = 1;
        goto cleanup;
    }
    render(&a);
    while (a.running) {
        if (ReadConsoleInputW(a.input, &record, 1, &received) && received) {
            if (record.EventType == KEY_EVENT) key_event(&a, &record.Event.KeyEvent);
            if (record.EventType == KEY_EVENT || record.EventType == WINDOW_BUFFER_SIZE_EVENT) render(&a);
        } else {
            /* Actual conio.h keyboard fallback: non-canonical character input. */
            conio_fallback(&a);
            render(&a);
        }
    }
cleanup:
    SetConsoleMode(a.input, a.old_input_mode);
    SetConsoleMode(a.output, a.old_output_mode);
    if (a.old_output_codepage) SetConsoleOutputCP(a.old_output_codepage);
    if (a.has_old_cursor) SetConsoleCursorInfo(a.output, &a.old_cursor);
    free(a.screen);
    ed_workspace_dispose(&a.ws);
    /* Move prompt below full-screen display so the shell remains usable. */
    { CONSOLE_SCREEN_BUFFER_INFO csbi;
      if (GetConsoleScreenBufferInfo(a.output, &csbi)) {
          COORD end = { csbi.srWindow.Left, csbi.srWindow.Bottom };
          SetConsoleCursorPosition(a.output, end);
          _cputws(L"\r\n");
      }
    }
    return result;
}
