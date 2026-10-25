#include "editor_core.h"
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#include <wchar.h>
#endif

#define ED_MAX_FILE_BYTES (128u * 1024u * 1024u)

static void failure(char *err, size_t cap, const char *message) {
    if (err && cap) snprintf(err, cap, "%s", message);
}

static char *copy_text(const char *source) {
    size_t n = strlen(source);
    char *result = (char *)malloc(n + 1);
    if (result) memcpy(result, source, n + 1);
    return result;
}

static int reserve_line(EdLine *line, size_t needed) {
    size_t capacity;
    char *new_mem;
    if (needed <= line->capacity) return 1;
    capacity = line->capacity ? line->capacity : 32;
    while (capacity < needed) {
        if (capacity > SIZE_MAX / 2) return 0;
        capacity *= 2;
    }
    new_mem = (char *)realloc(line->text, capacity);
    if (!new_mem) return 0;
    line->text = new_mem;
    line->capacity = capacity;
    return 1;
}

static int init_line(EdLine *line, const char *text, size_t length) {
    memset(line, 0, sizeof(*line));
    if (!reserve_line(line, length + 1)) return 0;
    if (length) memcpy(line->text, text, length);
    line->text[length] = '\0';
    line->length = length;
    return 1;
}

static void dispose_line(EdLine *line) {
    free(line->text);
    memset(line, 0, sizeof(*line));
}

static int reserve_buffer_lines(EdBuffer *buffer, size_t needed) {
    size_t new_capacity;
    EdLine *new_lines;
    if (needed <= buffer->line_capacity) return 1;
    new_capacity = buffer->line_capacity ? buffer->line_capacity : 8;
    while (new_capacity < needed) {
        if (new_capacity > SIZE_MAX / 2) return 0;
        new_capacity *= 2;
    }
    new_lines = (EdLine *)realloc(buffer->lines, new_capacity * sizeof(EdLine));
    if (!new_lines) return 0;
    buffer->lines = new_lines;
    buffer->line_capacity = new_capacity;
    return 1;
}

static void dispose_buffer(EdBuffer *buffer) {
    size_t i;
    if (!buffer) return;
    for (i = 0; i < buffer->line_count; ++i) dispose_line(&buffer->lines[i]);
    free(buffer->lines);
    free(buffer->path);
    free(buffer);
}

static EdBuffer *new_buffer(void) {
    EdBuffer *buffer = (EdBuffer *)calloc(1, sizeof(*buffer));
    if (!buffer) return NULL;
    if (!reserve_buffer_lines(buffer, 1) || !init_line(&buffer->lines[0], "", 0)) {
        dispose_buffer(buffer);
        return NULL;
    }
    buffer->line_count = 1;
    buffer->crlf = 1; /* Windows-native default; detected when loading */
    return buffer;
}

void ed_workspace_init(EdWorkspace *ws) {
    memset(ws, 0, sizeof(*ws));
    ws->next_untitled = 1;
}

void ed_workspace_dispose(EdWorkspace *ws) {
    size_t i;
    for (i = 0; i < ws->count; ++i) dispose_buffer(ws->items[i]);
    free(ws->items);
    ed_workspace_init(ws);
}

EdBuffer *ed_workspace_current(EdWorkspace *ws) {
    if (ws->count == 0 || ws->active >= ws->count) return NULL;
    return ws->items[ws->active];
}

static int append_buffer(EdWorkspace *ws, EdBuffer *buffer) {
    EdBuffer **next;
    size_t cap;
    if (ws->count == ws->capacity) {
        cap = ws->capacity ? ws->capacity * 2 : 8;
        next = (EdBuffer **)realloc(ws->items, cap * sizeof(*next));
        if (!next) return 0;
        ws->items = next;
        ws->capacity = cap;
    }
    ws->items[ws->count] = buffer;
    ws->active = ws->count++;
    return 1;
}

int ed_workspace_new(EdWorkspace *ws) {
    EdBuffer *buffer = new_buffer();
    if (!buffer) return 0;
    snprintf(buffer->untitled_name, sizeof(buffer->untitled_name), "[No Name %u]", ws->next_untitled);
    if (!append_buffer(ws, buffer)) { dispose_buffer(buffer); return 0; }
    ws->next_untitled++;
    return 1;
}

