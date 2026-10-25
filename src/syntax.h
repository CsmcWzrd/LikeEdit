#ifndef LIKEEDIT_SYNTAX_H
#define LIKEEDIT_SYNTAX_H
#include <stddef.h>

typedef enum {
    ED_LANG_PLAIN, ED_LANG_C, ED_LANG_CPP, ED_LANG_CSHARP, ED_LANG_PYTHON,
    ED_LANG_JAVASCRIPT, ED_LANG_JSON, ED_LANG_SHELL, ED_LANG_HTML, ED_LANG_CSS,
    ED_LANG_RUST, ED_LANG_GO, ED_LANG_JAVA, ED_LANG_SQL
} EdLanguage;

typedef enum {
    ED_COLOR_TEXT, ED_COLOR_KEYWORD, ED_COLOR_STRING, ED_COLOR_NUMBER,
    ED_COLOR_COMMENT, ED_COLOR_DIRECTIVE, ED_COLOR_TYPE, ED_COLOR_OPERATOR
} EdSyntaxColor;

EdLanguage ed_syntax_detect(const char *path);
const char *ed_syntax_name(EdLanguage language);
/* Colors are assigned per UTF-8 byte, for the exact original string length.
   in_block_comment is carried over between consecutive source lines. */
void ed_syntax_line(EdLanguage language, const char *line, size_t len,
                    unsigned char *colors, int *in_block_comment);
#endif
