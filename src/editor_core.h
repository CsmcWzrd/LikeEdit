#ifndef LIKEEDIT_EDITOR_CORE_H
#define LIKEEDIT_EDITOR_CORE_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char *text;
    size_t length;
    size_t capacity;
} EdLine;

typedef struct {
    char *path;                 /* UTF-8 path, or NULL for unnamed buffers */
    char untitled_name[32];
    EdLine *lines;
    size_t line_count;
    size_t line_capacity;
    size_t row;
    size_t col;                 /* UTF-8 byte offset at a codepoint boundary */
    size_t desired_column;      /* Unicode codepoint column for vertical movement */
    size_t scroll_row;
    size_t scroll_col;          /* visual screen-cell column */
    int changed;
    int crlf;
    int has_utf8_bom;
} EdBuffer;

typedef struct {
    EdBuffer **items;
    size_t count;
    size_t capacity;
    size_t active;
    unsigned next_untitled;
} EdWorkspace;

void ed_workspace_init(EdWorkspace *ws);
void ed_workspace_dispose(EdWorkspace *ws);
EdBuffer *ed_workspace_current(EdWorkspace *ws);
int ed_workspace_new(EdWorkspace *ws);
int ed_workspace_open(EdWorkspace *ws, const char *utf8_path, char *err, size_t errcap);
int ed_workspace_close(EdWorkspace *ws, size_t index);
int ed_workspace_switch(EdWorkspace *ws, size_t index);
int ed_buffer_load(EdBuffer *buffer, const char *utf8_path, char *err, size_t errcap);
int ed_buffer_save(EdBuffer *buffer, const char *utf8_path_or_null, char *err, size_t errcap);
const char *ed_buffer_name(const EdBuffer *buffer);

int ed_buffer_insert(EdBuffer *buffer, const char *utf8, size_t byte_count);
int ed_buffer_split(EdBuffer *buffer);
int ed_buffer_backspace(EdBuffer *buffer);
int ed_buffer_delete(EdBuffer *buffer);
int ed_buffer_delete_line(EdBuffer *buffer);
int ed_buffer_insert_line(EdBuffer *buffer, size_t at);
void ed_buffer_move_left(EdBuffer *buffer);
void ed_buffer_move_right(EdBuffer *buffer);
void ed_buffer_move_up(EdBuffer *buffer);
void ed_buffer_move_down(EdBuffer *buffer);
void ed_buffer_home(EdBuffer *buffer);
void ed_buffer_end(EdBuffer *buffer);
void ed_buffer_go(EdBuffer *buffer, size_t row);

size_t ed_utf8_next(const char *text, size_t length, size_t offset);
size_t ed_utf8_prev(const char *text, size_t offset);
size_t ed_utf8_count(const char *text, size_t byte_count);
size_t ed_utf8_offset(const char *text, size_t length, size_t codepoint_column);
size_t ed_utf8_encode(unsigned codepoint, char out[4]);
int ed_utf8_valid(const unsigned char *bytes, size_t length);

#ifdef __cplusplus
}
#endif
#endif