#ifdef _WIN32
static FILE *open_utf8(const char *utf8, const wchar_t *mode) {
    int chars = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8, -1, NULL, 0);
    wchar_t *wide;
    FILE *result;
    if (!chars) return NULL;
    wide = (wchar_t *)malloc((size_t)chars * sizeof(wchar_t));
    if (!wide) return NULL;
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8, -1, wide, chars)) {
        free(wide);
        return NULL;
    }
    result = _wfopen(wide, mode);
    free(wide);
    return result;
}
#define OPEN_READ(p) open_utf8((p), L"rb")
#define OPEN_WRITE(p) open_utf8((p), L"wb")
#else
#define OPEN_READ(p) fopen((p), "rb")
#define OPEN_WRITE(p) fopen((p), "wb")
#endif

size_t ed_utf8_next(const char *s, size_t n, size_t pos) {
    unsigned char c;
    size_t count, k;
    if (pos >= n) return n;
    c = (unsigned char)s[pos];
    count = c < 0x80 ? 1 : (c >= 0xC2 && c <= 0xDF ? 2 : (c >= 0xE0 && c <= 0xEF ? 3 : (c >= 0xF0 && c <= 0xF4 ? 4 : 1)));
    if (count > n - pos) return pos + 1;
    for (k = 1; k < count; ++k) if (((unsigned char)s[pos + k] & 0xC0) != 0x80) return pos + 1;
    return pos + count;
}

size_t ed_utf8_prev(const char *s, size_t pos) {
    size_t i, p;
    if (!pos) return 0;
    p = pos - 1;
    for (i = 0; i < 3 && p && ((unsigned char)s[p] & 0xC0) == 0x80; ++i) p--;
    return p;
}

size_t ed_utf8_count(const char *s, size_t n) {
    size_t pos = 0, count = 0;
    while (pos < n) { pos = ed_utf8_next(s, n, pos); count++; }
    return count;
}

size_t ed_utf8_offset(const char *s, size_t n, size_t column) {
    size_t pos = 0;
    while (column-- && pos < n) pos = ed_utf8_next(s, n, pos);
    return pos;
}

size_t ed_utf8_encode(unsigned cp, char out[4]) {
    if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return 0;
    if (cp < 0x80) { out[0] = (char)cp; return 1; }
    if (cp < 0x800) { out[0] = (char)(0xC0 | (cp >> 6)); out[1] = (char)(0x80 | (cp & 0x3F)); return 2; }
    if (cp < 0x10000) { out[0] = (char)(0xE0 | (cp >> 12)); out[1] = (char)(0x80 | ((cp >> 6) & 0x3F)); out[2] = (char)(0x80 | (cp & 0x3F)); return 3; }
    out[0] = (char)(0xF0 | (cp >> 18)); out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    out[2] = (char)(0x80 | ((cp >> 6) & 0x3F)); out[3] = (char)(0x80 | (cp & 0x3F)); return 4;
}

int ed_utf8_valid(const unsigned char *s, size_t n) {
    size_t i = 0;
    while (i < n) {
        unsigned cp;
        size_t k, width;
        if (s[i] < 0x80) { i++; continue; }
        if (s[i] >= 0xC2 && s[i] <= 0xDF) { width = 2; cp = s[i] & 0x1F; }
        else if (s[i] >= 0xE0 && s[i] <= 0xEF) { width = 3; cp = s[i] & 0x0F; }
        else if (s[i] >= 0xF0 && s[i] <= 0xF4) { width = 4; cp = s[i] & 0x07; }
        else return 0;
        if (width > n - i) return 0;
        for (k = 1; k < width; ++k) {
            if ((s[i+k] & 0xC0) != 0x80) return 0;
            cp = (cp << 6) | (s[i+k] & 0x3F);
        }
        if ((width == 2 && cp < 0x80) || (width == 3 && cp < 0x800) ||
            (width == 4 && cp < 0x10000) || cp > 0x10FFFF ||
            (cp >= 0xD800 && cp <= 0xDFFF)) return 0;
        i += width;
    }
    return 1;
}

