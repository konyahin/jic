# Agents

Read README.md first: build targets, tool layout, the termbox2 patch.

## Code

- Arenas, not `malloc`; `Str`, not NUL-terminated strings; sizes are
  `ptrdiff_t`. Check `src/prelude.c` for a helper before writing one.
- `assert` is `__builtin_unreachable()`: in `-O2` it is a promise to the
  compiler, not a check. Use it only for invariants. Bad input and failed
  syscalls are handled explicitly (see `fail()` in `src/reader.c`).
- The target is iSH: i686, 32-bit `ptrdiff_t`, slow CPU. Size arithmetic
  must not assume 64 bits. Run `make cross` when zig is available.
- Don't touch `vendor/` beyond the patch described in README.md.

## Done

- `make` and `make debug` build without warnings.
- Changes to `src/prelude.c` also pass `make test`.
- A TUI can't be checked headless. Test its logic with a harness in a
  scratch directory, outside the repo:

  ```c
  #define main tool_main
  #include "src/NAME.c"
  #undef main
  int main(void) { /* call NAME.c functions, print results */ }
  ```

  Build it with the debug flags and link `build/termbox2.o`.

## Commits

- Subject only: as short as possible while still clear. No body.
