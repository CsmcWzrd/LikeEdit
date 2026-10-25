#include "syntax.h"
#include <ctype.h>
#include <string.h>

static const char *const cwords =
    " auto break case char const continue default do double else enum extern float for goto if inline int long register restrict return short signed sizeof static struct switch typedef union unsigned void volatile while _Bool _Atomic _Generic _Static_assert ";
static const char *const cppwords =
    " alignas alignof asm bool catch class constexpr consteval constinit decltype delete explicit export false friend mutable namespace new noexcept nullptr operator override private protected public reinterpret_cast requires static_assert template this thread_local throw true try typename using virtual wchar_t co_await co_return co_yield ";
static const char *const cswords =
    " abstract as async await base bool byte checked class decimal delegate dynamic event explicit false finally fixed foreach get implicit in interface internal is lock namespace null object out override params partial private protected public readonly ref sbyte sealed set string this throw true try typeof unchecked unsafe ushort using var virtual where yield record init ";
static const char *const pywords =
    " and as assert async await break class continue def del elif else except False finally for from global if import in is lambda nonlocal not or pass raise return True try while with yield None match case ";
static const char *const jswords =
    " as async await break case catch class const continue debugger default delete do else export extends false finally for from function if import in instanceof let new null of return static super switch this throw true try typeof undefined var void while yield ";
static const char *const rustwords =
    " as async await break const continue crate dyn else enum extern false fn for if impl in let loop match mod move mut pub ref return self Self static struct super trait true type unsafe use where while ";
static const char *const gowords =
    " break case chan const continue default defer else fallthrough for func go goto if import interface map package range return select struct switch type var true false nil ";
static const char *const javawords =
    " abstract assert boolean break byte case catch char class const continue default do double else enum extends final finally float for if implements import instanceof int interface long native new null package private protected public return short static strictfp super switch synchronized this throw throws transient true try void volatile while ";
static const char *const sqlwords =
    " select from where join left right inner outer on as insert into update delete create table alter drop group by order having distinct limit offset null not and or between like in is exists primary key foreign references values set union all case when then end asc desc count sum avg min max ";
static const char *const types =
    " size_t ssize_t uint8_t uint16_t uint32_t uint64_t int8_t int16_t int32_t int64_t FILE HANDLE DWORD WORD BYTE WCHAR LPVOID string String Integer Boolean Object std ";

static int word_in(const char *word, size_t len, const char *set) {
    const char *scan = set;
    while (*scan) {
        const char *start;
        while (*scan == ' ') scan++;
        if (!*scan) break;
        start = scan;
        while (*scan && *scan != ' ') scan++;
        if ((size_t)(scan - start) == len && memcmp(start, word, len) == 0) return 1;
    }
    return 0;
}

static int iequal(const char *x, const char *y) {
    while (*x && *y) {
        if (tolower((unsigned char)*x) != tolower((unsigned char)*y)) return 0;
        x++; y++;
    }
    return !*x && !*y;
}

EdLanguage ed_syntax_detect(const char *path) {
    const char *dot;
    if (!path) return ED_LANG_PLAIN;
    dot = strrchr(path, '.');
    if (!dot) return ED_LANG_PLAIN;
    if (iequal(dot, ".c") || iequal(dot, ".h")) return ED_LANG_C;
    if (iequal(dot, ".cpp") || iequal(dot, ".cc") || iequal(dot, ".cxx") || iequal(dot, ".hpp") || iequal(dot, ".hh")) return ED_LANG_CPP;
    if (iequal(dot, ".cs")) return ED_LANG_CSHARP;
    if (iequal(dot, ".py")) return ED_LANG_PYTHON;
    if (iequal(dot, ".js") || iequal(dot, ".jsx") || iequal(dot, ".ts") || iequal(dot, ".tsx")) return ED_LANG_JAVASCRIPT;
    if (iequal(dot, ".json") || iequal(dot, ".jsonc")) return ED_LANG_JSON;
    if (iequal(dot, ".sh") || iequal(dot, ".bash") || iequal(dot, ".zsh")) return ED_LANG_SHELL;
    if (iequal(dot, ".html") || iequal(dot, ".htm") || iequal(dot, ".xml")) return ED_LANG_HTML;
    if (iequal(dot, ".css") || iequal(dot, ".scss")) return ED_LANG_CSS;
    if (iequal(dot, ".rs")) return ED_LANG_RUST;
    if (iequal(dot, ".go")) return ED_LANG_GO;
    if (iequal(dot, ".java")) return ED_LANG_JAVA;
    if (iequal(dot, ".sql")) return ED_LANG_SQL;
    return ED_LANG_PLAIN;
}

const char *ed_syntax_name(EdLanguage lang) {
    static const char *const names[] = { "Text", "C", "C++", "C#", "Python", "JavaScript", "JSON", "Shell", "HTML/XML", "CSS", "Rust", "Go", "Java", "SQL" };
    return lang >= ED_LANG_PLAIN && lang <= ED_LANG_SQL ? names[lang] : "Text";
}

