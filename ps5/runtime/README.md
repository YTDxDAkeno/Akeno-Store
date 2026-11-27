# PS5 title runtime

These files are taken unchanged from [lowbit/ps5-openrct2](https://github.com/lowbit/ps5-openrct2)
at commit `e2d9a826846f8c706360df794d5ccc850a1c1ef0` (`src/runtime/`), licensed GPL-3.0 like that project. They make a statically
linked C/C++ program behave inside a PS5 native title, as measured on the console by that port
(firmware 13.00):

| File | What it fixes |
| --- | --- |
| `heap.c` | malloc from a 2 GiB direct-memory mspace (a title's libc heap is small) |
| `dirent.c` | `opendir`/`readdir` over `sceKernelGetdents` (the libc version fails with EPERM) |
| `path.c` | `realpath`, `openat` and friends that the sandbox refuses or that live in unloaded modules |
| `libc.c` | locale `*_l` functions, `mkstemp`, `strcasestr`, `isatty`, `exit` through `sceSystemServiceLoadExec` |
| `log.c` | stdout/stderr to the kernel log |
| `thread.c` | at least 2 MiB of stack per thread |
| `ps5.ld` | the boilerplate's PIE layout plus the `.eh_frame` bounds libunwind needs |
| `stubs/*.txt` | functions of system modules the payload SDK has no stubs for |

`../patches/sdl/` holds the same port's changes to ps5-payload-dev's SDL 2 fork (zlib): no dynamic
API, no forced libsamplerate, the frame callback, the IME dialog as the only keyboard (a native
title must load libSceImeDialog itself), a software cursor and the DualSense touchpad.
