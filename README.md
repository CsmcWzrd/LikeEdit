# LikeEdit — a vi-inspired console editor for Windows 10/11

LikeEdit is a standalone terminal text editor written in C11, using native Windows console APIs and `<conio.h>` as a fallback for direct character input. It is **vi-like, not Vim**: it has a small, documented command/edit mode model without Vim's full command language.

## Key feature: bottom-half file picker

Launch several files at once:

```powershell
LikeEdit.exe main.c main.h README.md
```

From **COMMAND** mode, press **Ctrl+Shift+T** (Ctrl + capital T). The bottom half of the console becomes a selectable list of open files. Choose using Up/Down or `j`/`k`, then press **Enter**. The chosen file becomes the active document, and LikeEdit **automatically enters EDIT mode**. Press **Esc** to close the picker without switching.

**Important:** Some Windows Terminal keyboard profiles reserve Ctrl+Shift+T for terminal tabs and do not pass the keystroke to applications. In that case, use **Ctrl+T** (also supported) or type `:buffers` from COMMAND mode. The exact requested shortcut is recognized when the terminal forwards it.

## Editing and navigation

| Context | Key | Action |
|---|---|---|
| COMMAND | `i` | Enter EDIT mode at cursor |
| COMMAND | `a`, `I`, `A` | Insert after cursor, at line start, at line end |
| EDIT | `Esc` | Return to COMMAND mode |
| COMMAND | `Ctrl+Shift+T`, `Ctrl+T`, `:buffers` | Bottom-half open-file selector |
| Selector | `Up`/`Down` or `j`/`k` | Highlight file |
| Selector | `Enter` | Activate highlighted file **and enter EDIT mode** |
| Selector | `Esc` | Dismiss; stay in COMMAND mode |
| Either | Arrows, Home/End, Page Up/Down | Move caret |
| COMMAND | `h`, `j`, `k`, `l`, `0`, `$`, `gg`, `G` | Vi-like movement |
| COMMAND | `x`, `dd` | Delete code point or entire line |
| COMMAND | `o`, `O` | Open a new line below/above and enter EDIT mode |
| EDIT | Typing, Enter, Backspace, Delete, Tab | Insert, split, join, delete; tabs inserted literally |
| Either | `Ctrl+S` | Save; asks for filename if unnamed |
| Either | `Ctrl+O` | Prompt to open another file; enters EDIT mode |
| Either | `Ctrl+N` | New unsaved buffer; enters EDIT mode |
| Either | `Ctrl+Q` | Quit if all buffers are saved |

### Ex commands (type in COMMAND mode)

| Command | Action |
|---|---|
| `:w` or `:w path` | Save active buffer (optionally Save As) |
| `:q` | Quit if all buffers are saved |
| `:q!` | Quit discarding all unsaved changes |
| `:wq`, `:x` | Save current file, then quit if no other dirty buffers |
| `:e path` | Open/switch to a file (EDIT mode) |
| `:new` | New document (EDIT mode) |
| `:bn`, `:bp` | Next / previous open buffer |
| `:bd`, `:bd!` | Close active buffer (force-close when `!`) |
| `:buffers`, `:ls` | Open bottom-half selector |
| `:set number`, `:set nonumber` | Show/hide line numbers |
| `:help` | Brief command reminder |

## Syntax highlighting

Automatic per-file extension highlighting supports C, C++, C#, Python, JavaScript/TypeScript, JSON, shell scripts, HTML/XML, CSS, Rust, Go, Java and SQL. Highlighting handles strings, numbers, keywords, comments and common operators. Multiline comments are recognized. All other extensions use uncolored text.

## Build in Visual Studio 2022

1. Open **`LikeEdit.sln`** in Visual Studio 2022 with the **Desktop development with C++** workload installed (MSVC v143, Windows 10/11 SDK).
2. Select **x64 / Release** or **x64 / Debug**.
3. Build Solution. `LikeEdit.exe` and the `LikeEditTests.exe` test project are created under `out\x64\Release` (or the chosen configuration).
4. Run `LikeEditTests.exe` in the console to execute portable core tests, then run `LikeEdit.exe` in a real console window. If running within the Visual Studio Output window, use Ctrl+F5 or a terminal instead for keyboard input.

For Win32, select the **Win32** platform from the solution configuration. The app uses the Windows console; it is not a WinForms GUI.

### CMake (Visual Studio / Ninja / MinGW)

```powershell
cmake -S . -B build-win -G "Visual Studio 17 2022" -A x64
cmake --build build-win --config Release
ctest --test-dir build-win -C Release --output-on-failure
.\build-win\Release\LikeEdit.exe .\README.md .\src\editor_core.c
```

### NMake (VS2022 Developer Command Prompt)

```bat
nmake /F Makefile.msvc
nmake /F Makefile.msvc check
LikeEdit.exe README.md src\editor_core.c
```

### Portable core tests (Linux/macOS development host)

```sh
cmake -S . -B build-posix -DCMAKE_BUILD_TYPE=Debug
cmake --build build-posix
ctest --test-dir build-posix --output-on-failure
```

On non-Windows platforms CMake builds **only the portable document/syntax core and its tests**. The actual console editor is Windows-only.

## Text encoding and safeguards

- UTF-8 text for file contents, filenames and typing; preserves the detected CRLF or LF line-ending style, a trailing newline, and a UTF-8 BOM when present (mixed line-ending files are normalized to the majority style).
- Supports opening multiple files in memory, retaining each buffer's cursor and scroll position.
- Detects unsaved changes before close/quit, except explicit force commands.
- Rejects invalid UTF-8, NUL-containing files, and files larger than 128 MiB instead of silently damaging them.
- Files are written directly rather than through an atomic temporary rename. Keep external backups or version control for important data.
- No undo, search/replace, external plugins, Vi register operators or Vim-specific features yet.
- Wide, combined and supplementary Unicode glyphs can render imperfectly depending on the Windows console font/host; saved UTF-8 bytes are not changed by rendering.

See [docs/DESIGN.md](docs/DESIGN.md) for design details and [docs/WINDOWS_QA.md](docs/WINDOWS_QA.md) for an interactive Windows validation checklist.
