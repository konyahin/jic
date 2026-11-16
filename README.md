# jic

A home for small terminal utilities. Built for [iSH](https://ish.app)
(Alpine Linux, i686) on iPhone, runs on any POSIX terminal.

## Tools

- `try`: scratch pad for trying out ideas.
- `reader FILE`: pager for text files; `n` and `p` turn pages, `q` quits.

## Build

```
make            # build/NAME
make debug      # build/debug/NAME, UBSan traps at the faulting line
make test       # prelude self-test with a fuzz loop, ITERS=n
make cross      # build/i686/NAME, static musl via zig
make deploy     # cross, then copy to DEST (iCloud logs/bin)
```

The vendored termbox2 is patched: `tb_print_ex` no longer returns
`TB_ERR_OUT_OF_BOUNDS` when the start point is off screen. Its loop already
clips each cell, so a line starting left of the screen now shows its visible
part instead of nothing. Reapply after upgrading: delete this check at the
top of `tb_print_ex`:

```c
if (!cellbuf_in_bounds(&global.back, x, y)) {
    return TB_ERR_OUT_OF_BOUNDS;
}
```

A tool is `src/NAME.c`: it includes `vendor/termbox2.h`, then `prelude.c`,
and defines its own `main`, which sets up the arenas it needs. Add `NAME` to
`TOOLS` in the Makefile.

## License

Public domain, see [LICENSE](LICENSE) (Unlicense).
`vendor/termbox2.h` keeps its own MIT license.
