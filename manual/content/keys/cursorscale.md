---
key: CursorScale
summary: How many times larger than its artwork the mouse pointer is drawn.
when_omitted:
  kind: value
  value: "0"
  note: Zero follows picture enlargement during play and keeps the original pointer size in the front-end menus.
---

A positive value sets the pointer's size directly: `2` draws it at twice the size of its artwork. A value above `8` acts as `8`. A negative value draws the pointer at the size of its artwork however large the picture is displayed.

During play, `0` takes the smaller of the picture's horizontal and vertical enlargement and rounds it to the nearest whole number, so a picture displayed at about 2.2 times its rendered size gets a double-size pointer. The pointer is never drawn smaller than its artwork, or more than eight times larger. When the window is resized, a pointer set to `0` is rebuilt at its new size.

In the front-end menus, `0` keeps the pointer at its original artwork size.
A positive value applies in the menus and during play. [`SystemCursor`](/keys/systemcursor/)
replaces the game's arrow with the system pointer and uses the size set by the operating system.

Windows draws the pointer over the picture, so it does not grow when the picture is enlarged. This keeps it moving smoothly while the game is busy.
