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
