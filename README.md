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

`Ctrl+Shift+E` opens the file tree in a popup, on the current note. The tree speaks netrw: `j`/`k`
move, `Enter` opens, `-` goes up, `%` creates a note, `d` a folder, `R` renames, `D` trashes,
`/` searches the whole vault and jumps to the match,
`mf` / `mt` / `mm` mark notes, pick a target folder and move them there, and `M` moves the
selection through a fuzzy folder picker. Arrows, `h`/`l`, `gg`/`G` and `Escape` work too.
Every picker also moves with `Ctrl+N` / `Ctrl+P`. Notes and
folders can also be dragged onto folders. Renaming a note rewrites the `[[links]]` to it.

`Ctrl+,` shows every shortcut, grouped, with a filter, and is where you change them: pick
one (arrows, `Ctrl+N` / `Ctrl+P`, or type to filter), press `Enter` or click its keys, then
press the new keys. Keys another action had move over (the sheet says from where);
`Backspace` on an empty filter restores the default. Changed shortcuts are marked with a dot.
The vim and file tree keys are listed there for reference.

`Ctrl+Alt+V` (`⌥⌘V`) turns on vim mode: normal, insert and visual modes with the usual
motions, operators, text objects, counts, `.` repeat, `/` search and `:w` / `:q`. On macOS,
vim's Ctrl keys are the Control key, so `⌘` shortcuts keep working alongside it.

Moving to another note saves the one you leave, so following links never stops to ask.
Folders starting with a dot (`.obsidian`, `.git`, `.trash`) are ignored.

## Journal

`Ctrl+Shift+J` (or the calendar button in the footer) opens today's journal entry, with the
caret at the end, ready to write. Entries are plain notes named by date, `Journal/2026-10-11.md`,
and a new one starts with the date written out as its heading. An entry that already exists
anywhere in the vault is reused, so a folder of Obsidian daily notes works as is, and
`[[2026-10-11]]` links to that day. `Ctrl+Alt+Left` and `Ctrl+Alt+Right` step to the previous
and next entry that exists, skipping days without one.

## Images

Images stay files in the vault, in `assets/` at its root. Pasting an image (`Cmd+V` with a
screenshot on the clipboard) or dragging image files onto a note saves them there and embeds
them as `![[Pasted image 20261003153012.png]]`, as Obsidian does. Both `![[name.png]]` and
`![alt](name.png)` are found relative to the note first, then by file name anywhere in the
vault, so moving notes or images never breaks them.

With the cursor on a line holding an image, a thumbnail floats under it; `Cmd+Enter` or
`Cmd`-click opens the image in Quick Look. Images show in the file tree too, where renaming
one rewrites the notes that embed it. The browser preview renders them in place.

## Look and layout

- `Ctrl+Alt+T` (or the half-circle button in the footer) picks a theme: Omawrite, Paper, Nord,
  Solarized, Gruvbox or High Contrast. Arrow keys preview live, Enter keeps, Escape reverts.
  Each theme has a light and a dark variant that follows the system; headings get their
  own color. The Omawrite theme still takes Omarchy's colors.
- `Ctrl+Alt+L` cycles wrapping: a readable column, the window width, or off (long lines
  scroll sideways). Also in the font popup.
- Fenced code blocks get light syntax colors in a monospace font, and `$inline$` and
  `$$display$$` math are colored and kept verbatim, so Markdown never mangles them.
- `Ctrl+Shift+P` (or the eye button) opens the note rendered in your browser, in the same
  theme, font and column: formatted Markdown, typeset math (KaTeX) and highlighted code.
  The page loads marked, KaTeX and highlight.js from cdnjs, so it needs a connection;
  offline it shows the plain text.

## The same look in Obsidian

`extras/obsidian/omawrite-paper.css` is an Obsidian CSS snippet with Omawrite's Paper theme:
IBM Plex Mono at 20px, 1.4 line height, a 65-character column, and body-size bold headings
in the heading color. Copy it into `<vault>/.obsidian/snippets/`, enable it under Settings >
Appearance > CSS snippets, and use Obsidian's default theme. Install all four IBM Plex Mono
styles from `fonts/`, or Obsidian fakes bold and italic.

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
- `Ctrl+Alt+P` opens the system print dialog.
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
