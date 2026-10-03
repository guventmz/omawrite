# Omawrite

A dead-simple Markdown writing app built with Qt Quick and C++ that automatically follows system dark/light mode.

<img width="2948" height="3227" alt="screenshot-2026-06-23_15-24-08" src="https://github.com/user-attachments/assets/4e930c0d-edda-4046-b444-a59eff523329" />
<img width="2948" height="3227" alt="screenshot-2026-06-23_15-23-23" src="https://github.com/user-attachments/assets/8ced7c26-961b-4ded-b263-84403001a951" />


## Install

Install via the Omarchy Package Repository via the `omawrite` package. It's installed by default in new installations of Omarchy (from Quattro forward).

## Notes and links

Point Omawrite at a folder of Markdown notes (`Ctrl+Shift+O`, or the prompt the first
time you press `Ctrl+P`) and it becomes a small, keyboard-first knowledge base. Notes
stay plain `.md` files; nothing else is written to the folder.

- `Ctrl+P` fuzzy-finds a note by title (or folder). If nothing matches, Enter creates it.
- `Ctrl+Shift+F` searches the text of every note and jumps to the matching line.
- `[[Title]]` links to `Title.md` anywhere in the folder; `[[Title|shown text]]` and
  `[[Title#Heading]]` work too. Typing `[[` offers notes to link.
- `Ctrl+Enter`, or `Ctrl`-click, follows the link under the cursor, creating the note
  if it does not exist yet.
- `Ctrl+[` and `Ctrl+]` go back and forward through the notes you visited.
- `Ctrl+Shift+B` lists the notes that link to the current one. The footer shows the count.

`Ctrl+Shift+E` opens the file tree on the current note (press it again to jump in, and
once more to close). In the tree: arrows move and fold, `Enter` opens, `N` creates a note,
`Shift+N` a folder, `R` renames, `M` moves (pick the folder by fuzzy search),
`Cmd+Backspace` / `Delete` moves to the Trash, `Escape` returns to the editor. Notes and
folders can also be dragged onto folders. Renaming a note rewrites the `[[links]]` to it.

`Ctrl+,` shows every shortcut, grouped, with a filter.

Moving to another note saves the one you leave, so following links never stops to ask.
Folders starting with a dot (`.obsidian`, `.git`, `.trash`) are ignored.

## Look and layout

- `Ctrl+Alt+T` (or the half-circle button in the footer) picks a theme: Omawrite, Paper, Nord,
  Solarized, Gruvbox or High Contrast. Arrow keys preview live, Enter keeps, Escape reverts.
  Each theme has a light and a dark variant that follows the system; headings get their
  own color. The Omawrite theme still takes Omarchy's colors.
- `Ctrl+Alt+L` cycles wrapping: a readable column, the window width, or off (long lines
  scroll sideways). Also in the font popup.
- Fenced code blocks get light syntax colors in a monospace font, and `$inline$` and
  `$$display$$` math are colored and kept verbatim, so Markdown never mangles them.
- `Ctrl+Shift+V` (or the eye button) opens the note rendered in your browser, in the same
  theme, font and column: formatted Markdown, typeset math (KaTeX) and highlighted code.
  The page loads marked, KaTeX and highlight.js from cdnjs, so it needs a connection;
  offline it shows the plain text.

## Spelling (macOS)

Omawrite uses the system spell checker, so your languages, learned words and
System Settings preferences carry over:

- Misspelled prose gets the usual red dots; code, math, links and URLs are never checked.
- Right-click (or Control-click) a word for suggestions, Learn / Ignore Spelling, Look Up,
  and the spelling toggles. `⌃⌘D` looks up the word at the cursor.
- Your text replacements (System Settings > Keyboard > Text Replacements) expand as you
  type, and spelling is corrected automatically when that system setting is on.

## Shortcuts

- `Ctrl+S` saves. Unsaved documents use the XDG desktop portal file picker.
- `Ctrl+Shift+S` saves as.
- `Ctrl+O` opens a Markdown file through the portal picker.
- `Ctrl+Shift+P` opens the system print dialog.
- `Ctrl+N` opens a new Omawrite window.
- `Ctrl+Z`, `Ctrl+Shift+Z`, and `Ctrl+Y` handle undo and redo.
- `Super+F` toggles fullscreen. Qt maps this key as `Meta+F`.
- `Ctrl+F` searches the document. Use `Enter` or `Ctrl+G` for the next match and `Shift+Enter` for the previous match.
- `Ctrl+H` opens find and replace.
- `Ctrl+B`, `Ctrl+I`, and `Ctrl+K` insert bold, italic, and link Markdown.
- `Ctrl+?` shows the keyboard shortcut reference.

Unsaved drafts are recovered after an abnormal exit. Omawrite also watches open files
and warns before an external change can replace local work.

The font button in the footer picks the writing font from any installed text font,
and Omawrite remembers the choice. IBM Plex Mono is the default.

Text size and interface size are separate knobs: `Ctrl+=` / `Ctrl+-` / `Ctrl+0` size the
writing, `Ctrl+Alt+=` / `Ctrl+Alt+-` / `Ctrl+Alt+0` size everything around it (`⌘` and
`⌥⌘` on macOS). Both are also in the font popup and are remembered.

Both start from the desktop text size — `omarchy display text size`, or GNOME's
`text-scaling-factor` — and re-flows without a restart. The default of 12px leaves
Omawrite at the size it is designed around; larger and smaller sizes scale from there.

## Requirements

- Qt 6: `qt6-base`, `qt6-declarative`, `qt6-quickcontrols2`
- `xdg-desktop-portal` and a portal backend

## macOS

Omawrite also builds as a native `Omawrite.app` on macOS 14 or later.

```sh
brew install qt
bin/build     # build/Omawrite.app, linked against Homebrew Qt (for development)
bin/test      # unit tests
bin/bundle    # build/dist/Omawrite.app, self-contained via macdeployqt
bin/install   # bundles and copies the app to /Applications
```

On macOS, shortcuts use `⌘` wherever Linux uses `Ctrl`, with a few exceptions:

- `⌥⌘F` opens find and replace, since `⌘H` hides the app.
- `⌃⌘F` toggles fullscreen.
- `⌘W` closes the window and `⌘M` minimizes it.

Dark and light mode follow the system appearance. Markdown files open from Finder
and "Open With"; a file opened while the window holds a document gets a new window.
Text size stays at the default, since macOS has no desktop text scale setting.

The IBM Plex Mono font is bundled under the SIL Open Font License 1.1; see
`fonts/OFL.txt`. The font is copyright IBM Corp.
