# hack_olc C++ port — design

Date: 2026-09-05

## Purpose

Recode `hack_olc` (currently Rust + `olc_pixel_game_engine` binding) entirely
in C++, using olcPixelGameEngine natively as it was originally designed to be
used (a single-header C++ library). The existing Rust project is not being
replaced in the repo — it stays at the repo root, unmodified, as a working
reference — the C++ port lives alongside it in `cpp/`.

## Scope

Everything currently in this repo: the Hack CPU core, the `.hackem`/`.hack`
binary loader, the memory-mapped screen device, the memory-mapped keyboard
device, and the `olc::PixelGameEngine` application loop that ties them
together and throttles emulated speed to real wall-clock time.

Out of scope: `hackem` (the egui-based debugger) and `hack_cc` (the C
compiler) are referenced in source comments as sibling projects but are not
present in this repo and are not part of this port. The Rust project's
`#[ignore]`-marked diagnostic tests that depend on `hack_cc/demo/tetris.hackem`
are likewise out of scope (that file isn't available here).

## Repository layout

```
cpp/
  CMakeLists.txt
  third_party/
    olcPixelGameEngine.h       # vendored, official single-header release
  src/
    hack_engine.hpp
    hack_engine.cpp
    code_loader.cpp            # HackEngine::load_file, in its own TU
    screen.hpp
    screen.cpp
    keyboard.hpp
    keyboard.cpp
    main.cpp
  tests/
    CMakeLists.txt
    test_engine.cpp
  hello.hackem                 # copied here (and next to the built exe)
```

The Rust project (`Cargo.toml`, `src/*.rs`, root `hello.hackem`,
`tests/data/test2.hackem`) stays exactly where it is. `cpp/tests/` reads
`tests/data/test2.hackem` from the repo root via a relative path (mirroring
where the Rust tests already read it from).

## Build system

CMake, targeting MSVC via the Visual Studio 17 2022 generator (confirmed
installed on this machine, with the C++ desktop workload). C++20 standard.

olcPixelGameEngine is vendored directly as `cpp/third_party/olcPixelGameEngine.h`
(the official release from the OneLoneCoder repo) rather than fetched via
`FetchContent` or a package manager — this matches how virtually every
olcPixelGameEngine project is built, and needs no network access at
configure time.

Catch2 is pulled in for the test target via CMake `FetchContent` (pinned to a
specific release tag). `ctest` runs the suite (`ctest --test-dir cpp/build`).

Windows platform libs required by olcPixelGameEngine (`user32`, `gdi32`,
`opengl32`, `gdiplus`, `Shlwapi`, `dwmapi`) are linked in `cpp/CMakeLists.txt`.

## Components

### `HackEngine` (`hack_engine.hpp/.cpp`)

Direct port of `emulator::engine::HackEngine`:

- State: `uint16_t pc, a, d`; `std::array<uint16_t, 0x8000> ram, rom`;
  `uint16_t halt_addr`; `float speed`; `size_t rom_words_loaded,
  ram_words_loaded`; `uint64_t inst_count`; `std::map<uint16_t, BreakPoint>
  break_points`; `std::map<uint16_t, WatchPoint> watch_points`;
  `std::optional<uint16_t> triggered_watchpoint`; `std::vector<uint8_t>
  output_buffer`; a owned `HackScreen screen`; `uint16_t keyboard` (written
  by the caller once per frame, read-only from the CPU's perspective).
- `static uint16_t alu(uint16_t x, uint16_t y, uint16_t c)` — same six
  control-bit decomposition (`zx, nx, zy, ny, f, no`) and same truth table,
  using unsigned wraparound arithmetic (matches Rust's `wrapping_add`/`!`).
- `void sync_screen_from_ram()` — rebuild `screen` from `ram[0x4000..0x6000)`
  after a bulk load.
- `set_ram(uint16_t address, uint16_t value) -> bool` (`ui_stop` return),
  `get_ram(uint16_t address) -> uint16_t` — same address-range dispatch
  (`0x0000..0x3fff` plain RAM, `0x4000..0x5fff` also mirrored into
  `screen.write_word`, `0x6000` keyboard-read-only, `0x7fff` output-buffer
  append, `0x8000+` invalid), same watchpoint trigger check.
- `std::string take_output()` — drain `output_buffer` as UTF-8 (lossy is a
  Rust-ism; C++ can just construct the string directly since the buffer is
  raw bytes written by the emulated program).
- `step() -> std::pair<std::optional<StopReason>, bool>` — same fetch/decode
  (A-instruction vs. C-instruction), same `halt_addr + 1` Sys.halt detection,
  same dest/jump bit layout, same hard-loop detection
  (`pc > 2 && new_pc == pc - 2`), same breakpoint/watchpoint checks after the
  instruction commits.
- `execute_instructions(std::chrono::duration<float> run_time) -> StopReason`
  and `execute_count(uint32_t count) -> StopReason` — same two execution
  entry points (wall-clock-batched vs. fixed-count, no per-instruction clock
  read) as the Rust engine.
- Breakpoint/watchpoint management: `add_breakpoint`, `remove_breakpoint`,
  `remove_all_breakpoints`, `add_watchpoint`, `remove_watchpoint`,
  `remove_all_watchpoints`, `get_registers() -> std::tuple<uint16_t,
  uint16_t, uint16_t>`.
- `static std::string disassemble_one(uint16_t word)` and
  `disassemble_range(uint16_t start, uint16_t count) ->
  std::vector<std::tuple<uint16_t, uint16_t, std::string>>` — same mnemonic
  tables for both `a=0` (A register) and `a=1` (M) operand forms.

None of the breakpoint/watchpoint/disassembler API is called by `main.cpp`
today (same as the Rust version) — it's ported for parity, ready for a future
debugger UI.

### Errors

`HackRuntimeError : public std::runtime_error`, constructed from a reason
enum (`InvalidInstruction`, `InvalidReadAddress`, `InvalidWriteAddress`,
`InvalidPC`) plus the offending address where applicable, with a formatted
`what()` message matching the Rust `RuntimeError` variants' text. Thrown from
`step()`, `get_ram`, `set_ram`, and `load_file` on malformed input. `step()`
and `execute_count()`/`execute_instructions()` propagate the exception to the
caller; `main.cpp` catches it around the per-frame execute loop, sets
`halted = true`, and prints to stderr — mirroring the Rust
`Err(e) => { halted = true; eprintln!(...) }` arm.

### `load_file` (`code_loader.cpp`)

Member function `HackEngine::load_file(std::string_view bin)`. Same
two-format detection: if the text starts with `"hackem"`, parse the
`hackem v1.0 0xHHHH` header (exactly 3 whitespace-separated tokens, version
must be `v1.0`, halt address as `0x`-prefixed hex) followed by `RAM@HHHH` /
`ROM@HHHH` section headers and hex-word data lines (`//`-prefixed comments
and blank lines skipped, address auto-increments per word); otherwise, if
every non-blank/non-comment line is all `0`/`1` characters, parse as a raw
`.hack` ASCII-binary (one 16-bit binary word per line, loaded into ROM from
address 0); otherwise throw. Sets `rom_words_loaded`, `ram_words_loaded`,
resets `pc = 0`, calls `sync_screen_from_ram()` at the end — same as Rust.

### `HackScreen` (`screen.hpp/.cpp`)

Direct port of `screen::HackScreen`: `std::array<uint16_t, 0x2000> words`
(512×256 pixels, 1bpp, 16 pixels/word, LSB-first, mirrors RAM[0x4000..0x6000)
offset by 0x4000). `write_word`/`read_word`/`set_pixel`/`get_pixel`/`clear`
unchanged; `locate(x, y)` returns `std::optional<std::pair<size_t, uint16_t>>`
using the same bit-order (`1u16 << bit`, confirmed correct against both
hackem's own tests and hack_cc's reference emulator per the Rust comment —
carried forward verbatim, not re-derived). `draw()` calls `olc::Draw(x, y,
olc::BLACK)` per set bit, skipping all-zero words, matching the Rust
full-redraw-every-frame approach (no dirty-word tracking, same as today).

### `HackKeyboard` (`keyboard.hpp/.cpp`)

Direct port of `keyboard::HackKeyboard`: same three-tier `poll()` scan order
(26 letters → 10 digits with shift symbols → specials table), same Hack
keycodes (128 Enter, 129 Backspace, 130/131/132/133 Left/Up/Right/Down,
134-152 Home/End/PgUp/PgDn/Ins/Del/Esc/F1-F12, `.`), using
`olc::GetKey(olc::Key::...).bHeld` in place of `get_key(...).held`. Same
omission noted in the Rust source: no dedicated punctuation keys beyond
`.` since olc's `Key` enum doesn't expose them.

### `main.cpp` (`HackApp : public olc::PixelGameEngine`)

Direct port of `main.rs`'s `HackApp`/`olc::Application` impl:

- Same tuning constants: `DEFAULT_HZ = 2'000'000.0f`, `MAX_FRAME_TIME =
  0.05f`, `MAX_INSTRUCTIONS_PER_FRAME = 2'000'000u`, `REPEAT_INITIAL_DELAY =
  0.30f`, `REPEAT_INTERVAL = 0.08f`, `SUBSTEP_INSTRUCTIONS = 500u` — same
  values, same rationale (preserved as comments).
- Construction: first CLI arg (`argv[1]`) is an optional ROM path
  (`.hackem`/`.hack`); when absent, read `hello.hackem` from the directory
  containing the running executable (CMake copies it there post-build) and
  use that as the source text passed to `load_file`.
- `OnUserCreate()` — no-op, returns true.
- `OnUserUpdate(float elapsed_time)`:
  - Numpad +/- adjust `speed_hz` (double/halve), clamped to
    `[1.0, MAX_INSTRUCTIONS_PER_FRAME / MAX_FRAME_TIME]` — same as Rust.
  - `keyboard.poll()` then `read()` for `raw_key`.
  - Same pulse-key state machine (`last_physical_key`, `repeat_timer`) to
    turn a continuously-held key into a typematic-style pulse timed by real
    seconds, independent of `speed_hz`.
  - If not halted: compute this frame's instruction budget
    (`clamp(round(min(elapsed_time, MAX_FRAME_TIME) * speed_hz), 1,
    MAX_INSTRUCTIONS_PER_FRAME)`), execute in `SUBSTEP_INSTRUCTIONS`-sized
    chunks via `execute_count`, delivering `pulse_key` only on the first
    chunk of the frame (rest see `keyboard = 0`), catching
    `HackRuntimeError` to halt-and-log exactly like the Rust `Err(e)` arm,
    and stopping the frame's remaining budget on `SysHalt`/`HardLoop`.
  - `Clear(olc::WHITE)`, `engine.screen.draw()`, then a status line via
    `DrawString` at `(4, SCREEN_HEIGHT + 4)` showing PC/A/D in hex, raw/pulse
    key, current Hz, and running/HALTED — same format string as Rust.
  - Returns `true` (continue) always, mirroring `Ok(())`.
  - `OnUserDestroy()` — no-op, returns true.
- `main()`: parses `argv[1]` as an optional ROM path, constructs `HackApp`,
  calls `Construct(SCREEN_WIDTH, SCREEN_HEIGHT + 16, 2, 2)` then `Start()`.

## Testing

Catch2 (`FetchContent`, pinned tag), `cpp/tests/test_engine.cpp` and
`cpp/tests/CMakeLists.txt` wired into `ctest`. Ported test cases, matching
the Rust suite's coverage and assertions exactly:

- `alu` — exhaustive sweep (all 18 documented ALU control-bit patterns,
  both x-major and y-major loops as in the Rust version) for every x/y pair
  in the same ranges.
- `cpu` — hand-assembled ROM (conditional-branch smoke test), same
  expected `ram[17] == 2`, `ram[16] == 3`.
- `jumps_1_gt` / `jumps_1_eq` / `jumps_1_ge` / `jumps_1_ne` / `jumps_1_le` /
  `jumps_1_jmp` / `jumps_0_gt` / `jumps_0_eq` / `jumps_0_ge` / `jumps_1_lt` /
  `jumps_0_lt` / `jumps_0_ne` / `jumps_0_le` / `jumps_0_jmp` /
  `jumps_neg1_gt` / `jumps_neg1_eq` / `jumps_neg1_ge` / `jumps_neg1_lt` /
  `jumps_neg1_ne` / `jumps_neg1_le` / `jumps_neg1_jmp` — all 20 cases, same
  ROM words, same expected `ram[1]`.
- `c_program_factorial_fib` — loads `../../tests/data/test2.hackem`
  (relative to the test binary, pointing at the repo-root copy), runs to
  halt via repeated `execute_instructions(10s)` calls, asserts
  `StopReason::SysHalt` and `(int16_t)ram[256] == 133`.
- `hello_screen_output` — loads `../../hello.hackem` (repo-root copy), runs
  to halt, asserts at least one non-zero word in `ram[0x4000..0x6000)`.
- `hack_binary_screen_write` — loads the same 6-line raw `.hack` binary,
  asserts `rom_words_loaded == 6`, runs to `HardLoop`, checks
  `ram[0x4000] == 0xFFFF` and `ram[0x4020] == 0xFFFF`.
- `screen_tracks_ram_writes` — direct `set_ram` calls, checks
  `screen.get_pixel(...)` reflects them immediately, including the
  next-row/word-boundary case.

Not ported (out of scope, see above): the four Rust `#[ignore]` diagnostic
tests that dump PPMs or reproduce a tetris keyboard bug — all depend on
`hack_cc/demo/tetris.hackem`, unavailable in this repo.

## Out of scope / explicitly deferred

- No changes to the Rust project.
- No `hackem`/`hack_cc` integration.
- No packaging/installer; running the built exe from `cpp/build/...` (with
  `hello.hackem` copied alongside it) is sufficient.
