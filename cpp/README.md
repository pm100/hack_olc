# hack_olc (C++)

Native C++ port of the Rust `hack_olc` Hack CPU emulator, using
olcPixelGameEngine directly. The Rust project at the repo root is the
original implementation and is kept unmodified alongside this one.

## Build

Requires CMake 3.20+ and a C++20 compiler (developed against MSVC via
Visual Studio 17 2022).

```powershell
cmake -S cpp -B cpp/build -G "Visual Studio 17 2022" -A x64
cmake --build cpp/build --config Release
```

## Test

```powershell
ctest --test-dir cpp/build -C Release --output-on-failure
```

## Run

```powershell
& cpp/build/Release/hack_olc.exe [path/to/rom.hackem]
```

With no argument, runs the bundled `hello.hackem`. Numpad +/- doubles/halves
the emulated clock speed live.

## Known Issues

On at least one development machine (Intel UHD 630 + a DisplayLink USB
display adapter installed system-wide), the built application's window
opens and runs correctly internally — the CPU executes, produces the
correct screen buffer, and reaches HALTED as expected — but the window's
client area renders solid white with no visible pixels. This is believed
to be a graphics-driver/OpenGL-compatibility issue in the vendored
`olcPixelGameEngine.h`, not a defect in the ported emulator logic (all
17 tests pass, and direct engine-state inspection confirms correct
behavior). If you hit this, try a different GPU/monitor/driver, or treat
it as a follow-up investigation into `pge_impl.cpp`'s WGL/OpenGL context
setup.

## Layout

- `src/hack_engine.hpp/.cpp` — CPU core: ALU, fetch/decode/execute,
  breakpoints/watchpoints, disassembler.
- `src/code_loader.cpp` — `.hackem`/`.hack` binary loader.
- `src/screen.hpp/.cpp` — memory-mapped 512x256 1bpp screen device.
- `src/keyboard.hpp/.cpp` — memory-mapped keyboard device.
- `src/main.cpp` — the `olc::PixelGameEngine`-derived application.
- `src/pge_impl.cpp` — the single translation unit compiling in
  olcPixelGameEngine's implementation (see comment in that file before
  adding `OLC_PGE_APPLICATION` anywhere else).
- `third_party/olcPixelGameEngine.h` — vendored single-header library.
- `tests/test_engine.cpp` — Catch2 test suite.

See `../docs/superpowers/specs/2026-09-05-cpp-port-design.md` for the full
design.
