# C++ source style

The conventions below are the preferred style for new C++ integration code.
Imported and modified IndependentMDX, NoteMDX and Independent OPM files use
mixed upstream and Workbench formatting. In particular, native/src/player.cpp
contains Workbench changes and formatting edits; it is not a verbatim upstream
copy. Existing files do not uniformly satisfy every rule below. Formatting is
not an indication of authorship or license; see THIRD_PARTY_NOTICES.txt for the
path attribution table.

- `{` is on its own line, vertically aligned with `}`.
- Braces are not omitted .
- Chained conditions use vertically aligned `&&` / `||`.
- Semicolons end visual lines.
- Operators have surrounding spaces where applicable.
- Parentheses are retained around comparison expressions when they clarify precedence.
- `case` and `break` align with `switch`.
- `else if` is written as `else`, followed by `if` on the next line.
- Related declarations should align variable-name columns.
- Related assignments should align `=` columns.
- Variables are declared one per declaration.
- `//` comments are placed on the preceding line.
- Repeated hexadecimal command/register literals should use named constants.
- The conditional operator `?:` is prohibited; use `if` / `else`.
