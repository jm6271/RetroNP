# Architecture

RetroNP is a C++17 Win32 application with a custom GDI interface. `main.cpp` creates one `App`, gives its address to the window, and runs the message loop. `App` coordinates the document, viewport, dialogs, and rendering resources for that window.

## Source map

| Files | Responsibility |
| --- | --- |
| `src/main.cpp` | Process startup, command-line arguments, and message loop. |
| `src/window.cpp` | Win32 message adapter, DPI changes, resizing, focus, and drag-and-drop. |
| `src/app.h`, `src/app.cpp` | Controller state, viewport layout, coordinate mapping, title updates, and resource lifetime. |
| `src/editor.h`, `src/editor.cpp` | Platform-independent text, selection, navigation, undo/redo, search, and visual row calculation. |
| `src/file_io.h`, `src/file_io.cpp` | Encoding detection, text decoding, size/binary validation, CRLF output, and temporary-file replacement. No UI dependency. |
| `src/document_commands.cpp` | Document actions, unsaved-change requests, clipboard integration, and dispatch of menu commands. |
| `src/dialogs.cpp` | Dialog state transitions, save/discard/cancel decisions, and overwrite confirmation. |
| `src/keyboard.cpp`, `src/mouse.cpp` | Input routing, selection, hit testing, and scrolling. |
| `src/rendering.cpp` | Bitmap fonts, drawing primitives, title buttons, menus, editor cells, and scrollbars. |
| `src/dialog_rendering.cpp` | Modal dialog geometry and drawing. |
| `src/presenter.cpp` | Cached logical canvas, device scaling, caret composition, invalidation, and painting. |
| `src/graphics.h`, `src/graphics.cpp` | Classic palette, shared icon pixels, bitmap creation, and the owning frame buffer. |

`retronp_editor` is a standalone CMake library without a Win32 dependency. `retronp_app` contains the controller and Windows implementation, and links the editor. Both the executable and integration tests link the application library; tests never include an implementation `.cpp` file.

## State and coordinates

- The editor stores LF line endings. Reading normalizes CRLF/CR through `Editor::reset`; disk writes and clipboard copies produce CRLF.
- Text positions are wide-string offsets, which are UTF-16 code units on Windows. `previous` and `next` keep surrogate pairs intact. These helpers do not implement full Unicode grapheme segmentation.
- Selection uses a moving `caret` and a fixed `anchor`. `lo` and `hi` return ordered, half-open selection bounds. Undo snapshots include both endpoints; the saved text determines whether the document is dirty.
- Rows are visual segments with half-open document offsets. Soft wrapping can place adjacent row boundaries at the same offset. Tabs occupy space up to the next eight-column stop.
- Drawing and hit testing use logical pixels at 96 DPI. The window adapter translates device input to logical pixels. The presenter scales the finished logical image to the client size with nearest-neighbor scaling.
- `top` and `left` are scroll offsets in visual rows and columns. `preferredColumn` preserves the horizontal target during repeated vertical navigation.

## Interaction flow

Keyboard and pointer events go to a modal dialog first, an open menu second, and the document last. Dialog fields use a separate `Editor`, keeping their undo history independent of the document.

New, Open, Drop, and Exit go through `request`. A dirty document defers the requested action in `pending` until the user saves or discards. Save As and overwrite confirmation can span several events, so this state belongs to the controller. Cancel or a failed save clears the pending action. `continuePending` consumes the action before executing it.

`WM_KEYDOWN` handles navigation and commands; `WM_CHAR` inserts translated characters. A dialog keydown can close the dialog before its queued character arrives. `eatCharacter` consumes that character so accepting Save cannot insert a newline into the document.

The file service reads and validates into a result before the controller changes the active document. Saving writes a temporary sibling file, flushes and closes it, then replaces the destination. The controller updates its path and saved snapshot only after success. Expected file errors carry a display message; the controller chooses the dialog to display it in.

## Rendering and resource ownership

The controller owns its fonts and cached logical canvas. `FrameBuffer` owns a separate device-sized image and cannot be copied. GDI bitmaps must be deselected from their DC before deletion. The `App` destructor releases its canvas and fonts; the frame buffer releases its resources through its own destructor. The window borrows the controller through `GWLP_USERDATA` for the message-loop lifetime.

Content or UI changes mark the logical canvas dirty. Painting rebuilds it only when needed, scales it into the device frame, adds the current caret, and copies the finished frame to the window. Caret blinking invalidates only the caret rectangle without rebuilding the logical canvas. Moving within the same menu item causes no repaint; changing the highlighted item invalidates the popup region.

## Validation

Run `./build.ps1` for the MinGW release build or `./build-msvc.ps1` for the MSVC Debug build. Both build the executable and run the tests. The explicit source list in `build.ps1` must stay aligned with `CMakeLists.txt` when adding an application source file.

- `editor_tests` checks normalization, editing, selection, undo/redo, search, wrapping, tabs, and surrogate-pair navigation.
- `io_tests` creates a hidden window and exercises the real application library: encodings, round trips, failed opens/saves, dialog Enter handling, overwrite consent, and unsaved-change decisions.
- `tests/render_checks.h` renders into memory at 100% and 225% scaling. It checks caret-only changes, image reuse, typing, menu hover invalidation, and dialog boundaries.

These checks exercise the renderer and controller without desktop input injection. They do not replace a manual check of dragging, resizing, keyboard layout behavior, or display changes in a visible window.