int ed_buffer_load(EdBuffer *buffer, const char *path, char *err, size_t errcap) {
    FILE *file;
    long length;
    char *contents = NULL, *new_path;
    size_t i, start, n, crlf = 0, lf = 0;
    int had_bom;
    EdBuffer tmp = {0};
    file = OPEN_READ(path);
    if (!file) {
        if (errno != ENOENT) { failure(err, errcap, "Cannot open file for reading"); return 0; }
        new_path = copy_text(path);
        if (!new_path) { failure(err, errcap, "Out of memory"); return 0; }
        free(buffer->path);
        buffer->path = new_path;
        return 1; /* A new path is a legitimate empty buffer. */
    }
    if (fseek(file, 0, SEEK_END) != 0 || (length = ftell(file)) < 0 ||
        (unsigned long)length > ED_MAX_FILE_BYTES || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file); failure(err, errcap, "File is too large (128 MB maximum) or unreadable"); return 0;
    }
    contents = (char *)malloc((size_t)length + 1);
    if (!contents) { fclose(file); failure(err, errcap, "Out of memory"); return 0; }
    n = fread(contents, 1, (size_t)length, file);
    if (ferror(file) || n != (size_t)length) { fclose(file); free(contents); failure(err, errcap, "Failed reading file"); return 0; }
    fclose(file);
    if (memchr(contents, '\0', n)) { free(contents); failure(err, errcap, "Binary files (NUL bytes) are not supported"); return 0; }
    start = n >= 3 && (unsigned char)contents[0] == 0xEF &&
        (unsigned char)contents[1] == 0xBB && (unsigned char)contents[2] == 0xBF ? 3 : 0;
    had_bom = start == 3;
    if (!ed_utf8_valid((const unsigned char *)contents + start, n - start)) {
        free(contents); failure(err, errcap, "File is not valid UTF-8 (UTF-16 not supported)"); return 0;
    }
    tmp.crlf = 1;
    for (i = start; i <= n; ++i) {
        if (i == n || contents[i] == '\n') {
            size_t end = i;
            if (i < n) {
                if (end > start && contents[end - 1] == '\r') { end--; crlf++; }
                else lf++;
            }
            if (!reserve_buffer_lines(&tmp, tmp.line_count + 1) ||
                !init_line(&tmp.lines[tmp.line_count], contents + start, end - start)) {
                size_t j;
                for (j = 0; j < tmp.line_count; ++j) dispose_line(&tmp.lines[j]);
                free(tmp.lines); free(contents); failure(err, errcap, "Out of memory"); return 0;
            }
            tmp.line_count++;
            start = i + 1;
        }
    }
    free(contents);
    new_path = copy_text(path);
    if (!new_path) {
        for (i = 0; i < tmp.line_count; ++i) dispose_line(&tmp.lines[i]);
        free(tmp.lines); failure(err, errcap, "Out of memory"); return 0;
    }
    for (i = 0; i < buffer->line_count; ++i) dispose_line(&buffer->lines[i]);
    free(buffer->lines);
    free(buffer->path);
    buffer->lines = tmp.lines;
    buffer->line_count = tmp.line_count;
    buffer->line_capacity = tmp.line_capacity;
    buffer->path = new_path;
    buffer->row = buffer->col = buffer->scroll_row = buffer->scroll_col = buffer->desired_column = 0;
    buffer->crlf = crlf >= lf;
    buffer->has_utf8_bom = had_bom;
    buffer->changed = 0;
    return 1;
}

int ed_workspace_open(EdWorkspace *ws, const char *path, char *err, size_t errcap) {
    size_t i;
    EdBuffer *buffer;
    if (!path || !*path) { failure(err, errcap, "A filename is required"); return 0; }
    for (i = 0; i < ws->count; ++i) {
        if (ws->items[i]->path && strcmp(ws->items[i]->path, path) == 0) {
            ws->active = i;
            return 1;
        }
    }
    buffer = new_buffer();
    if (!buffer) { failure(err, errcap, "Out of memory"); return 0; }
    if (!ed_buffer_load(buffer, path, err, errcap) || !append_buffer(ws, buffer)) {
        if (err && !*err) failure(err, errcap, "Out of memory");
        dispose_buffer(buffer);
        return 0;
    }
    return 1;
}

int ed_workspace_close(EdWorkspace *ws, size_t index) {
    size_t i;
    if (index >= ws->count) return 0;
    dispose_buffer(ws->items[index]);
    for (i = index + 1; i < ws->count; ++i) ws->items[i - 1] = ws->items[i];
    ws->count--;
    if (!ws->count) ws->active = 0;
    else if (ws->active >= ws->count) ws->active = ws->count - 1;
    else if (index < ws->active) ws->active--;
    return 1;
}

int ed_workspace_switch(EdWorkspace *ws, size_t index) {
    if (index >= ws->count) return 0;
    ws->active = index;
    return 1;
}

