## X11 minesweeper

This is a minesweeper clone written as a X11 client in C, as detailed
by [this article](https://gaultier.github.io/blog/write_a_video_game_from_scratch_like_1987.html).

The client uses the xcb library to interact with the X11 server and
libpng to load the bitmap assets.

I also added the ability to flag suspected cells and fixed some
offsets in the bitmap.

To build and run:

```
$ meson setup build
$ cd build
$ ninja
$ cd ..
$ build/x11minesweeper
```
