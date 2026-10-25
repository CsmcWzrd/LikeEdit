#include "editor_core.h"
#include "syntax.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int passes = 0;
#define CHECK(expr) do { if (!(expr)) { fprintf(stderr, "FAILED %s at %s:%d\n", #expr, __FILE__, __LINE__); return 0; } passes++; } while (0)

static int test_text_operations(void) {
    EdWorkspace ws;
    EdBuffer *b;
    ed_workspace_init(&ws);
    CHECK(ed_workspace_new(&ws));
    b = ed_workspace_current(&ws);
    CHECK(b && b->line_count == 1 && b->lines[0].length == 0);
    CHECK(ed_buffer_insert(b, "Hello", 5));
    CHECK(b->col == 5 && !strcmp(b->lines[0].text, "Hello"));
    ed_buffer_move_left(b);
    CHECK(b->col == 4);
    CHECK(ed_buffer_insert(b, "!", 1));
    CHECK(!strcmp(b->lines[0].text, "Hell!o"));
    CHECK(ed_buffer_split(b));
    CHECK(b->line_count == 2 && !strcmp(b->lines[0].text, "Hell!") && !strcmp(b->lines[1].text, "o"));
    CHECK(b->row == 1 && b->col == 0);
    CHECK(ed_buffer_backspace(b));
    CHECK(b->line_count == 1 && !strcmp(b->lines[0].text, "Hell!o"));
    ed_buffer_go(b, 0);
    CHECK(ed_buffer_delete(b));
    CHECK(!strcmp(b->lines[0].text, "ell!o"));
    CHECK(ed_buffer_insert_line(b, 1));
    CHECK(b->row == 1 && b->line_count == 2);
    CHECK(ed_buffer_delete_line(b));
    CHECK(b->line_count == 1 && !strcmp(b->lines[0].text, "ell!o"));
    CHECK(ed_buffer_delete_line(b));
    CHECK(b->line_count == 1 && !strcmp(b->lines[0].text, ""));
    ed_workspace_dispose(&ws);
    return 1;
}

static int test_unicode(void) {
    const char *text = "a\xE2\x82\xAC\xE4\xB8\xAD"; /* a, EUR, CJK */
    char encoded[4];
    EdWorkspace ws;
    EdBuffer *b;
    ed_workspace_init(&ws);
    CHECK(ed_utf8_valid((const unsigned char *)text, strlen(text)));
    CHECK(!ed_utf8_valid((const unsigned char *)"\xC0\x80", 2));
    CHECK(!ed_utf8_valid((const unsigned char *)"\xED\xA0\x80", 3));
    CHECK(!ed_utf8_valid((const unsigned char *)"\xF4\x90\x80\x80", 4));
    CHECK(ed_utf8_count(text, strlen(text)) == 3);
    CHECK(ed_utf8_offset(text, strlen(text), 2) == 4);
    CHECK(ed_utf8_prev(text, 7) == 4);
    CHECK(ed_utf8_next(text, strlen(text), 1) == 4);
    CHECK(ed_utf8_encode(0x1F680, encoded) == 4);
    CHECK(ed_utf8_valid((const unsigned char *)encoded, 4));
    CHECK(ed_workspace_new(&ws));
    b = ed_workspace_current(&ws);
    CHECK(ed_buffer_insert(b, text, strlen(text)));
    CHECK(ed_utf8_count(b->lines[0].text, b->col) == 3);
    ed_buffer_move_left(b);
    CHECK(b->col == 4);
    CHECK(ed_buffer_backspace(b));
    CHECK(!strcmp(b->lines[0].text, "a\xE4\xB8\xAD"));
    ed_workspace_dispose(&ws);
    return 1;
}

static int test_workspace(void) {
    EdWorkspace ws;
    ed_workspace_init(&ws);
    CHECK(ed_workspace_new(&ws));
    CHECK(ed_workspace_new(&ws));
    CHECK(ed_workspace_new(&ws));
    CHECK(ws.count == 3 && ws.active == 2);
    CHECK(ed_workspace_switch(&ws, 0));
    CHECK(ed_workspace_close(&ws, 1));
    CHECK(ws.count == 2 && ws.active == 0);
    CHECK(ed_workspace_switch(&ws, 1));
    CHECK(ed_workspace_close(&ws, 0));
    CHECK(ws.count == 1 && ws.active == 0);
    CHECK(!ed_workspace_switch(&ws, 2));
    ed_workspace_dispose(&ws);
    return 1;
}

