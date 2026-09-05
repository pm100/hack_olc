//! hack_olc — the Hack CPU emulator (ported from hackem's `emulator::engine`,
//! decoupled from egui) driven by olcPixelGameEngine for the screen and
//! keyboard.

mod emulator {
    mod code_loader;
    pub mod engine;
}
mod keyboard;
mod screen;

use emulator::engine::HackEngine;
use keyboard::HackKeyboard;
use olc_pixel_game_engine as olc;

/// Starting emulated clock speed, in Hz. tetris.c's movement/drop timers are
/// plain tick counters (e.g. `key_repeat_wait = 150`, decremented once per
/// game-loop pass) rather than wall-clock delays, so they only feel right if
/// we cap how many instructions run per *real* second directly. The previous
/// approach — run flat-out for an 8ms wall-clock budget every rendered frame
/// — put no ceiling on that at all: at the ~688 FPS this app renders at
/// uncapped, that's roughly 5.5 real seconds of full-speed CPU execution
/// crammed into every second of wall time, blowing through those tick counts
/// almost instantly (the reported "jumps hard left/right").
///
/// There's no reference platform to measure this against, so rather than
/// keep guessing constants across rebuilds: Numpad +/- double/halve it live,
/// shown in the status line.
const DEFAULT_HZ: f32 = 2_000_000.0;

/// Longest real time a single frame's instruction budget may span, so a
/// stall (e.g. window drag) doesn't cause a catch-up burst on the next frame.
const MAX_FRAME_TIME: f32 = 0.05;

/// Hard ceiling on instructions executed in one frame, regardless of
/// `speed_hz` — without this, cranking speed up with Numpad + has no upper
/// bound and a big enough budget makes a single frame take so long to
/// compute that the app stops responding to input entirely (looking exactly
/// like "the keyboard stopped working", not "running fast").
const MAX_INSTRUCTIONS_PER_FRAME: u32 = 2_000_000;

/// tetris.c paces gravity (`drop_counter` vs `current_drop_delay()`) and
/// horizontal/rotation repeat (`key_repeat_wait`, initial 150 then 45 loop
/// iterations) with two tick counters that both advance once per pass of the
/// same game loop — so both are driven by whatever `speed_hz` we pick. Their
/// constants aren't proportional to each other (a `speed_hz` that makes
/// gravity feel right makes movement repeat far too fast, and vice versa),
/// so one knob can't satisfy both. Instead of feeding the emulated keyboard
/// a continuously-held value for the whole frame (letting the CPU race
/// through `key_repeat_wait` at full emulated speed), we present it as a
/// brief pulse — like a real keyboard's typematic repeat — timed by actual
/// wall-clock seconds. Gravity still runs at `speed_hz`; movement now
/// repeats at a fixed real-world rate regardless of `speed_hz`.
const REPEAT_INITIAL_DELAY: f32 = 0.30;
const REPEAT_INTERVAL: f32 = 0.08;

/// Instructions per keyboard-pulse sub-chunk: small enough that a "pulse"
/// (or its absence) is only visible to roughly one game-loop iteration,
/// rather than the whole frame's — potentially much larger — budget.
const SUBSTEP_INSTRUCTIONS: u32 = 500;

struct HackApp {
    engine: HackEngine,
    keyboard: HackKeyboard,
    halted: bool,
    speed_hz: f32,
    /// Last physically-held key (0 = none), to detect a fresh press.
    last_physical_key: u16,
    /// Counts down to the next repeat pulse while a key is held.
    repeat_timer: f32,
}

impl HackApp {
    /// `rom` is a path to a `.hx`/`.hackem` or raw `.hack` binary; falls back
    /// to the bundled hello.hackem when none is given. A real "load a
    /// different program" UI (see hackem's `load`/`load_code` debugger
    /// command) can replace this later.
    fn new(rom: Option<&str>) -> Self {
        let mut engine = HackEngine::new();
        let source = match rom {
            Some(path) => {
                std::fs::read_to_string(path).unwrap_or_else(|e| panic!("can't read {path}: {e}"))
            }
            None => include_str!("../hello.hackem").to_string(),
        };
        engine
            .load_file(&source)
            .expect("ROM should be a valid .hackem/.hack binary");
        Self {
            engine,
            keyboard: HackKeyboard::new(),
            halted: false,
            speed_hz: DEFAULT_HZ,
            last_physical_key: 0,
            repeat_timer: 0.0,
        }
    }
}