const char *ed_buffer_name(const EdBuffer *buffer) {
    const char *slash, *backslash, *name;
    if (!buffer->path) return buffer->untitled_name[0] ? buffer->untitled_name : "[No Name]";
    slash = strrchr(buffer->path, '/');
    backslash = strrchr(buffer->path, '\\');
    name = slash && (!backslash || slash > backslash) ? slash + 1 :
        backslash ? backslash + 1 : buffer->path;
    return *name ? name : buffer->path;
}

int ed_buffer_save(EdBuffer *buffer, const char *new_path, char *err, size_t errcap) {
    FILE *file;
    size_t i;
    const char *path = new_path && *new_path ? new_path : buffer->path;
    char *copy = NULL;
    if (!path) { failure(err, errcap, "No file name; use :w <filename>"); return 0; }
    if (new_path && *new_path) {
        copy = copy_text(new_path);
        if (!copy) { failure(err, errcap, "Out of memory"); return 0; }
    }
    file = OPEN_WRITE(path);
    if (!file) { free(copy); failure(err, errcap, "Cannot open file for writing"); return 0; }
    if (buffer->has_utf8_bom && fwrite("\xEF\xBB\xBF", 1, 3, file) != 3) {
        fclose(file); free(copy); failure(err, errcap, "Cannot write UTF-8 BOM"); return 0;
    }
    for (i = 0; i < buffer->line_count; ++i) {
        EdLine *line = &buffer->lines[i];
        if (fwrite(line->text, 1, line->length, file) != line->length ||
            (i + 1 < buffer->line_count && fputs(buffer->crlf ? "\r\n" : "\n", file) < 0)) {
            fclose(file); free(copy); failure(err, errcap, "Write failed; file may be incomplete"); return 0;
        }
    }
    if (fclose(file) != 0) { free(copy); failure(err, errcap, "Failed to close saved file"); return 0; }
    if (copy) { free(buffer->path); buffer->path = copy; }
    buffer->changed = 0;
    return 1;
}

static void update_desired(EdBuffer *buffer) {
    EdLine *line = &buffer->lines[buffer->row];
    buffer->desired_column = ed_utf8_count(line->text, buffer->col);
}

int ed_buffer_insert(EdBuffer *buffer, const char *text, size_t length) {
    EdLine *line;
    if (!length) return 1;
    if (!ed_utf8_valid((const unsigned char *)text, length) || memchr(text, '\n', length) || memchr(text, '\r', length) || memchr(text, '\0', length)) return 0;
    line = &buffer->lines[buffer->row];
    if (length > SIZE_MAX - line->length - 1 || !reserve_line(line, line->length + length + 1)) return 0;
    memmove(line->text + buffer->col + length, line->text + buffer->col, line->length - buffer->col + 1);
    memcpy(line->text + buffer->col, text, length);
    line->length += length;
    buffer->col += length;
    buffer->changed = 1;
    update_desired(buffer);
    return 1;
}

int ed_buffer_split(EdBuffer *buffer) {
    size_t row = buffer->row, col = buffer->col;
    EdLine *line = &buffer->lines[row], second = {0};
    if (!init_line(&second, line->text + col, line->length - col)) return 0;
    if (!reserve_buffer_lines(buffer, buffer->line_count + 1)) { dispose_line(&second); return 0; }
    memmove(buffer->lines + row + 2, buffer->lines + row + 1,
            (buffer->line_count - row - 1) * sizeof(EdLine));
    buffer->lines[row + 1] = second;
    buffer->line_count++;
    line = &buffer->lines[row];
    line->length = col;
    line->text[col] = '\0';
    buffer->row++;
    buffer->col = 0;
    buffer->desired_column = 0;
    buffer->changed = 1;
    return 1;
}

int ed_buffer_backspace(EdBuffer *buffer) {
    EdLine *line = &buffer->lines[buffer->row];
    if (buffer->col) {
        size_t prev = ed_utf8_prev(line->text, buffer->col);
        memmove(line->text + prev, line->text + buffer->col, line->length - buffer->col + 1);
        line->length -= buffer->col - prev;
        buffer->col = prev;
    } else if (buffer->row) {
        EdLine *previous = &buffer->lines[buffer->row - 1];
        size_t new_length = previous->length + line->length;
        size_t old = previous->length;
        if (new_length < old || !reserve_line(previous, new_length + 1)) return 0;
        memcpy(previous->text + old, line->text, line->length + 1);
        previous->length = new_length;
        dispose_line(line);
        memmove(buffer->lines + buffer->row, buffer->lines + buffer->row + 1,
                (buffer->line_count - buffer->row - 1) * sizeof(EdLine));
        buffer->line_count--;
        buffer->row--;
        buffer->col = old;
    } else return 1;
    buffer->changed = 1;
    update_desired(buffer);
    return 1;
}

