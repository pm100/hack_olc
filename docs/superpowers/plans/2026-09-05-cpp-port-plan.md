# hack_olc C++ Port Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Port the Hack CPU emulator in this repo to native C++ (olcPixelGameEngine, CMake/MSVC, Catch2), living alongside the untouched Rust project under `cpp/`.

**Architecture:** A `hack_engine` static library (CPU core, screen, keyboard, binary loader — no window dependency at test time) plus a `hack_olc` executable (`main.cpp`, the `olc::PixelGameEngine`-derived app) and a `hack_olc_tests` Catch2 executable, all built by one top-level CMake project at `cpp/`.

**Tech Stack:** C++20, CMake (Visual Studio 17 2022 generator, MSVC), vendored `olcPixelGameEngine.h`, Catch2 v3 (FetchContent).

**Spec:** `docs/superpowers/specs/2026-09-05-cpp-port-design.md`

## Global Constraints

- C++20 standard (`CMAKE_CXX_STANDARD 20`).
- Build via CMake, MSVC, Visual Studio 17 2022 generator (confirmed installed: `C:\Program Files\Microsoft Visual Studio\2022\Community`).
- `olcPixelGameEngine.h` is vendored at `cpp/third_party/olcPixelGameEngine.h` (not FetchContent, not a package manager).
- Catch2 is pulled via CMake `FetchContent`, pinned to tag `v3.5.4`.
- Errors use C++ exceptions: `hack::HackRuntimeError : public std::runtime_error`.
- The Rust project (`Cargo.toml`, root `src/*.rs`, root `hello.hackem`, `tests/data/test2.hackem`) is never modified.
- Out of scope: `hackem`, `hack_cc`, and the Rust suite's `#[ignore]`-marked tetris/PPM diagnostic tests (they depend on files not in this repo).
- **Single olcPixelGameEngine implementation TU rule:** exactly one translation unit in the whole build may `#define OLC_PGE_APPLICATION` before including `olcPixelGameEngine.h` — that is `cpp/src/pge_impl.cpp`, compiled once into the `hack_engine` static library. Every other file (`screen.cpp`, `keyboard.cpp`, `main.cpp`) includes `"olcPixelGameEngine.h"` for declarations only, with no `OLC_PGE_APPLICATION` define. This is required so `hack_olc_tests` — which never calls `Construct()`/`Start()` but does link object files that call `olc::PixelGameEngine` member functions (`Draw`, `GetKey`) — still resolves those symbols at link time, and so `hack_olc` doesn't get duplicate-symbol errors from defining the implementation twice.

---

### Task 1: CMake project skeleton, vendored PGE header, Catch2, build pipeline proof

**Files:**
- Create: `cpp/CMakeLists.txt`
- Create: `cpp/third_party/olcPixelGameEngine.h` (vendored, not authored)
- Create: `cpp/src/pge_impl.cpp`
- Create: `cpp/tests/CMakeLists.txt`
- Create: `cpp/tests/test_engine.cpp`

**Interfaces:**
- Consumes: nothing (first task).
- Produces: a `hack_engine` STATIC library target (source: `src/pge_impl.cpp` only, for now), linked with Windows platform libs; a `hack_olc_tests` executable target linked against `hack_engine` and `Catch2::Catch2WithMain`, registered with `ctest`. Later tasks add sources to `hack_engine` via `target_sources(hack_engine PRIVATE ...)` and append `TEST_CASE`s to `test_engine.cpp`.

- [ ] **Step 1: Vendor the olcPixelGameEngine header**

```bash
mkdir -p /c/work/hack_olc/cpp/third_party
curl -fsSL -o /c/work/hack_olc/cpp/third_party/olcPixelGameEngine.h \
  https://raw.githubusercontent.com/OneLoneCoder/olcPixelGameEngine/master/olcPixelGameEngine.h
```

If this machine has no outbound network access in the execution environment, `curl` will fail — in that case download the file manually from `https://github.com/OneLoneCoder/olcPixelGameEngine/blob/master/olcPixelGameEngine.h` (raw view) and save it to `cpp/third_party/olcPixelGameEngine.h` before continuing. Verify the file is present and non-trivial:

```bash
wc -l /c/work/hack_olc/cpp/third_party/olcPixelGameEngine.h
```

Expected: several thousand lines (the real header is large).

- [ ] **Step 2: Create `cpp/src/pge_impl.cpp`**

```cpp
// The single translation unit where olcPixelGameEngine's implementation is
// compiled in, per its single-header-library convention (see the "Single
// olcPixelGameEngine implementation TU rule" in the plan's Global
// Constraints). Every other file includes olcPixelGameEngine.h for
// declarations only — do not add OLC_PGE_APPLICATION anywhere else.
#define OLC_PGE_APPLICATION
#include "olcPixelGameEngine.h"
```

- [ ] **Step 3: Create `cpp/CMakeLists.txt`**

```cmake
cmake_minimum_required(VERSION 3.20)
project(hack_olc_cpp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(hack_engine STATIC
    src/pge_impl.cpp
)
target_include_directories(hack_engine PUBLIC src third_party)

if (WIN32)
    target_link_libraries(hack_engine PUBLIC user32 gdi32 opengl32 gdiplus Shlwapi dwmapi)
endif()

enable_testing()
add_subdirectory(tests)
```

- [ ] **Step 4: Create `cpp/tests/CMakeLists.txt`**

```cmake
include(FetchContent)
FetchContent_Declare(
    catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.5.4
)
FetchContent_MakeAvailable(catch2)
list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)

add_executable(hack_olc_tests test_engine.cpp)
target_link_libraries(hack_olc_tests PRIVATE hack_engine Catch2::Catch2WithMain)

include(CTest)
include(Catch)
catch_discover_tests(hack_olc_tests)
```

- [ ] **Step 5: Create `cpp/tests/test_engine.cpp`**

```cpp
#include <catch2/catch_test_macros.hpp>

TEST_CASE("build infrastructure works", "[smoke]") {
    REQUIRE(1 + 1 == 2);
}
```

- [ ] **Step 6: Configure and build**

```powershell
cmake -S cpp -B cpp/build -G "Visual Studio 17 2022" -A x64
cmake --build cpp/build --config Debug
```

Expected: configure succeeds (Catch2 fetched), build succeeds with no errors — this proves the vendored header compiles/links (via `pge_impl.cpp` in `hack_engine`) and Catch2 is wired up correctly.

- [ ] **Step 7: Run the test suite**

```powershell
ctest --test-dir cpp/build -C Debug --output-on-failure
```

Expected: 1 test, `build infrastructure works`, passes.

- [ ] **Step 8: Commit**

```bash
cd /c/work/hack_olc
git add cpp/
git commit -m "$(cat <<'EOF'
cpp: add CMake skeleton, vendored olcPixelGameEngine, Catch2 harness

Task 1 of the C++ port plan. No emulator logic yet — proves the
build/test pipeline (CMake + MSVC + vendored PGE header + Catch2 via
FetchContent) works end to end.

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01FJ7mbw7oTHRjamU6c2aEVQ
EOF
)"
git push
```

---

### Task 2: HackScreen

**Files:**
- Create: `cpp/src/screen.hpp`
- Create: `cpp/src/screen.cpp`
- Modify: `cpp/CMakeLists.txt` (add `src/screen.cpp` to `hack_engine`)
- Modify: `cpp/tests/test_engine.cpp` (append screen tests)