static int read_bytes(const char *path, unsigned char *dst, size_t *count) {
    FILE *f = fopen(path, "rb");
    long n;
    if (!f) return 0;
    if (fseek(f, 0, SEEK_END) || (n = ftell(f)) < 0 || n > 10000 || fseek(f, 0, SEEK_SET)) { fclose(f); return 0; }
    *count = fread(dst, 1, (size_t)n, f);
    fclose(f);
    return *count == (size_t)n;
}

static int test_io(void) {
    const char *source = "likeedit_test_source.tmp";
    const char *output = "likeedit_test_output.tmp";
    const unsigned char payload[] = { 0xEF,0xBB,0xBF,'a','\r','\n','b','\r','\n' };
    unsigned char data[10000];
    size_t count = 0;
    FILE *f = fopen(source, "wb");
    EdWorkspace ws;
    EdBuffer *b;
    char err[256] = {0};
    if (!f) return 0;
    CHECK(fwrite(payload, 1, sizeof(payload), f) == sizeof(payload));
    CHECK(fclose(f) == 0);
    ed_workspace_init(&ws);
    CHECK(ed_workspace_open(&ws, source, err, sizeof(err)));
    b = ed_workspace_current(&ws);
    CHECK(b->line_count == 3 && b->crlf && b->has_utf8_bom);
    CHECK(!strcmp(b->lines[0].text, "a") && !strcmp(b->lines[1].text, "b") && !strcmp(b->lines[2].text, ""));
    CHECK(ed_buffer_save(b, output, err, sizeof(err)));
    CHECK(read_bytes(output, data, &count));
    CHECK(count == sizeof(payload) && memcmp(data, payload, sizeof(payload)) == 0);
    CHECK(ed_workspace_open(&ws, output, err, sizeof(err)) && ws.count == 1);
    ed_workspace_dispose(&ws);
    remove(source);
    remove(output);
    return 1;
}

static int test_syntax(void) {
    const char *start = "int a = 42; /* note";
    const char *end = " continued */ return \"okay\";";
    const char *py = "def hello(x): # greeting";
    unsigned char colors[256];
    int block = 0;
    CHECK(ed_syntax_detect("main.cpp") == ED_LANG_CPP);
    CHECK(ed_syntax_detect("script.py") == ED_LANG_PYTHON);
    CHECK(ed_syntax_detect("test.cs") == ED_LANG_CSHARP);
    CHECK(ed_syntax_detect("config.json") == ED_LANG_JSON);
    CHECK(ed_syntax_detect("Makefile") == ED_LANG_PLAIN);
    ed_syntax_line(ED_LANG_C, start, strlen(start), colors, &block);
    CHECK(colors[0] == ED_COLOR_KEYWORD && block == 1);
    CHECK(colors[8] == ED_COLOR_NUMBER);
    ed_syntax_line(ED_LANG_C, end, strlen(end), colors, &block);
    CHECK(colors[0] == ED_COLOR_COMMENT && block == 0);
    CHECK(colors[14] == ED_COLOR_KEYWORD);
    ed_syntax_line(ED_LANG_PYTHON, py, strlen(py), colors, &block);
    CHECK(colors[0] == ED_COLOR_KEYWORD);
    CHECK(colors[strlen(py) - 1] == ED_COLOR_COMMENT);
    return 1;
}

static int test_line_stress(void) {
    EdWorkspace ws;
    EdBuffer *b;
    size_t i;
    ed_workspace_init(&ws);
    CHECK(ed_workspace_new(&ws));
    b = ed_workspace_current(&ws);
    for (i = 0; i < 2000; ++i) {
        CHECK(ed_buffer_insert_line(b, b->row + 1));
        CHECK(ed_buffer_insert(b, "test", 4));
    }
    CHECK(b->line_count == 2001);
    CHECK(!strcmp(b->lines[1500].text, "test"));
    ed_buffer_go(b, 0);
    for (i = 0; i < 2000; ++i) CHECK(ed_buffer_delete_line(b));
    CHECK(b->line_count == 1 && !strcmp(b->lines[0].text, "test"));
    ed_workspace_dispose(&ws);
    return 1;
}

int main(void) {
    if (!test_text_operations() || !test_unicode() || !test_workspace() ||
        !test_io() || !test_syntax() || !test_line_stress()) return 1;
    printf("PASS: %d editor core checks\n", passes);
    return 0;
}
