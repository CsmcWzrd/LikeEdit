# Windows interactive smoke-test checklist

The portable core is covered by `likeedit_tests`. The Windows console backend cannot be run from a Linux build host; verify the following in cmd.exe, PowerShell and Windows Terminal on Windows 10 and Windows 11.

1. Build x64 Debug and x64 Release with Visual Studio 2022. Test Win32 if needed.
2. Open `LikeEdit.exe README.md src\editor_core.c src\syntax.c`. The initial mode is COMMAND and the active file is `syntax.c`.
3. Press Ctrl+Shift+T from COMMAND mode. A selection panel should cover the bottom half of the viewport. In Windows Terminal, if the host intercepts the shortcut, test Ctrl+T or `:buffers` instead.
4. Press Up to select `editor_core.c`, then Enter. Confirm that file is active, the bottom panel is gone, and the mode is EDIT (without pressing `i`).
5. Type some ASCII and Unicode. Test arrows, Enter, Backspace, Delete, Tab and Esc. Confirm syntax colors and line numbers appear.
6. Press Ctrl+O and open a second file with spaces in the path. Confirm the selector shows both full paths and unsaved changes with `[modified]`.
7. Return to COMMAND, type `:buffers` and cancel with Esc; confirm that active file and mode do not change.
8. Type `:w` to save an existing file, or `:w full-path` for Save As. Check content and line endings with a hex viewer.
9. Test `:new` followed by editing and `:q` (refuses to quit); `:bd` (refuses to close); `:bd!` (forces close); `:q!` (forces quit).
10. Resize the terminal while editing and while the selector is open. Ensure the app still repaints rather than crashing.
11. Run `LikeEditTests.exe`; all assertions should pass.
12. Check that the application restores input mode and shell prompt on normal exit.

Important: On Windows Terminal, Ctrl+Shift+T may be a host-level shortcut and cannot be overridden by a console application. Use the supplied alternate keys or change the Windows Terminal keybinding.