**Interfaces:**
- Consumes: nothing new (forward-declares `olc::PixelGameEngine`, doesn't need the full header in `screen.hpp`).
- Produces: `hack::SCREEN_WIDTH = 512`, `hack::SCREEN_HEIGHT = 256` (int32_t constants); `class hack::HackScreen` with `write_word(size_t, uint16_t)`, `read_word(size_t) const -> uint16_t`, `set_pixel(int32_t, int32_t, bool)`, `get_pixel(int32_t, int32_t) const -> bool`, `clear()`, `draw(olc::PixelGameEngine&) const`. `HackEngine` (Task 4) will own a `HackScreen screen` member and use these exact names.

- [ ] **Step 1: Write `cpp/src/screen.hpp`**

```cpp
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>

namespace olc { class PixelGameEngine; }

namespace hack {

constexpr int32_t SCREEN_WIDTH = 512;
constexpr int32_t SCREEN_HEIGHT = 256;

/// Memory-mapped Hack screen device: 512x256 pixels, 1 bit per pixel,
/// packed 16 pixels per 16-bit word, LSB first. On the real machine this
/// lives at RAM[0x4000..0x6000); `offset` here is address minus 0x4000, so
/// HackEngine can drive it with the exact same word offsets it already uses.
class HackScreen {
public:
    HackScreen() = default;

    /// Write a 16-bit word at screen-relative offset (0..0x2000), as the
    /// CPU would when it writes RAM[0x4000 + offset].
    void write_word(std::size_t offset, uint16_t value);
    uint16_t read_word(std::size_t offset) const;

    /// Set/clear a single pixel via read-modify-write of its containing
    /// word — the same operation a Hack program performs with `M=D`.
    void set_pixel(int32_t x, int32_t y, bool on);
    bool get_pixel(int32_t x, int32_t y) const;

    void clear();

    /// Full redraw from the word buffer, skipping all-zero words.
    void draw(olc::PixelGameEngine& pge) const;

private:
    static constexpr int WORD_PIXELS = 16;
    static constexpr std::size_t WORDS_PER_ROW =
        static_cast<std::size_t>(SCREEN_WIDTH) / WORD_PIXELS;
    static constexpr std::size_t WORD_COUNT =
        (static_cast<std::size_t>(SCREEN_WIDTH) * static_cast<std::size_t>(SCREEN_HEIGHT)) / WORD_PIXELS;

    static std::optional<std::pair<std::size_t, uint16_t>> locate(int32_t x, int32_t y);

    std::array<uint16_t, WORD_COUNT> words_{};
};

} // namespace hack
```

- [ ] **Step 2: Write `cpp/src/screen.cpp`**

```cpp
#include "screen.hpp"

#include "olcPixelGameEngine.h"

namespace hack {

void HackScreen::write_word(std::size_t offset, uint16_t value) {
    words_[offset] = value;
}

uint16_t HackScreen::read_word(std::size_t offset) const {
    return words_[offset];
}

void HackScreen::set_pixel(int32_t x, int32_t y, bool on) {
    auto loc = locate(x, y);
    if (!loc) return;
    auto [offset, mask] = *loc;
    if (on) {
        words_[offset] |= mask;
    } else {
        words_[offset] &= static_cast<uint16_t>(~mask);
    }
}

bool HackScreen::get_pixel(int32_t x, int32_t y) const {
    auto loc = locate(x, y);
    if (!loc) return false;
    auto [offset, mask] = *loc;
    return (words_[offset] & mask) != 0;
}

std::optional<std::pair<std::size_t, uint16_t>> HackScreen::locate(int32_t x, int32_t y) {
    if (x < 0 || y < 0 || x >= SCREEN_WIDTH || y >= SCREEN_HEIGHT) {
        return std::nullopt;
    }
    std::size_t word_col = static_cast<std::size_t>(x) / WORD_PIXELS;
    std::size_t bit = static_cast<std::size_t>(x) % WORD_PIXELS;
    std::size_t offset = static_cast<std::size_t>(y) * WORDS_PER_ROW + word_col;
    // Bit i is the i-th pixel from the left of the word. Confirmed against
    // both hackem's own test and hack_cc's reference emulator in the
    // original Rust port — naive bit==col is correct.
    return std::make_pair(offset, static_cast<uint16_t>(1u << bit));
}

void HackScreen::clear() {
    words_.fill(0);
}

void HackScreen::draw(olc::PixelGameEngine& pge) const {
    for (int32_t y = 0; y < SCREEN_HEIGHT; ++y) {
        for (std::size_t word_col = 0; word_col < WORDS_PER_ROW; ++word_col) {
            uint16_t word = words_[static_cast<std::size_t>(y) * WORDS_PER_ROW + word_col];
            if (word == 0) continue;
            int32_t base_x = static_cast<int32_t>(word_col * WORD_PIXELS);
            for (int bit = 0; bit < WORD_PIXELS; ++bit) {
                if (word & (1u << bit)) {
                    pge.Draw(base_x + bit, y, olc::BLACK);
                }
            }
        }
    }
}

} // namespace hack
```

- [ ] **Step 3: Add `screen.cpp` to the library in `cpp/CMakeLists.txt`**

Change:
```cmake
add_library(hack_engine STATIC
    src/pge_impl.cpp
)
```
to:
```cmake
add_library(hack_engine STATIC
    src/pge_impl.cpp
    src/screen.cpp
)
```

- [ ] **Step 4: Append screen tests to `cpp/tests/test_engine.cpp`**

```cpp
#include "screen.hpp"

TEST_CASE("screen pixels round-trip through set/get", "[screen]") {
    hack::HackScreen screen;
    REQUIRE_FALSE(screen.get_pixel(0, 0));

    screen.set_pixel(0, 0, true);
    screen.set_pixel(1, 0, true);
    REQUIRE(screen.get_pixel(0, 0));
    REQUIRE(screen.get_pixel(1, 0));
    REQUIRE_FALSE(screen.get_pixel(2, 0));

    screen.set_pixel(0, 0, false);
    REQUIRE_FALSE(screen.get_pixel(0, 0));
}

TEST_CASE("screen out-of-bounds pixels are always off and ignored on write", "[screen]") {
    hack::HackScreen screen;
    REQUIRE_FALSE(screen.get_pixel(-1, 0));
    REQUIRE_FALSE(screen.get_pixel(0, -1));
    REQUIRE_FALSE(screen.get_pixel(hack::SCREEN_WIDTH, 0));
    REQUIRE_FALSE(screen.get_pixel(0, hack::SCREEN_HEIGHT));

    screen.set_pixel(hack::SCREEN_WIDTH, 0, true); // must not crash or affect in-bounds state
    REQUIRE_FALSE(screen.get_pixel(0, 0));
}

TEST_CASE("write_word/read_word round-trip", "[screen]") {
    hack::HackScreen screen;
    screen.write_word(5, 0xBEEF);
    REQUIRE(screen.read_word(5) == 0xBEEF);
}

TEST_CASE("write_word sets the pixels get_pixel reports", "[screen]") {
    hack::HackScreen screen;
    screen.write_word(0, 0x0003); // bits 0 and 1 set
    REQUIRE(screen.get_pixel(0, 0));
    REQUIRE(screen.get_pixel(1, 0));
    REQUIRE_FALSE(screen.get_pixel(2, 0));

    constexpr int screen_words_per_row = hack::SCREEN_WIDTH / 16;
    screen.write_word(screen_words_per_row, 0x8000); // row 1, bit 15
    REQUIRE(screen.get_pixel(15, 1));
}

TEST_CASE("clear resets every pixel", "[screen]") {
    hack::HackScreen screen;
    screen.set_pixel(10, 10, true);
    screen.clear();
    REQUIRE_FALSE(screen.get_pixel(10, 10));
}
```

- [ ] **Step 5: Build and run tests**

```powershell
cmake --build cpp/build --config Debug
ctest --test-dir cpp/build -C Debug --output-on-failure
```

Expected: all tests pass, including the 5 new `[screen]` cases.

- [ ] **Step 6: Commit**

```bash
cd /c/work/hack_olc
git add cpp/
git commit -m "$(cat <<'EOF'
cpp: port HackScreen

Direct port of the Rust screen::HackScreen. draw() takes an
olc::PixelGameEngine& (real olcPixelGameEngine has no free-function
Draw() — that's a Rust-binding-only shape) rather than the free
function the design spec assumed.

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01FJ7mbw7oTHRjamU6c2aEVQ
EOF
)"
git push
```

---

### Task 3: HackKeyboard

**Files:**
- Create: `cpp/src/keyboard.hpp`
- Create: `cpp/src/keyboard.cpp`
- Modify: `cpp/CMakeLists.txt` (add `src/keyboard.cpp`)
- Modify: `cpp/tests/test_engine.cpp` (append keyboard test)

**Interfaces:**
- Consumes: nothing new.
- Produces: `hack::KEY_ENTER/KEY_BACKSPACE/KEY_LEFT/KEY_UP/KEY_RIGHT/KEY_DOWN` (uint16_t constants); `class hack::HackKeyboard` with `read() const -> uint16_t` (inline) and `poll(olc::PixelGameEngine&)`. `main.cpp` (Task 7) calls both.

- [ ] **Step 1: Write `cpp/src/keyboard.hpp`**

```cpp
#pragma once

#include <cstdint>

namespace olc { class PixelGameEngine; }

namespace hack {

// Mirrors RAM[0x6000] on the real machine: the code of whichever key is
// currently held down, 0 when none is. Printable keys report their ASCII
// code; special keys use these Hack keyboard-spec codes.
constexpr uint16_t KEY_ENTER = 128;
constexpr uint16_t KEY_BACKSPACE = 129;
constexpr uint16_t KEY_LEFT = 130;
constexpr uint16_t KEY_UP = 131;
constexpr uint16_t KEY_RIGHT = 132;
constexpr uint16_t KEY_DOWN = 133;

class HackKeyboard {
public:
    HackKeyboard() = default;

    uint16_t read() const { return current_; }

    /// Call once per frame to resync the register from the live key state.
    void poll(olc::PixelGameEngine& pge);

private:
    uint16_t current_ = 0;
};

} // namespace hack
```

`read()` and the default constructor are inline in the header on purpose:
`hack_olc_tests` calls only these, so it never needs to link the object file
containing `poll()` (which references real `olc::PixelGameEngine` member
functions) — see Step 3's test.

- [ ] **Step 2: Write `cpp/src/keyboard.cpp`**

```cpp
#include "keyboard.hpp"

#include <cctype>

#include "olcPixelGameEngine.h"

namespace hack {
namespace {

struct LetterKey {
    olc::Key key;
    char lower;
};

constexpr LetterKey LETTERS[] = {
    {olc::Key::A, 'a'}, {olc::Key::B, 'b'}, {olc::Key::C, 'c'}, {olc::Key::D, 'd'},
    {olc::Key::E, 'e'}, {olc::Key::F, 'f'}, {olc::Key::G, 'g'}, {olc::Key::H, 'h'},
    {olc::Key::I, 'i'}, {olc::Key::J, 'j'}, {olc::Key::K, 'k'}, {olc::Key::L, 'l'},
    {olc::Key::M, 'm'}, {olc::Key::N, 'n'}, {olc::Key::O, 'o'}, {olc::Key::P, 'p'},
    {olc::Key::Q, 'q'}, {olc::Key::R, 'r'}, {olc::Key::S, 's'}, {olc::Key::T, 't'},
    {olc::Key::U, 'u'}, {olc::Key::V, 'v'}, {olc::Key::W, 'w'}, {olc::Key::X, 'x'},
    {olc::Key::Y, 'y'}, {olc::Key::Z, 'z'},
};

struct DigitKey {
    olc::Key key;
    char digit;
    char symbol;
};

constexpr DigitKey DIGITS[] = {
    {olc::Key::K0, '0', ')'}, {olc::Key::K1, '1', '!'}, {olc::Key::K2, '2', '@'},
    {olc::Key::K3, '3', '#'}, {olc::Key::K4, '4', '$'}, {olc::Key::K5, '5', '%'},
    {olc::Key::K6, '6', '^'}, {olc::Key::K7, '7', '&'}, {olc::Key::K8, '8', '*'},
    {olc::Key::K9, '9', '('},
};

struct SpecialKey {
    olc::Key key;
    uint16_t code;
};

const SpecialKey SPECIALS[] = {
    {olc::Key::SPACE, static_cast<uint16_t>(' ')},
    {olc::Key::RETURN, KEY_ENTER},
    {olc::Key::ENTER, KEY_ENTER},
    {olc::Key::BACK, KEY_BACKSPACE},
    {olc::Key::LEFT, KEY_LEFT},
    {olc::Key::UP, KEY_UP},
    {olc::Key::RIGHT, KEY_RIGHT},
    {olc::Key::DOWN, KEY_DOWN},
    {olc::Key::HOME, 134},
    {olc::Key::END, 135},
    {olc::Key::PGUP, 136},
    {olc::Key::PGDN, 137},
    {olc::Key::INS, 138},
    {olc::Key::DEL, 139},
    {olc::Key::ESCAPE, 140},
    {olc::Key::F1, 141},
    {olc::Key::F2, 142},
    {olc::Key::F3, 143},
    {olc::Key::F4, 144},
    {olc::Key::F5, 145},
    {olc::Key::F6, 146},
    {olc::Key::F7, 147},
    {olc::Key::F8, 148},
    {olc::Key::F9, 149},
    {olc::Key::F10, 150},
    {olc::Key::F11, 151},
    {olc::Key::F12, 152},
    {olc::Key::PERIOD, static_cast<uint16_t>('.')},
};

} // namespace

void HackKeyboard::poll(olc::PixelGameEngine& pge) {
    bool shift = pge.GetKey(olc::Key::SHIFT).bHeld;

    for (const auto& lk : LETTERS) {
        if (pge.GetKey(lk.key).bHeld) {
            current_ = shift
                ? static_cast<uint16_t>(std::toupper(static_cast<unsigned char>(lk.lower)))
                : static_cast<uint16_t>(lk.lower);
            return;
        }
    }

    for (const auto& dk : DIGITS) {
        if (pge.GetKey(dk.key).bHeld) {
            current_ = shift ? static_cast<uint16_t>(dk.symbol) : static_cast<uint16_t>(dk.digit);
            return;
        }
    }

    for (const auto& sk : SPECIALS) {
        if (pge.GetKey(sk.key).bHeld) {
            current_ = sk.code;
            return;
        }
    }

    current_ = 0;
}

} // namespace hack
```

- [ ] **Step 3: Add `keyboard.cpp` to the library in `cpp/CMakeLists.txt`**

```cmake
add_library(hack_engine STATIC
    src/pge_impl.cpp
    src/screen.cpp
    src/keyboard.cpp
)
```

- [ ] **Step 4: Append keyboard test to `cpp/tests/test_engine.cpp`**

```cpp
#include "keyboard.hpp"

TEST_CASE("keyboard starts with no key held", "[keyboard]") {
    hack::HackKeyboard kb;
    REQUIRE(kb.read() == 0);
}
```

- [ ] **Step 5: Build and run tests**

```powershell
cmake --build cpp/build --config Debug
ctest --test-dir cpp/build -C Debug --output-on-failure
```

Expected: all tests pass, including the new `[keyboard]` case. This also
confirms the "single implementation TU" rule works: `keyboard.o`'s `poll()`
references `olc::PixelGameEngine::GetKey`, but since the test never calls
`poll()`, the linker never needs that object file, so no unresolved-symbol
error occurs even though nothing in `hack_olc_tests` defines
`OLC_PGE_APPLICATION`.

- [ ] **Step 6: Commit**

```bash
cd /c/work/hack_olc
git add cpp/
git commit -m "$(cat <<'EOF'
cpp: port HackKeyboard

Direct port of the Rust keyboard::HackKeyboard. poll() takes an
olc::PixelGameEngine& and calls GetKey() on it as a member function,
matching real olcPixelGameEngine's API shape.

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01FJ7mbw7oTHRjamU6c2aEVQ
EOF
)"
git push
```

---

### Task 4: HackEngine core (state, ALU, RAM/step/execute, breakpoints/watchpoints)

**Files:**
- Create: `cpp/src/hack_engine.hpp`
- Create: `cpp/src/hack_engine.cpp`
- Modify: `cpp/CMakeLists.txt` (add `src/hack_engine.cpp`)
- Modify: `cpp/tests/test_engine.cpp` (append engine tests)

**Interfaces:**
- Consumes: `hack::HackScreen` (Task 2, `screen.hpp`).
- Produces: `enum class hack::RuntimeErrorReason`; `class hack::HackRuntimeError : public std::runtime_error`; `struct hack::BreakPoint { bool enabled; }`; `struct hack::WatchPoint { bool read, write, enabled; }`; `enum class hack::StopReason { RefreshUI, SysHalt, HardLoop, BreakPoint, WatchPoint }`; `class hack::HackEngine` with public fields `pc, a, d` (`uint16_t`), `ram, rom` (`std::array<uint16_t, 0x8000>`), `halt_addr` (`uint16_t`), `speed` (`float`), `rom_words_loaded, ram_words_loaded` (`size_t`), `break_points` (`std::map<uint16_t, BreakPoint>`), `watch_points` (`std::map<uint16_t, WatchPoint>`), `triggered_watchpoint` (`std::optional<uint16_t>`), `screen` (`HackScreen`), `keyboard` (`uint16_t`); and methods `sync_screen_from_ram()`, `set_ram(uint16_t, uint16_t) -> bool`, `get_ram(uint16_t) -> uint16_t`, `take_output() -> std::string`, `execute_instructions(std::chrono::duration<float>) -> StopReason`, `execute_count(uint32_t) -> StopReason`, `get_registers() const -> std::tuple<uint16_t, uint16_t, uint16_t>`, `add_breakpoint/remove_breakpoint/remove_all_breakpoints`, `add_watchpoint/remove_watchpoint/remove_all_watchpoints`, static `alu(uint16_t, uint16_t, uint16_t) -> uint16_t` (public so tests can call it directly). `load_file` and the disassembler are declared here but implemented in later tasks (Task 5, Task 6).

- [ ] **Step 1: Write `cpp/src/hack_engine.hpp`**

```cpp
#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

#include "screen.hpp"

namespace hack {

enum class RuntimeErrorReason {
    InvalidInstruction,
    InvalidReadAddress,
    InvalidWriteAddress,
    InvalidPC,
};

class HackRuntimeError : public std::runtime_error {
public:
    HackRuntimeError(RuntimeErrorReason reason, uint16_t address);
    explicit HackRuntimeError(RuntimeErrorReason reason);

    RuntimeErrorReason reason() const { return reason_; }

private:
    RuntimeErrorReason reason_;
};

struct BreakPoint {
    bool enabled = true;
};

struct WatchPoint {
    bool read = false;
    bool write = false;
    bool enabled = true;
};

enum class StopReason {
    RefreshUI,
    SysHalt,
    HardLoop,
    BreakPoint,
    WatchPoint,
};

class HackEngine {
public:
    HackEngine() = default;

    uint16_t pc = 0;
    uint16_t a = 0;
    uint16_t d = 0;
    std::array<uint16_t, 0x8000> ram{};
    std::array<uint16_t, 0x8000> rom{};
    uint16_t halt_addr = 0;
    float speed = 0.0f;
    size_t rom_words_loaded = 0;
    size_t ram_words_loaded = 0;

    std::map<uint16_t, BreakPoint> break_points;
    std::map<uint16_t, WatchPoint> watch_points;
    std::optional<uint16_t> triggered_watchpoint;

    HackScreen screen;
    uint16_t keyboard = 0;

    // Implemented in code_loader.cpp (Task 6).
    void load_file(const std::string& bin);

    /// Rebuild `screen` from the raw RAM contents. Needed after bulk RAM
    /// writes (e.g. load_file) that bypass set_ram.
    void sync_screen_from_ram();

    bool set_ram(uint16_t address, uint16_t value); // returns ui_stop
    uint16_t get_ram(uint16_t address);

    /// Drain all bytes written to the output port (RAM[0x7FFF]) since the
    /// last call.
    std::string take_output();

    StopReason execute_instructions(std::chrono::duration<float> run_time);

    /// Execute up to `count` instructions with no wall-clock check at all —
    /// for callers implementing their own instructions-per-second throttle.
    StopReason execute_count(uint32_t count);

    std::tuple<uint16_t, uint16_t, uint16_t> get_registers() const;

    void add_breakpoint(uint16_t address);
    void remove_breakpoint(uint16_t address);
    void remove_all_breakpoints();

    void add_watchpoint(uint16_t address, bool read, bool write);
    void remove_watchpoint(uint16_t address);
    void remove_all_watchpoints();

    // Implemented in hack_engine.cpp (disassembler part, Task 5).
    static std::string disassemble_one(uint16_t word);
    std::vector<std::tuple<uint16_t, uint16_t, std::string>> disassemble_range(
        uint16_t start, uint16_t count) const;

    /// Public (unlike the Rust original's module-private fn) so tests in a
    /// separate translation unit can exercise it directly.
    static uint16_t alu(uint16_t x_in, uint16_t y_in, uint16_t c);

private:
    /// Execute a single instruction. Returns the reason execution should
    /// stop (halt, hard loop, breakpoint, watchpoint), if any, plus whether
    /// this instruction wrote to the screen (ui_stop).
    std::pair<std::optional<StopReason>, bool> step();

    uint64_t inst_count_ = 0;
    std::vector<uint8_t> output_buffer_;
};

} // namespace hack
```

- [ ] **Step 2: Write `cpp/src/hack_engine.cpp`**

```cpp
#include "hack_engine.hpp"

#include <string>

namespace hack {
namespace {

std::string format_reason(RuntimeErrorReason reason, std::optional<uint16_t> address) {
    switch (reason) {
        case RuntimeErrorReason::InvalidInstruction:
            return "Invalid instruction";
        case RuntimeErrorReason::InvalidReadAddress:
            return "Invalid RAM read address " + std::to_string(*address);
        case RuntimeErrorReason::InvalidWriteAddress:
            return "Invalid RAM write address " + std::to_string(*address);
        case RuntimeErrorReason::InvalidPC:
            return "Invalid instruction address " + std::to_string(*address);
    }
    return "Unknown error";
}

} // namespace

HackRuntimeError::HackRuntimeError(RuntimeErrorReason reason, uint16_t address)
    : std::runtime_error(format_reason(reason, address)), reason_(reason) {}

HackRuntimeError::HackRuntimeError(RuntimeErrorReason reason)
    : std::runtime_error(format_reason(reason, std::nullopt)), reason_(reason) {}

uint16_t HackEngine::alu(uint16_t x_in, uint16_t y_in, uint16_t c) {
    uint16_t zx = (c >> 5) & 0x1;
    uint16_t nx = (c >> 4) & 0x1;
    uint16_t zy = (c >> 3) & 0x1;
    uint16_t ny = (c >> 2) & 0x1;
    uint16_t f = (c >> 1) & 0x1;
    uint16_t no = c & 0x1;

    uint16_t x = zx ? 0 : x_in;
    x = nx ? static_cast<uint16_t>(~x) : x;

    uint16_t y = zy ? 0 : y_in;
    y = ny ? static_cast<uint16_t>(~y) : y;

    if (f) {
        uint16_t sum = static_cast<uint16_t>(x + y);
        return no ? static_cast<uint16_t>(~sum) : sum;
    }
    uint16_t bit_and = static_cast<uint16_t>(x & y);
    return no ? static_cast<uint16_t>(~bit_and) : bit_and;
}

void HackEngine::sync_screen_from_ram() {
    constexpr size_t SCREEN_WORDS = 0x2000;
    for (size_t offset = 0; offset < SCREEN_WORDS; ++offset) {
        screen.write_word(offset, ram[0x4000 + offset]);
    }
}

bool HackEngine::set_ram(uint16_t address, uint16_t value) {
    if (address >= 0x8000) {
        throw HackRuntimeError(RuntimeErrorReason::InvalidWriteAddress, address);
    }

    bool ui_stop = false;
    if (address <= 0x3fff) {
        ram[address] = value;
    } else if (address <= 0x5fff) {
        size_t offset = static_cast<size_t>(address) - 0x4000;
        ram[address] = value;
        screen.write_word(offset, value);
    } else if (address == 0x6000) {
        // keyboard - write ignored
    } else if (address == 0x7fff) {
        // output port: buffer the byte, never interrupt execution
        output_buffer_.push_back(static_cast<uint8_t>(value));
    } else {
        ram[address] = value;
    }

    if (auto wp = watch_points.find(address); wp != watch_points.end()) {
        if (wp->second.write && wp->second.enabled) {
            triggered_watchpoint = address;
        }
    }
    return ui_stop;
}

uint16_t HackEngine::get_ram(uint16_t address) {
    if (address >= 0x8000) {
        throw HackRuntimeError(RuntimeErrorReason::InvalidReadAddress, address);
    }
    if (address == 0x6000) {
        return keyboard;
    }
    if (auto wp = watch_points.find(address); wp != watch_points.end()) {
        if (wp->second.read && wp->second.enabled) {
            triggered_watchpoint = address;
        }
    }
    return ram[address];
}

std::string HackEngine::take_output() {
    std::string s(output_buffer_.begin(), output_buffer_.end());
    output_buffer_.clear();
    return s;
}

std::pair<std::optional<StopReason>, bool> HackEngine::step() {
    if (pc >= 0x8000) {
        throw HackRuntimeError(RuntimeErrorReason::InvalidPC, pc);
    }
    inst_count_++;

    uint16_t instruction = rom[pc];

    // did we hit a call to Sys.halt?
    if (halt_addr != 0 && pc == static_cast<uint16_t>(halt_addr + 1)) {
        return {StopReason::SysHalt, false};
    }

    uint16_t opcode = instruction >> 15;
    uint16_t old_pc = pc;
    bool ui_stop = false;
    pc = static_cast<uint16_t>(pc + 1);

    if (opcode == 0) {
        // A instruction
        a = instruction;
    } else {
        // C instruction
        uint16_t a_bit = (instruction >> 12) & 0x1;
        uint16_t c = (instruction >> 6) & 0x3F;
        uint16_t dest = (instruction >> 3) & 0x7;
        uint16_t j = instruction & 0x7;

        // cannot use A as a jump address and a ram read/write address in
        // the same instruction
        if (j != 0 && (dest & 0x1) != 0) {
            throw HackRuntimeError(RuntimeErrorReason::InvalidInstruction);
        }

        uint16_t y = (a_bit == 0) ? a : get_ram(a);
        uint16_t alu_out = alu(d, y, c);

        // M
        if (dest & 0x1) {
            ui_stop = set_ram(a, alu_out);
        }
        // D
        if (dest & 0x2) {
            d = alu_out;
        }
        // A (update is deferred til after jmp is tested)
        uint16_t new_a = (dest & 0x4) ? alu_out : a;

        uint16_t saved_pc = pc;

        if ((j & 0x1) && static_cast<int16_t>(alu_out) > 0) {
            pc = a;
        }
        if ((j & 0x2) && alu_out == 0) {
            pc = a;
        }
        if ((j & 0x4) && static_cast<int16_t>(alu_out) < 0) {
            pc = a;
        }

        a = new_a;

        // detects halt loop: (halt) @halt 0;JMP
        if (saved_pc > 2 && pc == static_cast<uint16_t>(saved_pc - 2)) {
            return {StopReason::HardLoop, ui_stop};
        }
    }

    if (auto bp = break_points.find(old_pc); bp != break_points.end()) {
        if (bp->second.enabled) {
            return {StopReason::BreakPoint, ui_stop};
        }
    }
    if (triggered_watchpoint.has_value()) {
        triggered_watchpoint.reset();
        return {StopReason::WatchPoint, ui_stop};
    }
    return {std::nullopt, ui_stop};
}

StopReason HackEngine::execute_instructions(std::chrono::duration<float> run_time) {
    using clock = std::chrono::steady_clock;
    auto start_time = clock::now();
    speed = 0.0f;
    int counter = 0;
    uint64_t inst_count_snap = inst_count_;

    while (true) {
        counter++;
        // every chunk of instructions check to see if we should refresh the
        // UI by returning to the caller
        if (counter > 1000) {
            auto elapsed = std::chrono::duration<float>(clock::now() - start_time);
            if (elapsed > run_time) {
                speed = static_cast<float>(inst_count_ - inst_count_snap) / elapsed.count() / 1'000'000.0f;
                return StopReason::RefreshUI;
            }
            counter = 0;
        }

        auto [stop, ui_stop] = step();
        if (stop.has_value()) {
            return *stop;
        }
        if (run_time.count() == 0.0f || ui_stop) {
            return StopReason::RefreshUI;
        }
    }
}

StopReason HackEngine::execute_count(uint32_t count) {
    for (uint32_t i = 0; i < count; ++i) {
        auto [stop, ui_stop] = step();
        (void)ui_stop;
        if (stop.has_value()) {
            return *stop;
        }
    }
    return StopReason::RefreshUI;
}

std::tuple<uint16_t, uint16_t, uint16_t> HackEngine::get_registers() const {
    return {pc, a, d};
}

void HackEngine::add_breakpoint(uint16_t address) {
    break_points[address] = BreakPoint{true};
}

void HackEngine::remove_breakpoint(uint16_t address) {
    break_points.erase(address);
}

void HackEngine::remove_all_breakpoints() {
    break_points.clear();
}

void HackEngine::add_watchpoint(uint16_t address, bool read, bool write) {
    watch_points[address] = WatchPoint{read, write, true};
}

void HackEngine::remove_watchpoint(uint16_t address) {
    watch_points.erase(address);
}

void HackEngine::remove_all_watchpoints() {
    watch_points.clear();
}

} // namespace hack
```

- [ ] **Step 3: Add `hack_engine.cpp` to the library in `cpp/CMakeLists.txt`**

```cmake
add_library(hack_engine STATIC
    src/pge_impl.cpp
    src/screen.cpp
    src/keyboard.cpp
    src/hack_engine.cpp
)
```

- [ ] **Step 4: Append engine tests to `cpp/tests/test_engine.cpp`**

```cpp
#include "hack_engine.hpp"

namespace {

void test_alu_core(uint16_t x, uint16_t y) {
    using hack::HackEngine;
    REQUIRE(HackEngine::alu(x, y, 0x2a) == 0);
    REQUIRE(HackEngine::alu(x, y, 0x3f) == 1);
    REQUIRE(HackEngine::alu(x, y, 0x3a) == 0xffff);
    REQUIRE(HackEngine::alu(x, y, 0x0c) == x);
    REQUIRE(HackEngine::alu(x, y, 0x30) == y);
    REQUIRE(HackEngine::alu(x, y, 0x0d) == static_cast<uint16_t>(~x));
    REQUIRE(HackEngine::alu(x, y, 0x31) == static_cast<uint16_t>(~y));
    REQUIRE(HackEngine::alu(x, y, 0x0f) == static_cast<uint16_t>(0 - x));
    REQUIRE(HackEngine::alu(x, y, 0x33) == static_cast<uint16_t>(0 - y));
    REQUIRE(HackEngine::alu(x, y, 0x1f) == static_cast<uint16_t>(x + 1));
    REQUIRE(HackEngine::alu(x, y, 0x37) == static_cast<uint16_t>(y + 1));
    REQUIRE(HackEngine::alu(x, y, 0x0e) == static_cast<uint16_t>(x - 1));
    REQUIRE(HackEngine::alu(x, y, 0x32) == static_cast<uint16_t>(y - 1));
    REQUIRE(HackEngine::alu(x, y, 0x02) == static_cast<uint16_t>(x + y));
    REQUIRE(HackEngine::alu(x, y, 0x13) == static_cast<uint16_t>(x - y));
    REQUIRE(HackEngine::alu(x, y, 0x07) == static_cast<uint16_t>(y - x));
    REQUIRE(HackEngine::alu(x, y, 0x00) == static_cast<uint16_t>(x & y));
    REQUIRE(HackEngine::alu(x, y, 0x15) == static_cast<uint16_t>(x | y));
}

hack::StopReason run_to_hard_loop(hack::HackEngine& cpu) {
    using namespace std::chrono_literals;
    while (true) {
        auto reason = cpu.execute_instructions(0s);
        if (reason == hack::StopReason::HardLoop) return reason;
    }
}

} // namespace

TEST_CASE("alu matches the documented truth table exhaustively", "[engine][alu]") {
    for (uint32_t x = 0; x <= 0xffff; ++x) {
        for (uint32_t y = 0; y <= 0xff; ++y) {
            test_alu_core(static_cast<uint16_t>(x), static_cast<uint16_t>(y));
        }
    }
    for (uint32_t y = 0; y <= 0xffff; ++y) {
        for (uint32_t x = 0; x <= 0xff; ++x) {
            test_alu_core(static_cast<uint16_t>(x), static_cast<uint16_t>(y));
        }
    }
}

TEST_CASE("a small hand-assembled conditional program runs to completion", "[engine]") {
    hack::HackEngine cpu;
    // @2 / D=A / @17 / M=D / @10 / D;JLT / @3 / D=A / @16 / M=D / (POP) (HALT) / @HALT / D;JMP
    cpu.rom[0] = 0x0002;
    cpu.rom[1] = 0x8c10;
    cpu.rom[2] = 0x0011;
    cpu.rom[3] = 0x8308;
    cpu.rom[4] = 0x000a;
    cpu.rom[5] = 0x8304;
    cpu.rom[6] = 0x0003;
    cpu.rom[7] = 0x8c10;
    cpu.rom[8] = 0x0010;
    cpu.rom[9] = 0x8308;
    cpu.rom[10] = 0x000a;
    cpu.rom[11] = 0x8307;

    run_to_hard_loop(cpu);

    REQUIRE(cpu.ram[17] == 2);
    REQUIRE(cpu.ram[16] == 3);
}

namespace {

/// Builds a `D=<value>; @L1; D;<jump>; @1; M=1; (L1) @L1; D;JMP` program
/// (mirroring the Rust jump tests exactly) and returns ram[1]: 1 means the
/// branch was NOT taken, 0 means it was.
uint16_t run_jump_test(uint16_t d_setup, uint16_t jump_word) {
    hack::HackEngine cpu;
    cpu.rom[0] = d_setup;
    cpu.rom[1] = 0x0005;
    cpu.rom[2] = jump_word;
    cpu.rom[3] = 0x0001;
    cpu.rom[4] = 0xefc8;
    cpu.rom[5] = 0x0005;
    cpu.rom[6] = 0xe307;
    run_to_hard_loop(cpu);
    return cpu.ram[1];
}

} // namespace

TEST_CASE("conditional jumps take/skip the branch as expected", "[engine][jumps]") {
    // D=1
    CHECK(run_jump_test(0xefd0, 0xe301) == 0); // JGT taken
    CHECK(run_jump_test(0xefd0, 0xe302) == 1); // JEQ not taken
    CHECK(run_jump_test(0xefd0, 0xe303) == 0); // JGE taken
    CHECK(run_jump_test(0xefd0, 0xe305) == 0); // JNE taken
    CHECK(run_jump_test(0xefd0, 0xe306) == 1); // JLE not taken
    CHECK(run_jump_test(0xefd0, 0xe307) == 0); // JMP taken
    CHECK(run_jump_test(0xefd0, 0xe304) == 1); // JLT not taken

    // D=0
    CHECK(run_jump_test(0xea90, 0xe301) == 1); // JGT not taken
    CHECK(run_jump_test(0xea90, 0xe302) == 0); // JEQ taken
    CHECK(run_jump_test(0xea90, 0xe303) == 0); // JGE taken
    CHECK(run_jump_test(0xea90, 0xe304) == 1); // JLT not taken
    CHECK(run_jump_test(0xea90, 0xe305) == 1); // JNE not taken
    CHECK(run_jump_test(0xea90, 0xe306) == 0); // JLE taken
    CHECK(run_jump_test(0xea90, 0xe307) == 0); // JMP taken

    // D=-1
    CHECK(run_jump_test(0xee90, 0xe301) == 1); // JGT not taken
    CHECK(run_jump_test(0xee90, 0xe302) == 1); // JEQ not taken
    CHECK(run_jump_test(0xee90, 0xe303) == 1); // JGE not taken
    CHECK(run_jump_test(0xee90, 0xe304) == 0); // JLT taken
    CHECK(run_jump_test(0xee90, 0xe305) == 0); // JNE taken
    CHECK(run_jump_test(0xee90, 0xe306) == 0); // JLE taken
    CHECK(run_jump_test(0xee90, 0xe307) == 0); // JMP taken
}

TEST_CASE("set_ram/get_ram keep the screen in sync with RAM writes", "[engine]") {
    hack::HackEngine engine;
    engine.set_ram(0x4000, 0x0003);
    REQUIRE(engine.screen.get_pixel(0, 0));
    REQUIRE(engine.screen.get_pixel(1, 0));
    REQUIRE_FALSE(engine.screen.get_pixel(2, 0));

    constexpr int screen_words_per_row = hack::SCREEN_WIDTH / 16;
    uint16_t next_row = static_cast<uint16_t>(0x4000 + screen_words_per_row);
    engine.set_ram(next_row, 0x8000);
    REQUIRE(engine.screen.get_pixel(15, 1));
}
```

`0s` requires `#include <chrono>` and `using namespace std::chrono_literals;` — both are already covered by the `using namespace std::chrono_literals;` inside `run_to_hard_loop`'s enclosing anonymous namespace and `hack_engine.hpp`'s `<chrono>` include.

- [ ] **Step 5: Build and run tests**

```powershell
cmake --build cpp/build --config Debug
ctest --test-dir cpp/build -C Debug --output-on-failure
```

Expected: all tests pass, including `[engine][alu]`, the hand-assembled
program test, `[engine][jumps]` (all 20 cases as one parametrized-by-call
test), and the RAM/screen sync test.

- [ ] **Step 6: Commit**

```bash
cd /c/work/hack_olc
git add cpp/
git commit -m "$(cat <<'EOF'
cpp: port HackEngine core (ALU, fetch/decode/execute, breakpoints/watchpoints)

Direct port of the Rust emulator::engine::HackEngine, minus load_file
and the disassembler (later tasks). alu() is public (not
module-private like the Rust original) so the Catch2 test binary can
call it directly.

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01FJ7mbw7oTHRjamU6c2aEVQ
EOF
)"
git push
```

---

### Task 5: Disassembler

**Files:**
- Modify: `cpp/src/hack_engine.cpp` (implement `disassemble_one`/`disassemble_range`)
- Modify: `cpp/tests/test_engine.cpp` (append disassembler tests)

**Interfaces:**
- Consumes: `hack::HackEngine` (Task 4).
- Produces: `hack::HackEngine::disassemble_one(uint16_t) -> std::string` (static), `hack::HackEngine::disassemble_range(uint16_t, uint16_t) const -> std::vector<std::tuple<uint16_t, uint16_t, std::string>>`. Not called anywhere else in this plan — ported for parity with the Rust engine, ready for a future debugger UI.

- [ ] **Step 1: Append the disassembler implementation to `cpp/src/hack_engine.cpp`**

Add before the closing `} // namespace hack`:

```cpp
std::string HackEngine::disassemble_one(uint16_t word) {
    if ((word >> 15) == 0) {
        // A-instruction: @value
        return "@" + std::to_string(word & 0x7FFF);
    }

    // C-instruction: dest=comp;jump
    uint16_t a_bit = (word >> 12) & 0x1;
    uint16_t comp = (word >> 6) & 0x3F;
    uint16_t dest = (word >> 3) & 0x7;
    uint16_t jump = word & 0x7;

    std::string comp_str;
    if (a_bit == 0) {
        switch (comp) {
            case 0b101010: comp_str = "0"; break;
            case 0b111111: comp_str = "1"; break;
            case 0b111010: comp_str = "-1"; break;
            case 0b001100: comp_str = "D"; break;
            case 0b110000: comp_str = "A"; break;
            case 0b001101: comp_str = "!D"; break;
            case 0b110001: comp_str = "!A"; break;
            case 0b001111: comp_str = "-D"; break;
            case 0b110011: comp_str = "-A"; break;
            case 0b011111: comp_str = "D+1"; break;
            case 0b110111: comp_str = "A+1"; break;
            case 0b001110: comp_str = "D-1"; break;
            case 0b110010: comp_str = "A-1"; break;
            case 0b000010: comp_str = "D+A"; break;
            case 0b010011: comp_str = "D-A"; break;
            case 0b000111: comp_str = "A-D"; break;
            case 0b000000: comp_str = "D&A"; break;
            case 0b010101: comp_str = "D|A"; break;
            default: comp_str = "???"; break;
        }
    } else {
        switch (comp) {
            case 0b101010: comp_str = "0"; break;
            case 0b111111: comp_str = "1"; break;
            case 0b111010: comp_str = "-1"; break;
            case 0b001100: comp_str = "D"; break;
            case 0b110000: comp_str = "M"; break;
            case 0b001101: comp_str = "!D"; break;
            case 0b110001: comp_str = "!M"; break;
            case 0b001111: comp_str = "-D"; break;
            case 0b110011: comp_str = "-M"; break;
            case 0b011111: comp_str = "D+1"; break;
            case 0b110111: comp_str = "M+1"; break;
            case 0b001110: comp_str = "D-1"; break;
            case 0b110010: comp_str = "M-1"; break;
            case 0b000010: comp_str = "D+M"; break;
            case 0b010011: comp_str = "D-M"; break;
            case 0b000111: comp_str = "M-D"; break;
            case 0b000000: comp_str = "D&M"; break;
            case 0b010101: comp_str = "D|M"; break;
            default: comp_str = "???"; break;
        }
    }

    std::string dest_str;
    switch (dest) {
        case 0b000: dest_str = ""; break;
        case 0b001: dest_str = "M="; break;
        case 0b010: dest_str = "D="; break;
        case 0b011: dest_str = "MD="; break;
        case 0b100: dest_str = "A="; break;
        case 0b101: dest_str = "AM="; break;
        case 0b110: dest_str = "AD="; break;
        case 0b111: dest_str = "AMD="; break;
        default: break;
    }

    std::string jump_str;
    switch (jump) {
        case 0b000: jump_str = ""; break;
        case 0b001: jump_str = ";JGT"; break;
        case 0b010: jump_str = ";JEQ"; break;
        case 0b011: jump_str = ";JGE"; break;
        case 0b100: jump_str = ";JLT"; break;
        case 0b101: jump_str = ";JNE"; break;
        case 0b110: jump_str = ";JLE"; break;
        case 0b111: jump_str = ";JMP"; break;
        default: break;
    }

    return dest_str + comp_str + jump_str;
}

std::vector<std::tuple<uint16_t, uint16_t, std::string>> HackEngine::disassemble_range(
    uint16_t start, uint16_t count) const {
    std::vector<std::tuple<uint16_t, uint16_t, std::string>> result;
    result.reserve(count);
    for (uint16_t i = 0; i < count; ++i) {
        uint16_t addr = static_cast<uint16_t>(start + i);
        if (static_cast<size_t>(addr) >= rom.size()) break;
        uint16_t word = rom[addr];
        result.emplace_back(addr, word, disassemble_one(word));
    }
    return result;
}
```

- [ ] **Step 2: Append disassembler tests to `cpp/tests/test_engine.cpp`**

These three words are drawn straight from Task 4's own hand-assembled test
program and its labeled comments, so the expected mnemonics are independently
verifiable against that program's intent:

```cpp
TEST_CASE("disassemble_one decodes A- and C-instructions", "[engine][disasm]") {
    using hack::HackEngine;
    REQUIRE(HackEngine::disassemble_one(0x0002) == "@2");
    REQUIRE(HackEngine::disassemble_one(0x8c10) == "D=A");
    REQUIRE(HackEngine::disassemble_one(0xe307) == "D;JMP");
}

TEST_CASE("disassemble_range walks consecutive ROM words", "[engine][disasm]") {
    hack::HackEngine cpu;
    cpu.rom[0] = 0x0002;
    cpu.rom[1] = 0x8c10;
    cpu.rom[2] = 0xe307;

    auto lines = cpu.disassemble_range(0, 3);
    REQUIRE(lines.size() == 3);
    REQUIRE(std::get<0>(lines[0]) == 0);
    REQUIRE(std::get<2>(lines[0]) == "@2");
    REQUIRE(std::get<2>(lines[1]) == "D=A");
    REQUIRE(std::get<2>(lines[2]) == "D;JMP");
}
```

- [ ] **Step 3: Build and run tests**

```powershell
cmake --build cpp/build --config Debug
ctest --test-dir cpp/build -C Debug --output-on-failure
```

Expected: all tests pass, including the new `[engine][disasm]` cases.

- [ ] **Step 4: Commit**

```bash
cd /c/work/hack_olc
git add cpp/
git commit -m "$(cat <<'EOF'
cpp: port HackEngine disassembler

disassemble_one/disassemble_range, ported for parity with the Rust
engine (not used by main.cpp today, same as upstream).

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01FJ7mbw7oTHRjamU6c2aEVQ
EOF
)"
git push
```

---

### Task 6: Binary/hackem loader

**Files:**
- Create: `cpp/src/code_loader.cpp`
- Modify: `cpp/CMakeLists.txt` (add `src/code_loader.cpp`)
- Modify: `cpp/tests/CMakeLists.txt` (add `HACK_OLC_REPO_ROOT` compile definition)
- Modify: `cpp/tests/test_engine.cpp` (append loader tests)

**Interfaces:**
- Consumes: `hack::HackEngine` (Task 4, specifically `ram`, `rom`, `halt_addr`, `rom_words_loaded`, `ram_words_loaded`, `pc`, `sync_screen_from_ram()`).
- Produces: `hack::HackEngine::load_file(const std::string&)` (throws `std::runtime_error` on malformed input). `main.cpp` (Task 7) calls this.

- [ ] **Step 1: Write `cpp/src/code_loader.cpp`**

```cpp
// .hx (hackem) and raw .hack binary loader.

#include "hack_engine.hpp"

#include <cctype>
#include <charconv>
#include <stdexcept>
#include <vector>

namespace hack {
namespace {

enum class LoadTarget { None, Ram, Rom };

std::vector<std::string_view> split_lines(std::string_view text) {
    std::vector<std::string_view> lines;
    size_t start = 0;
    while (start <= text.size()) {
        size_t end = text.find('\n', start);
        if (end == std::string_view::npos) {
            lines.push_back(text.substr(start));
            break;
        }
        size_t line_end = end;
        if (line_end > start && text[line_end - 1] == '\r') --line_end;
        lines.push_back(text.substr(start, line_end - start));
        start = end + 1;
    }
    return lines;
}

std::string_view trim(std::string_view s) {
    size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string_view::npos) return {};
    size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

std::vector<std::string_view> split_whitespace(std::string_view s) {
    std::vector<std::string_view> parts;
    size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
        size_t start = i;
        while (i < s.size() && !std::isspace(static_cast<unsigned char>(s[i]))) ++i;
        if (i > start) parts.push_back(s.substr(start, i - start));
    }
    return parts;
}

uint16_t parse_u16(std::string_view s, int base, size_t lineno, const char* what) {
    uint16_t value = 0;
    auto res = std::from_chars(s.data(), s.data() + s.size(), value, base);
    if (res.ec != std::errc() || res.ptr != s.data() + s.size()) {
        throw std::runtime_error(
            "line " + std::to_string(lineno) + ": invalid " + what + " '" + std::string(s) + "'");
    }
    return value;
}

bool starts_with(std::string_view s, std::string_view prefix) {
    return s.size() >= prefix.size() && s.substr(0, prefix.size()) == prefix;
}

} // namespace

void HackEngine::load_file(const std::string& bin) {
    uint16_t address = 0;
    size_t rom_count = 0;
    size_t ram_count = 0;

    auto lines = split_lines(bin);

    if (starts_with(bin, "hackem")) {
        LoadTarget target = LoadTarget::None;
        for (size_t idx = 0; idx < lines.size(); ++idx) {
            size_t lineno = idx + 1;
            std::string_view line = trim(lines[idx]);
            if (line.empty()) continue;

            if (starts_with(line, "hackem")) {
                auto parts = split_whitespace(line);
                if (parts.size() != 3) {
                    throw std::runtime_error(
                        "line " + std::to_string(lineno) + ": invalid hackem header (expected 3 tokens)");
                }
                if (parts[1] != "v1.0") {
                    throw std::runtime_error(
                        "line " + std::to_string(lineno) + ": unsupported version '" + std::string(parts[1]) + "'");
                }
                std::string_view halt_str = parts[2];
                if (!starts_with(halt_str, "0x")) {
                    throw std::runtime_error(
                        "line " + std::to_string(lineno) + ": halt address missing 0x prefix");
                }
                halt_addr = parse_u16(halt_str.substr(2), 16, lineno, "halt address");
                continue;
            }
            if (starts_with(line, "//")) continue;

            if (starts_with(line, "RAM@")) {
                address = parse_u16(line.substr(4), 16, lineno, "RAM address");
                target = LoadTarget::Ram;
            } else if (starts_with(line, "ROM@")) {
                address = parse_u16(line.substr(4), 16, lineno, "ROM address");
                target = LoadTarget::Rom;
            } else {
                uint16_t value = parse_u16(line, 16, lineno, "hex word");
                switch (target) {
                    case LoadTarget::Ram:
                        ram[address] = value;
                        ram_count++;
                        break;
                    case LoadTarget::Rom:
                        rom[address] = value;
                        rom_count++;
                        break;
                    case LoadTarget::None:
                        throw std::runtime_error(
                            "line " + std::to_string(lineno) + ": data before any section header");
                }
                address = static_cast<uint16_t>(address + 1);
            }
        }
    } else {
        bool all_binary = true;
        for (auto raw : lines) {
            std::string_view t = trim(raw);
            if (t.empty() || starts_with(t, "//")) continue;
            for (char c : t) {
                if (c != '0' && c != '1') {
                    all_binary = false;
                    break;
                }
            }
            if (!all_binary) break;
        }
        if (!all_binary) {
            throw std::runtime_error("unrecognised file format (not hackem binary or .hack binary)");
        }

        for (size_t idx = 0; idx < lines.size(); ++idx) {
            size_t lineno = idx + 1;
            std::string_view line = trim(lines[idx]);
            if (line.empty() || starts_with(line, "//")) continue;
            uint16_t value = parse_u16(line, 2, lineno, "binary word");
            rom[address] = value;
            address = static_cast<uint16_t>(address + 1);
            rom_count++;
        }
    }

    rom_words_loaded = rom_count;
    ram_words_loaded = ram_count;
    pc = 0;
    sync_screen_from_ram();
}

} // namespace hack
```

- [ ] **Step 2: Add `code_loader.cpp` to the library in `cpp/CMakeLists.txt`**

```cmake
add_library(hack_engine STATIC
    src/pge_impl.cpp
    src/screen.cpp
    src/keyboard.cpp
    src/hack_engine.cpp
    src/code_loader.cpp
)
```

- [ ] **Step 3: Point tests at the repo-root data files in `cpp/tests/CMakeLists.txt`**

Add after `add_executable(hack_olc_tests test_engine.cpp)`:

```cmake
target_compile_definitions(hack_olc_tests PRIVATE
    HACK_OLC_REPO_ROOT="${CMAKE_SOURCE_DIR}/.."
)
```

(`CMAKE_SOURCE_DIR` here is `cpp/`, since that's where `project()` was
called, so `${CMAKE_SOURCE_DIR}/..` is the repo root containing
`hello.hackem` and `tests/data/test2.hackem`.)

- [ ] **Step 4: Append loader tests to `cpp/tests/test_engine.cpp`**

```cpp
#include <fstream>
#include <sstream>

namespace {

std::string read_repo_file(const char* relative_path) {
    std::string path = std::string(HACK_OLC_REPO_ROOT) + "/" + relative_path;
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("can't open " + path);
    }
    std::ostringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

hack::StopReason run_to_halt(hack::HackEngine& engine) {
    using namespace std::chrono_literals;
    while (true) {
        auto stop = engine.execute_instructions(10s);
        switch (stop) {
            case hack::StopReason::SysHalt:
            case hack::StopReason::HardLoop:
                return stop;
            case hack::StopReason::RefreshUI:
                continue;
            default:
                FAIL("unexpected stop reason");
        }
    }
}

} // namespace

TEST_CASE("load_file parses a hackem-format binary", "[loader]") {
    const char* binfile =
        "hackem v1.0 0x0000\n"
        "ROM@0000\n"
        "0002\n"
        "8c10\n"
        "0011\n"
        "8308\n"
        "000a\n"
        "8304\n"
        "0003\n"
        "8c10\n"
        "0010\n"
        "8308\n"
        "000a\n"
        "8307\n"
        "RAM@0000\n"
        "1234\n"
        "2345\n"
        "RAM@3333\n"
        "abcd\n"
        "ffff";

    hack::HackEngine hack;
    hack.load_file(binfile);

    REQUIRE(hack.rom[0] == 0x0002);
    REQUIRE(hack.rom[1] == 0x8c10);
}

TEST_CASE("load_file parses a raw .hack ASCII binary and writes the screen", "[loader]") {
    // Minimal .hack binary: @16384; M=-1; @16416; M=-1; @4; 0;JMP
    const char* hack_binary =
        "0100000000000000\n"
        "1110111010001000\n"
        "0100000000100000\n"
        "1110111010001000\n"
        "0000000000000100\n"
        "1110101010000111";

    hack::HackEngine engine;
    engine.load_file(hack_binary);
    REQUIRE(engine.rom_words_loaded == 6);

    auto result = run_to_halt(engine);
    REQUIRE(result == hack::StopReason::HardLoop);
    REQUIRE(engine.ram[0x4000] == 0xFFFF);
    REQUIRE(engine.ram[0x4020] == 0xFFFF);
}

TEST_CASE("a real C program (factorial+fib) runs to Sys.halt with the right result", "[loader]") {
    hack::HackEngine engine;
    std::string content = read_repo_file("tests/data/test2.hackem");
    engine.load_file(content);

    auto result = run_to_halt(engine);
    REQUIRE(result == hack::StopReason::SysHalt);
    REQUIRE(static_cast<int16_t>(engine.ram[256]) == 133);
}

TEST_CASE("hello.hackem halts and leaves a non-blank screen", "[loader]") {
    hack::HackEngine engine;
    std::string content = read_repo_file("hello.hackem");
    engine.load_file(content);

    auto result = run_to_halt(engine);
    REQUIRE(result == hack::StopReason::SysHalt);

    size_t nonzero_count = 0;
    for (size_t offset = 0x4000; offset < 0x6000; ++offset) {
        if (engine.ram[offset] != 0) ++nonzero_count;
    }
    REQUIRE(nonzero_count > 0);
}
```

- [ ] **Step 5: Build and run tests**

```powershell
cmake --build cpp/build --config Debug
ctest --test-dir cpp/build -C Debug --output-on-failure
```

Expected: all tests pass, including the new `[loader]` cases —
`test2.hackem` produces `133`, and `hello.hackem` halts with a non-blank
screen.

- [ ] **Step 6: Commit**

```bash
cd /c/work/hack_olc
git add cpp/
git commit -m "$(cat <<'EOF'
cpp: port the .hackem/.hack binary loader

Direct port of the Rust emulator::code_loader. Tests locate
tests/data/test2.hackem and hello.hackem at the repo root via a
HACK_OLC_REPO_ROOT compile definition rather than embedding them.

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01FJ7mbw7oTHRjamU6c2aEVQ
EOF
)"
git push
```

---

### Task 7: `hack_olc` application (main.cpp)

**Files:**
- Create: `cpp/src/main.cpp`
- Modify: `cpp/CMakeLists.txt` (add the `hack_olc` executable target, post-build copy of `hello.hackem`)

**Interfaces:**
- Consumes: `hack::HackEngine` (Task 4/5/6), `hack::HackScreen` (Task 2, via `engine.screen`), `hack::HackKeyboard` (Task 3).
- Produces: the `hack_olc.exe` executable. Nothing later in this plan depends on `main.cpp`'s internals.

- [ ] **Step 1: Write `cpp/src/main.cpp`**

```cpp
// hack_olc — the Hack CPU emulator, driven by olcPixelGameEngine for the
// screen and keyboard. Direct port of the Rust hack_olc main.rs.
//
// This is the only file that includes olcPixelGameEngine.h without
// OLC_PGE_APPLICATION defined here — the implementation lives in
// pge_impl.cpp (see the plan's "Single olcPixelGameEngine implementation
// TU rule").
#include "olcPixelGameEngine.h"

#include "hack_engine.hpp"
#include "keyboard.hpp"
#include "screen.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>

namespace {

// Starting emulated clock speed, in Hz. tetris.c's movement/drop timers are
// plain tick counters rather than wall-clock delays, so they only feel
// right if we cap how many instructions run per *real* second directly.
// Numpad +/- double/halve it live, shown in the status line.
constexpr float DEFAULT_HZ = 2'000'000.0f;

// Longest real time a single frame's instruction budget may span, so a
// stall (e.g. window drag) doesn't cause a catch-up burst on the next frame.
constexpr float MAX_FRAME_TIME = 0.05f;

// Hard ceiling on instructions executed in one frame, regardless of
// speed_hz_ — without this, cranking speed up with Numpad + has no upper
// bound and a big enough budget makes a single frame take so long to
// compute that the app stops responding to input entirely.
constexpr uint32_t MAX_INSTRUCTIONS_PER_FRAME = 2'000'000u;

// tetris.c paces gravity and horizontal/rotation repeat with two tick
// counters that both advance once per pass of the same game loop, so both
// are driven by whatever speed_hz_ we pick — but their constants aren't
// proportional to each other, so one knob can't satisfy both. Instead we
// present a held key as a brief pulse, like a real keyboard's typematic
// repeat, timed by actual wall-clock seconds.
constexpr float REPEAT_INITIAL_DELAY = 0.30f;
constexpr float REPEAT_INTERVAL = 0.08f;

// Instructions per keyboard-pulse sub-chunk: small enough that a "pulse"
// (or its absence) is only visible to roughly one game-loop iteration.
constexpr uint32_t SUBSTEP_INSTRUCTIONS = 500u;

std::string read_file_to_string(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("can't read " + path.string());
    }
    std::ostringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

} // namespace

class HackApp : public olc::PixelGameEngine {
public:
    /// `rom_path` is a path to a .hx/.hackem or raw .hack binary; falls back
    /// to hello.hackem next to the executable when absent.
    HackApp(const std::optional<std::string>& rom_path, std::filesystem::path exe_dir)
        : exe_dir_(std::move(exe_dir)) {
        sAppName = "hack_olc";

        std::string source = rom_path.has_value()
            ? read_file_to_string(*rom_path)
            : read_file_to_string(exe_dir_ / "hello.hackem");
        engine_.load_file(source);
    }

    bool OnUserCreate() override { return true; }

    bool OnUserUpdate(float elapsed_time) override {
        // Clamped well above MAX_INSTRUCTIONS_PER_FRAME so the displayed
        // number always reflects what's actually running.
        float max_speed_hz = static_cast<float>(MAX_INSTRUCTIONS_PER_FRAME) / MAX_FRAME_TIME;
        if (GetKey(olc::Key::NP_ADD).bPressed) {
            speed_hz_ = std::min(speed_hz_ * 2.0f, max_speed_hz);
        }
        if (GetKey(olc::Key::NP_SUB).bPressed) {
            speed_hz_ = std::max(speed_hz_ / 2.0f, 1.0f);
        }

        keyboard_.poll(*this);
        uint16_t raw_key = keyboard_.read();

        // Decide whether this frame delivers a repeat "pulse" of raw_key.
        uint16_t pulse_key;
        if (raw_key == 0) {
            last_physical_key_ = 0;
            pulse_key = 0;
        } else if (raw_key != last_physical_key_) {
            last_physical_key_ = raw_key;
            repeat_timer_ = REPEAT_INITIAL_DELAY;
            pulse_key = raw_key;
        } else {
            repeat_timer_ -= elapsed_time;
            if (repeat_timer_ <= 0.0f) {
                repeat_timer_ = REPEAT_INTERVAL;
                pulse_key = raw_key;
            } else {
                pulse_key = 0;
            }
        }

        if (!halted_) {
            float budget = std::round(std::min(elapsed_time, MAX_FRAME_TIME) * speed_hz_);
            uint32_t remaining = static_cast<uint32_t>(
                std::clamp(budget, 1.0f, static_cast<float>(MAX_INSTRUCTIONS_PER_FRAME)));

            // Only the first sub-chunk carries pulse_key; the rest of the
            // frame's budget sees 0.
            engine_.keyboard = pulse_key;
            while (remaining > 0) {
                uint32_t chunk = std::min(remaining, SUBSTEP_INSTRUCTIONS);
                try {
                    hack::StopReason reason = engine_.execute_count(chunk);
                    if (reason == hack::StopReason::SysHalt || reason == hack::StopReason::HardLoop) {
                        halted_ = true;
                        break;
                    }
                } catch (const hack::HackRuntimeError& e) {
                    halted_ = true;
                    std::fprintf(stderr, "emulator stopped: %s\n", e.what());
                    break;
                }
                engine_.keyboard = 0;
                remaining -= chunk;
            }
        }

        Clear(olc::WHITE);
        engine_.screen.draw(*this);

        auto [pc, a, d] = engine_.get_registers();
        char status[256];
        std::snprintf(status, sizeof(status),
            "PC=%04x A=%04x D=%04x  key=%3u pulse=%3u  %.0f Hz (Num +/-)  %s", pc, a, d, raw_key,
            pulse_key, speed_hz_, halted_ ? "HALTED" : "running");
        DrawString(4, hack::SCREEN_HEIGHT + 4, status, olc::DARK_GREY);

        return true;
    }

    bool OnUserDestroy() override { return true; }

private:
    hack::HackEngine engine_;
    hack::HackKeyboard keyboard_;
    std::filesystem::path exe_dir_;
    bool halted_ = false;
    float speed_hz_ = DEFAULT_HZ;
    uint16_t last_physical_key_ = 0;
    float repeat_timer_ = 0.0f;
};

int main(int argc, char* argv[]) {
    std::optional<std::string> rom_path;
    if (argc > 1) {
        rom_path = std::string(argv[1]);
    }

    std::filesystem::path exe_dir = std::filesystem::absolute(argv[0]).parent_path();

    HackApp app(rom_path, exe_dir);
    // olc::rcode is an unscoped enum (enum rcode { FAIL=0, OK=1, NO_FILE=-1 })
    // declared inside namespace olc; `olc::rcode::OK` is valid C++11+ syntax
    // for it (the header itself uses this exact form), so this compiles as
    // written against the vendored header.
    if (app.Construct(hack::SCREEN_WIDTH, hack::SCREEN_HEIGHT + 16, 2, 2) == olc::rcode::OK) {
        app.Start();
    }
    return 0;
}
```

- [ ] **Step 2: Add the `hack_olc` executable target to `cpp/CMakeLists.txt`**

Append at the end of the file (after `add_subdirectory(tests)`, or before —
order doesn't matter here):

```cmake
add_executable(hack_olc src/main.cpp)
target_link_libraries(hack_olc PRIVATE hack_engine)

add_custom_command(TARGET hack_olc POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/../hello.hackem
        $<TARGET_FILE_DIR:hack_olc>/hello.hackem
)
```

- [ ] **Step 3: Build**

```powershell
cmake --build cpp/build --config Debug
```

Expected: `hack_olc.exe` builds successfully. `hello.hackem` should now
exist next to the built executable (e.g. `cpp/build/Debug/hello.hackem`).

- [ ] **Step 4: Run tests (regression check — nothing here should have broken them)**

```powershell
ctest --test-dir cpp/build -C Debug --output-on-failure
```

Expected: all tests still pass.

- [ ] **Step 5: Manually run the app and confirm it renders**

```powershell
& cpp/build/Debug/hack_olc.exe
```

Expected: a window titled "hack_olc" opens showing "Hello, World!" rendered
via the Hack screen (matching what the Rust `cargo run` build shows), with a
status line underneath. Close the window when done. This step has no
automated assertion — it's a manual visual check, the same way the Rust
version was verified.

- [ ] **Step 6: Commit**

```bash
cd /c/work/hack_olc
git add cpp/
git commit -m "$(cat <<'EOF'
cpp: port the hack_olc application (main.cpp)

Direct port of the Rust HackApp/olc::Application impl: same tuning
constants, same pulse-key repeat logic, same sub-chunked instruction
throttle. Uses real olcPixelGameEngine's instance-method API
(Construct/Start/GetKey/Draw/Clear/DrawString on the derived class)
rather than the Rust binding's free-function shape.

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01FJ7mbw7oTHRjamU6c2aEVQ
EOF
)"
git push
```

---

### Task 8: Final integration pass

**Files:**
- Create: `cpp/README.md`
- No source changes.

**Interfaces:**
- Consumes: everything from Tasks 1–7.
- Produces: nothing new — this is a verification + documentation task.

- [ ] **Step 1: Clean rebuild from scratch**

```powershell
Remove-Item -Recurse -Force cpp/build -ErrorAction SilentlyContinue
cmake -S cpp -B cpp/build -G "Visual Studio 17 2022" -A x64
cmake --build cpp/build --config Release
```

Expected: a full clean configure+build succeeds in Release, confirming
nothing was accidentally relying on stale build state.

- [ ] **Step 2: Run the full test suite in Release**

```powershell
ctest --test-dir cpp/build -C Release --output-on-failure
```

Expected: every test from Tasks 2–6 passes (screen, keyboard, engine/alu,
engine/jumps, engine/disasm, loader).

- [ ] **Step 3: Manually run the Release build**

```powershell
& cpp/build/Release/hack_olc.exe
```

Expected: same visual result as Task 7 Step 5.

- [ ] **Step 4: Write `cpp/README.md`**

```markdown
# hack_olc (C++)

Native C++ port of the Rust `hack_olc` Hack CPU emulator, using
olcPixelGameEngine directly. The Rust project at the repo root is the
original implementation and is kept unmodified alongside this one.

## Build

Requires CMake 3.20+ and a C++20 compiler (developed against MSVC via
Visual Studio 17 2022).

\`\`\`powershell
cmake -S cpp -B cpp/build -G "Visual Studio 17 2022" -A x64
cmake --build cpp/build --config Release
\`\`\`

## Test

\`\`\`powershell
ctest --test-dir cpp/build -C Release --output-on-failure
\`\`\`

## Run

\`\`\`powershell
& cpp/build/Release/hack_olc.exe [path/to/rom.hackem]
\`\`\`

With no argument, runs the bundled `hello.hackem`. Numpad +/- doubles/halves
the emulated clock speed live.

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
```

- [ ] **Step 5: Commit and push**

```bash
cd /c/work/hack_olc
git add cpp/README.md
git commit -m "$(cat <<'EOF'
cpp: add README, verify clean Release build + full test suite

Completes the C++ port: clean configure+build+test in Release
confirmed, manual run confirmed against the same hello.hackem output
as the Rust version.

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01FJ7mbw7oTHRjamU6c2aEVQ
EOF
)"
git push
```