static int is_name(int c) { return isalnum((unsigned char)c) || c == '_'; }
static int is_first(int c) { return isalpha((unsigned char)c) || c == '_'; }
static void color_range(unsigned char *colors, size_t begin, size_t end, unsigned char color) {
    size_t i;
    for (i = begin; i < end; ++i) colors[i] = color;
}

void ed_syntax_line(EdLanguage lang, const char *s, size_t n, unsigned char *colors, int *block) {
    size_t i = 0;
    int comment = block ? *block : 0;
    int hash_comment = lang == ED_LANG_PYTHON || lang == ED_LANG_SHELL;
    int slash_comment = lang == ED_LANG_C || lang == ED_LANG_CPP || lang == ED_LANG_CSHARP ||
                        lang == ED_LANG_JAVASCRIPT || lang == ED_LANG_RUST || lang == ED_LANG_GO ||
                        lang == ED_LANG_JAVA || lang == ED_LANG_JSON || lang == ED_LANG_CSS;
    int block_comment = slash_comment || lang == ED_LANG_SQL || lang == ED_LANG_HTML;
    const char *words = NULL;
    memset(colors, ED_COLOR_TEXT, n);
    switch (lang) {
    case ED_LANG_C: words = cwords; break;
    case ED_LANG_CPP: words = cppwords; break;
    case ED_LANG_CSHARP: words = cswords; break;
    case ED_LANG_PYTHON: words = pywords; break;
    case ED_LANG_JAVASCRIPT: words = jswords; break;
    case ED_LANG_RUST: words = rustwords; break;
    case ED_LANG_GO: words = gowords; break;
    case ED_LANG_JAVA: words = javawords; break;
    case ED_LANG_SQL: words = sqlwords; break;
    default: break;
    }
    while (i < n) {
        size_t start = i;
        if (comment) {
            if (lang == ED_LANG_HTML) {
                while (i < n && !(i + 2 < n && s[i] == '-' && s[i+1] == '-' && s[i+2] == '>')) i++;
                if (i + 2 < n) { i += 3; comment = 0; }
            } else {
                while (i < n && !(i + 1 < n && s[i] == '*' && s[i+1] == '/')) i++;
                if (i + 1 < n) { i += 2; comment = 0; }
            }
            color_range(colors, start, i, ED_COLOR_COMMENT);
            continue;
        }
        if ((hash_comment && s[i] == '#') ||
            (slash_comment && i + 1 < n && s[i] == '/' && s[i+1] == '/') ||
            (lang == ED_LANG_SQL && i + 1 < n && s[i] == '-' && s[i+1] == '-')) {
            color_range(colors, i, n, ED_COLOR_COMMENT);
            break;
        }
        if (block_comment && ((i + 1 < n && s[i] == '/' && s[i+1] == '*') ||
            (lang == ED_LANG_HTML && i + 3 < n && memcmp(s + i, "<!--", 4) == 0))) {
            comment = 1;
            continue;
        }
        if ((lang == ED_LANG_C || lang == ED_LANG_CPP) && s[i] == '#' &&
            (i == 0 || s[i-1] == ' ' || s[i-1] == '\t')) {
            color_range(colors, i, n, ED_COLOR_DIRECTIVE);
            break;
        }
        if (s[i] == '"' || s[i] == '\'' || (lang == ED_LANG_PYTHON && s[i] == '`')) {
            char quote = s[i++];
            while (i < n) {
                if (s[i] == '\\' && i + 1 < n) { i += 2; continue; }
                if (s[i++] == quote) break;
            }
            color_range(colors, start, i, ED_COLOR_STRING);
            continue;
        }
        if (isdigit((unsigned char)s[i]) && (i == 0 || !is_name(s[i-1]))) {
            while (i < n && (isalnum((unsigned char)s[i]) || s[i] == '.' || s[i] == '_')) i++;
            color_range(colors, start, i, ED_COLOR_NUMBER);
            continue;
        }
        if (is_first((unsigned char)s[i])) {
            while (i < n && is_name((unsigned char)s[i])) i++;
            if (words && word_in(s + start, i - start, words)) color_range(colors, start, i, ED_COLOR_KEYWORD);
            else if (word_in(s + start, i - start, types)) color_range(colors, start, i, ED_COLOR_TYPE);
            else if (lang == ED_LANG_JSON && (word_in(s + start, i - start, " true false null ")))
                color_range(colors, start, i, ED_COLOR_KEYWORD);
            continue;
        }
        if (strchr("=+-*/%&|!<>^~?:", s[i]) != NULL) colors[i] = ED_COLOR_OPERATOR;
        i++;
    }
    if (block) *block = comment;
}
