# hack_olc

A CPU emulator and GUI for the **Hack platform** — the 16-bit computer built up
from NAND gates in [*The Elements of Computing Systems*](https://www.nand2tetris.org/)
("nand2tetris"). It's a from-scratch reimplementation of the Hack CPU, screen,
and keyboard, driven by [olcPixelGameEngine](https://github.com/OneLoneCoder/olcPixelGameEngine)
(PGE) for the window, rendering, and input.

The active implementation lives in [`cpp/`](cpp/README.md) — C++20 against
PGE3, buildable natively (Windows) or as a browser WASM build via Emscripten.
An earlier Rust port against PGE2 used to live at the repo root; it's been
removed in favor of the C++ version, which is now the only one maintained
here. It's still recoverable from git history if it's ever needed again.

## What's bundled here

- `hello.hackem` — a minimal "Hello, World!" ROM; also the fixture the C++
  test suite (`cpp/tests/test_engine.cpp`) asserts against directly, so it's
  not just a demo — don't replace its contents.
- `tetris.hackem` — a full Tetris game; the default ROM `hack_olc` runs with
  no arguments (both native and WASM builds).
- `tests/data/test2.hackem` — a second, smaller fixture used only by the test
  suite.

See [`cpp/README.md`](cpp/README.md) for how to build, test, and run.

## The `.hackem` format

`.hackem` is a small text-based binary format (not part of the original
nand2tetris spec) for loading a Hack program's ROM and initial RAM state
directly, without running an assembler bootstrap at load time. A file looks
like:

```
hackem v1.0 0x0033      <- format version + the CPU's expected halt address
ROM@0000                <- section marker + starting address
0100                    <- one 16-bit hex word per line, loaded sequentially
ec10
...
```

`hack_olc`'s loader (`cpp/src/code_loader.cpp`) reads this format (and also
accepts raw newline-separated binary `.hack` files, for compatibility with
stock nand2tetris tooling). It does not have a `.hackem` *writer* — that's a
compiler/toolchain concern, not an emulator concern.

## Relationship to `hack_cc`

`hack_cc` is a separate, sibling project: a C compiler, assembler, and its
own fast Hack emulator/IDE, all written in Rust. It's where `.hackem` files
actually get produced — `tetris.hackem` here was compiled from `hack_cc`'s
`demo/tetris.c` via its `hack_cc.exe` compiler, not written by hand or built
by anything in this repo.

The two projects are decoupled on purpose: `hack_olc` is a Hack CPU
implementation plus a PGE-based screen/keyboard/GUI shell around it, and only
cares that its input is a valid `.hackem` (or `.hack`) binary. Nothing in
`hack_olc`'s build depends on `hack_cc` being present or built. When
`tetris.hackem` needs to be regenerated (say, after a `tetris.c` change), the
flow is manual: compile it with `hack_cc.exe`, then copy the resulting
`.hackem` file into this repo's root, replacing `tetris.hackem`.
