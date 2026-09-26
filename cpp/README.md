# hack_olc (C++)

A Hack CPU emulator built against olcPixelGameEngine3 (PGE3). Builds
natively (Windows and Linux) or as a browser WASM build via Emscripten.

## Build (native)

Requires CMake 3.20+ and a C++20 compiler (developed against MSVC via
Visual Studio 17 2022). On Linux, install development headers for X11,
OpenGL, libpng, and XInput2 before configuring.

```powershell
cmake -S cpp -B cpp/build -G "Visual Studio 17 2022" -A x64
cmake --build cpp/build --config Release
```

## Test

```powershell
ctest --test-dir cpp/build -C Release --output-on-failure
```

The test suite reads `hello.hackem` and `tests/data/test2.hackem` directly
from the repo root at run time, via a configure-time `HACK_OLC_REPO_ROOT`
path baked into the test binary. This means moving `cpp/` out of the repo,
or moving the whole repo after configuring, will break the tests. (The
built `hack_olc.exe` has a related but separate dependency: the build's
post-build step copies `tetris.hackem` and `hello.hackem` next to the
executable so it has a default ROM to run, plus the original demo, on hand.)

## Run (native)

```powershell
& cpp/build/Release/hack_olc.exe [path/to/rom.hackem]
```

With no argument, runs the bundled `tetris.hackem` (arrow keys to move,
Up to rotate, Space to hard-drop, Q to quit). Pass `hello.hackem` (or any
other `.hackem`/`.hack` ROM) explicitly to run something else. Numpad +/-
doubles/halves the emulated clock speed live.

## Build (WASM)

Requires the [Emscripten SDK](https://emscripten.org/docs/getting_started/downloads.html)
installed and activated (`emsdk install latest && emsdk activate latest`,
then source `emsdk_env.sh`/`emsdk_env.bat` into your shell so `emcc` and
`emcmake` are on `PATH`).

```powershell
emcmake cmake -S cpp -B cpp/build-wasm -G Ninja
cmake --build cpp/build-wasm
```

This produces `cpp/build-wasm/hack_olc.{html,js,wasm,data}`. There's no
separate WASM test target — the Catch2 suite only builds for native.

## Run (WASM)

Browsers refuse to load `.wasm` over `file://`, so it needs a real (even if
local) HTTP server:

```powershell
python -m http.server 8000 --directory cpp/build-wasm
```

Then open `http://localhost:8000/hack_olc.html`. There's no argv in a
browser, so it always runs the bundled `tetris.hackem` (preloaded into the
virtual filesystem at build time) — same controls as native. Expect it to
run visibly slower than native at the same emulated Hz, since Emscripten
interprets/JITs instead of running native x86; `DEFAULT_HZ` is set higher
for this build specifically to compensate (see `main.cpp`).

## Error Handling

Two distinct exception types are in play, and a caller wanting to catch
both needs two catch clauses: runtime CPU errors from `HackEngine::step`
(and anything that calls it, including `execute_instructions` and
`execute_count`), `get_ram`, and `set_ram` throw `hack::HackRuntimeError`;
`HackEngine::load_file` throws plain `std::runtime_error` on malformed ROM
data.

## Layout

- `src/hack_engine.hpp/.cpp` — CPU core: ALU, fetch/decode/execute,
  breakpoints/watchpoints, disassembler.
- `src/code_loader.cpp` — `.hackem`/`.hack` binary loader.
- `src/screen.hpp/.cpp` — memory-mapped 512x256 1bpp screen device.
- `src/keyboard.hpp/.cpp` — memory-mapped keyboard device.
- `src/main.cpp` — the `olc::PixelGameEngine`-derived application.
- `src/pge_impl.cpp` — the single translation unit compiling in
  olcPixelGameEngine's implementation (see comment in that file before
  adding `OLC_PGE3_APPLICATION` anywhere else).
- `third_party/olcPixelGameEngine3.h` — vendored single-header library.
- `tests/test_engine.cpp` — Catch2 test suite.

See `../docs/superpowers/specs/2026-09-05-cpp-port-design.md` for the full
design.