int ed_buffer_delete(EdBuffer *buffer) {
    EdLine *line = &buffer->lines[buffer->row];
    if (buffer->col < line->length) {
        size_t next = ed_utf8_next(line->text, line->length, buffer->col);
        memmove(line->text + buffer->col, line->text + next, line->length - next + 1);
        line->length -= next - buffer->col;
    } else if (buffer->row + 1 < buffer->line_count) {
        EdLine *following = &buffer->lines[buffer->row + 1];
        size_t old = line->length;
        if (following->length > SIZE_MAX - old - 1 ||
            !reserve_line(line, old + following->length + 1)) return 0;
        memcpy(line->text + old, following->text, following->length + 1);
        line->length += following->length;
        dispose_line(following);
        memmove(buffer->lines + buffer->row + 1, buffer->lines + buffer->row + 2,
                (buffer->line_count - buffer->row - 2) * sizeof(EdLine));
        buffer->line_count--;
    } else return 1;
    buffer->changed = 1;
    update_desired(buffer);
    return 1;
}

int ed_buffer_insert_line(EdBuffer *buffer, size_t at) {
    EdLine empty = {0};
    if (at > buffer->line_count || !init_line(&empty, "", 0)) return 0;
    if (!reserve_buffer_lines(buffer, buffer->line_count + 1)) { dispose_line(&empty); return 0; }
    memmove(buffer->lines + at + 1, buffer->lines + at,
            (buffer->line_count - at) * sizeof(EdLine));
    buffer->lines[at] = empty;
    buffer->line_count++;
    buffer->row = at;
    buffer->col = buffer->desired_column = 0;
    buffer->changed = 1;
    return 1;
}

int ed_buffer_delete_line(EdBuffer *buffer) {
    size_t row = buffer->row;
    if (buffer->line_count == 1) {
        if (!buffer->lines[0].length) return 1;
        buffer->lines[0].length = 0;
        buffer->lines[0].text[0] = '\0';
    } else {
        dispose_line(&buffer->lines[row]);
        memmove(buffer->lines + row, buffer->lines + row + 1,
                (buffer->line_count - row - 1) * sizeof(EdLine));
        buffer->line_count--;
        if (buffer->row >= buffer->line_count) buffer->row = buffer->line_count - 1;
    }
    buffer->col = 0;
    buffer->changed = 1;
    update_desired(buffer);
    return 1;
}

void ed_buffer_move_left(EdBuffer *buffer) {
    if (buffer->col) buffer->col = ed_utf8_prev(buffer->lines[buffer->row].text, buffer->col);
    else if (buffer->row) { buffer->row--; buffer->col = buffer->lines[buffer->row].length; }
    update_desired(buffer);
}
void ed_buffer_move_right(EdBuffer *buffer) {
    EdLine *line = &buffer->lines[buffer->row];
    if (buffer->col < line->length) buffer->col = ed_utf8_next(line->text, line->length, buffer->col);
    else if (buffer->row + 1 < buffer->line_count) { buffer->row++; buffer->col = 0; }
    update_desired(buffer);
}
void ed_buffer_move_up(EdBuffer *buffer) {
    if (buffer->row) buffer->row--;
    buffer->col = ed_utf8_offset(buffer->lines[buffer->row].text,
                                 buffer->lines[buffer->row].length, buffer->desired_column);
}
void ed_buffer_move_down(EdBuffer *buffer) {
    if (buffer->row + 1 < buffer->line_count) buffer->row++;
    buffer->col = ed_utf8_offset(buffer->lines[buffer->row].text,
                                 buffer->lines[buffer->row].length, buffer->desired_column);
}
void ed_buffer_home(EdBuffer *buffer) { buffer->col = buffer->desired_column = 0; }
void ed_buffer_end(EdBuffer *buffer) {
    buffer->col = buffer->lines[buffer->row].length;
    update_desired(buffer);
}
void ed_buffer_go(EdBuffer *buffer, size_t row) {
    buffer->row = row >= buffer->line_count ? buffer->line_count - 1 : row;
    buffer->col = 0;
    update_desired(buffer);
}
