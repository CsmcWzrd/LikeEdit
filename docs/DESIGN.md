# LikeEdit architecture

LikeEdit is a Windows console application written in C11. It is intentionally **vi-inspired**, not Vim-compatible.

- `src/editor_core.[ch]`: platform-independent UTF-8 line-based document engine, multi-buffer workspace, I/O, cursor movement, line splitting and joining.
- `src/syntax.[ch]`: stateless-per-line lexical scanner; block comment state is carried across lines.
- `src/win_editor.c`: Windows console UI, input processing, drawing, command interpreter, editor modes, and bottom-half buffer selector.
- `tests/test_core.c`: portable unit/regression tests.

## Input and screen

Windows `ReadConsoleInputW` is used for key events and modifier detection, since standard `<conio.h>` `_getwch()` cannot reliably distinguish Ctrl+T from Ctrl+Shift+T. A real `_getwch()` fallback is implemented when console input records fail, and `_cputws()` provides console output on exit. `WriteConsoleOutputW` draws an attributed cell grid for syntax colors. No ncurses, GUI toolkit, or third-party editor components are used.

Ctrl+Shift+T (or Ctrl+T) is accepted **in command mode**. The bottom half of the viewport is repainted as a buffer picker; Enter switches to the selected file and changes directly to **edit mode**. Esc dismisses the picker and returns to command mode. `:buffers` provides a fallback where a terminal host intercepts Ctrl+Shift+T.

## Current limitations

- Syntax highlighting is lexical (not a full parser), and rendering from the start for multiline comment state can be slow in exceptionally large files.
- UTF-8 is stored losslessly, but the Win32 console cell display can approximate supplementary Unicode and combined/wide glyphs. The rendering of those glyphs depends on the console host/font.
- No undo tree, macros, split-window editing, swap files, regex search, clipboard, or Vim plugin/command compatibility.
- Only UTF-8 text is supported; binary, legacy code pages and UTF-16 are rejected instead of corrupted. A maximum file size of 128 MiB is enforced. Detected CRLF/LF style and UTF-8 BOM are preserved; mixed-ending files are normalized.
- Saves write directly to the destination and may leave an incomplete file on I/O failure. For critical data, use version control or backups.
- The core can be built and tested on POSIX, but the interactive application requires Windows.