impl olc::Application for HackApp {
    fn on_user_create(&mut self) -> Result<(), olc::Error> {
        Ok(())
    }

    fn on_user_update(&mut self, elapsed_time: f32) -> Result<(), olc::Error> {
        // Clamped well above MAX_INSTRUCTIONS_PER_FRAME so the displayed
        // number always reflects what's actually running, instead of
        // silently capping out while still climbing on-screen.
        let max_speed_hz = MAX_INSTRUCTIONS_PER_FRAME as f32 / MAX_FRAME_TIME;
        if olc::get_key(olc::Key::NP_ADD).pressed {
            self.speed_hz = (self.speed_hz * 2.0).min(max_speed_hz);
        }
        if olc::get_key(olc::Key::NP_SUB).pressed {
            self.speed_hz = (self.speed_hz / 2.0).max(1.0);
        }

        self.keyboard.poll();
        let raw_key = self.keyboard.read();

        // Decide whether this frame delivers a repeat "pulse" of raw_key
        // (see REPEAT_INITIAL_DELAY/REPEAT_INTERVAL's doc comment above).
        let pulse_key = if raw_key == 0 {
            self.last_physical_key = 0;
            0
        } else if raw_key != self.last_physical_key {
            // Fresh press (or switched keys): pulse immediately.
            self.last_physical_key = raw_key;
            self.repeat_timer = REPEAT_INITIAL_DELAY;
            raw_key
        } else {
            self.repeat_timer -= elapsed_time;
            if self.repeat_timer <= 0.0 {
                self.repeat_timer = REPEAT_INTERVAL;
                raw_key
            } else {
                0
            }
        };

        if !self.halted {
            use emulator::engine::StopReason::*;
            // .max(1) so a very low speed_hz can never round down to a
            // permanent 0-instruction budget and freeze the emulated CPU
            // outright — it should always be *possible* to speed back up.
            let mut remaining = ((elapsed_time.min(MAX_FRAME_TIME) * self.speed_hz).round() as u32)
                .clamp(1, MAX_INSTRUCTIONS_PER_FRAME);
            // Only the first sub-chunk carries pulse_key; the rest of the
            // frame's (possibly much larger) budget sees 0, so a pulse
            // reaches roughly one game-loop iteration, not every iteration
            // that fits in this frame's instruction budget.
            self.engine.keyboard = pulse_key;
            'run: while remaining > 0 {
                let chunk = remaining.min(SUBSTEP_INSTRUCTIONS);
                match self.engine.execute_count(chunk) {
                    Ok(SysHalt) | Ok(HardLoop) => {
                        self.halted = true;
                        break 'run;
                    }
                    Ok(_) => {}
                    Err(e) => {
                        self.halted = true;
                        eprintln!("emulator stopped: {e}");
                        break 'run;
                    }
                }
                self.engine.keyboard = 0;
                remaining -= chunk;
            }
        }

        olc::clear(olc::WHITE);
        self.engine.screen.draw();

        let (pc, a, d) = self.engine.get_registers();
        let status = format!(
            "PC={pc:04x} A={a:04x} D={d:04x}  key={raw_key:>3} pulse={pulse_key:>3}  {:.0} Hz (Num +/-)  {}",
            self.speed_hz,
            if self.halted { "HALTED" } else { "running" },
        );
        olc::draw_string(4, screen::SCREEN_HEIGHT + 4, &status, olc::DARK_GREY)?;
        Ok(())
    }

    fn on_user_destroy(&mut self) -> Result<(), olc::Error> {
        Ok(())
    }
}

fn main() {
    let rom = std::env::args().nth(1);
    let mut app = HackApp::new(rom.as_deref());
    olc::start(
        "hack_olc",
        &mut app,
        screen::SCREEN_WIDTH,
        screen::SCREEN_HEIGHT + 16,
        2,
        2,
    )
    .unwrap();
}
