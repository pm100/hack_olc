//! The Hack CPU: 16-bit ALU, fetch/decode/execute, breakpoints, watchpoints,
//! and a disassembler. Ported from hackem's `emulator::engine`, decoupled
//! from egui: screen writes go through our own `HackScreen` (word-for-word,
//! no pixel-color cache) instead of an `egui::ColorImage`, and the keyboard
//! register is a plain field set by the caller each frame instead of a
//! `static mut` egui reads out of.

use std::collections::BTreeMap;

use crate::screen::HackScreen;
use anyhow::{bail, Result};
use thiserror::Error;
use web_time::{Duration, Instant};

const SCREEN_WORDS: usize = 0x2000;

#[derive(Debug, Error, PartialEq)]
pub enum RuntimeError {
    #[error("Invalid instruction")]
    InvalidInstruction,
    #[error("Invalid RAM read address {0}")]
    InvalidReadAddress(u16),
    #[error("Invalid RAM write address {0}")]
    InvalidWriteAddress(u16),
    #[error("Invalid instruction address {0}")]
    InvalidPC(u16),
}

pub(crate) struct BreakPoint {
    pub enabled: bool,
}

pub struct WatchPoint {
    pub read: bool,
    pub write: bool,
    pub enabled: bool,
}

pub struct HackEngine {
    pub pc: u16,
    pub a: u16,
    pub d: u16,
    pub ram: [u16; 0x8000],
    pub rom: [u16; 0x8000],
    pub halt_addr: u16,
    pub speed: f32,
    pub rom_words_loaded: usize,
    pub ram_words_loaded: usize,
    inst_count: u64,
    pub break_points: BTreeMap<u16, BreakPoint>,
    pub watch_points: BTreeMap<u16, WatchPoint>,
    pub triggered_watchpoint: Option<u16>,
    /// Output port buffer: bytes written to RAM[0x7FFF] accumulate here.
    output_buffer: Vec<u8>,
    /// The memory-mapped screen (RAM[0x4000..0x6000)), kept in sync on every
    /// write via `set_ram`.
    pub screen: HackScreen,
    /// The memory-mapped keyboard register (RAM[0x6000]). The caller (main
    /// loop) is responsible for updating this from real input each frame —
    /// the CPU only ever reads it.
    pub keyboard: u16,
}

#[derive(Debug, PartialEq)]
pub(crate) enum StopReason {
    RefreshUI,
    SysHalt,
    HardLoop,
    BreakPoint,
    WatchPoint,
}

impl Default for HackEngine {
    fn default() -> Self {
        Self::new()
    }
}

impl HackEngine {
    pub fn new() -> HackEngine {
        HackEngine {
            pc: 0,
            a: 0,
            d: 0,
            ram: [0; 0x8000],
            rom: [0; 0x8000],
            halt_addr: 0,
            speed: 0.0,
            rom_words_loaded: 0,
            ram_words_loaded: 0,
            inst_count: 0,
            break_points: BTreeMap::new(),
            watch_points: BTreeMap::new(),
            triggered_watchpoint: None,
            output_buffer: Vec::new(),
            screen: HackScreen::new(),
            keyboard: 0,
        }
    }
    fn alu(x_in: u16, y_in: u16, c: u16) -> u16 {
        let zx = (c >> 5) & 0x1;
        let nx = (c >> 4) & 0x1;
        let zy = (c >> 3) & 0x1;
        let ny = (c >> 2) & 0x1;
        let f = (c >> 1) & 0x1;
        let no = c & 0x1;

        let x = if zx != 0 { 0 } else { x_in };
        let x = if nx != 0 { !x } else { x };

        let y = if zy != 0 { 0 } else { y_in };
        let y = if ny != 0 { !y } else { y };

        if f != 0 {
            if no != 0 {
                !u16::wrapping_add(x, y)
            } else {
                u16::wrapping_add(x, y)
            }
        } else if no != 0 {
            !(x & y)
        } else {
            x & y
        }
    }

    /// Rebuild `screen` from the raw RAM contents. Needed after bulk RAM
    /// writes (e.g. `load_file`) that bypass `set_ram`.
    pub fn sync_screen_from_ram(&mut self) {
        for offset in 0..SCREEN_WORDS {
            self.screen.write_word(offset, self.ram[0x4000 + offset]);
        }
    }

    pub fn set_ram(&mut self, address: u16, value: u16) -> Result<bool> {
        if address >= 0x8000 {
            bail!(RuntimeError::InvalidWriteAddress(address));
        }

        let ui_stop = match address {
            0x0000..=0x3fff => {
                self.ram[address as usize] = value;
                false
            }
            0x4000..=0x5fff => {
                let offset = address as usize - 0x4000;
                self.ram[address as usize] = value;
                self.screen.write_word(offset, value);
                false
            }
            0x6000 => {
                // keyboard - write ignored
                false
            }
            0x7fff => {
                // output port: buffer the byte, never interrupt execution
                self.output_buffer.push(value as u8);
                false
            }
            _ => {
                self.ram[address as usize] = value;
                false
            }
        };
        if let Some(wp) = self.watch_points.get(&address) {
            if wp.write && wp.enabled {
                self.triggered_watchpoint = Some(address);
            }
        }
        Ok(ui_stop)
    }

    /// Drain all bytes written to the output port (RAM[0x7FFF]) since the last call.
    /// Returns a lossy UTF-8 string of the accumulated output.
    pub fn take_output(&mut self) -> String {
        let s = String::from_utf8_lossy(&self.output_buffer).into_owned();
        self.output_buffer.clear();
        s
    }

    pub fn get_ram(&mut self, address: u16) -> Result<u16> {
        if address >= 0x8000 {
            bail!(RuntimeError::InvalidReadAddress(address));
        }
        if address == 0x6000 {
            return Ok(self.keyboard);
        }
        if let Some(wp) = self.watch_points.get(&address) {
            if wp.read && wp.enabled {
                self.triggered_watchpoint = Some(address);
            }
        }
        Ok(self.ram[address as usize])
    }
    /// Execute a single instruction. Returns the reason execution should
    /// stop (halt, hard loop, breakpoint, watchpoint), if any, plus whether
    /// this instruction wrote to the screen (`ui_stop`) — a hint that a
    /// wall-clock-driven caller may want to redraw promptly rather than
    /// batch more screen writes first.
    fn step(&mut self) -> Result<(Option<StopReason>, bool)> {
        if self.pc >= 0x8000 {
            bail!(RuntimeError::InvalidPC(self.pc));
        }
        self.inst_count += 1;

        let instruction = self.rom[self.pc as usize];

        // did we hit a call to Sys.halt?
        if self.halt_addr != 0 && self.pc == self.halt_addr + 1 {
            return Ok((Some(StopReason::SysHalt), false));
        }
        let opcode = instruction >> 15;
        let old_pc = self.pc;
        let mut ui_stop = false;
        self.pc += 1;
        match opcode {
            0 => {
                // A instruction
                self.a = instruction;
            }
            1 => {
                // C instruction
                let a = (instruction >> 12) & 0x1;
                let c = (instruction >> 6) & 0x3F;
                let d = (instruction >> 3) & 0x7;
                let j = instruction & 0x7;

                // cannot use A as a jump address and a ram read write address in the same instruction

                if j != 0 && d & 0x1 != 0 {
                    bail!(RuntimeError::InvalidInstruction);
                }

                let y = if a == 0 {
                    self.a
                } else {
                    self.get_ram(self.a)?
                };

                let alu_out = Self::alu(self.d, y, c);

                // M
                if d & 0x1 != 0 {
                    ui_stop = self.set_ram(self.a, alu_out)?;
                }
                // D
                if d & 0x2 != 0 {
                    self.d = alu_out;
                }
                // A (update is deferred til after jmp is tested)
                // see http://nand2tetris-questions-and-answers-forum.52.s1.nabble.com/Subtle-different-behaviors-between-the-CPU-emulator-and-the-proposed-CPU-design-td4034781.html
                let new_a = if d & 0x4 != 0 { alu_out } else { self.a };

                let pc = self.pc;

                if j & 0x1 != 0 && (alu_out as i16) > 0 {
                    self.pc = self.a;
                }
                if j & 0x2 != 0 && alu_out == 0 {
                    self.pc = self.a;
                }
                if j & 0x4 != 0 && (alu_out as i16) < 0 {
                    self.pc = self.a;
                }

                self.a = new_a;
                // detects halt loop
                // (halt)
                // @halt
                // 0;JMP

                if pc > 2 && self.pc == pc - 2 {
                    return Ok((Some(StopReason::HardLoop), ui_stop));
                }
            }
            _ => {
                bail!(RuntimeError::InvalidInstruction);
            }
        }
        if let Some(bp) = self.break_points.get(&old_pc) {
            if bp.enabled {
                return Ok((Some(StopReason::BreakPoint), ui_stop));
            }
        }
        if self.triggered_watchpoint.take().is_some() {
            return Ok((Some(StopReason::WatchPoint), ui_stop));
        }
        Ok((None, ui_stop))
    }

    pub(crate) fn execute_instructions(&mut self, run_time: Duration) -> Result<StopReason> {
        let start_time = Instant::now();
        self.speed = 0.0;
        let mut counter = 0;
        let inst_count_snap = self.inst_count;
        loop {
            counter += 1;
            // every chunk of instructions check to see if we should refresh the UI
            // by returning to the caller
            if counter > 1000 {
                let time = Instant::now() - start_time;
                if time > run_time {
                    self.speed =
                        (self.inst_count - inst_count_snap) as f32 / time.as_secs_f32() / 1000000.0;
                    return Ok(StopReason::RefreshUI);
                }
                counter = 0;
            }

            let (stop, ui_stop) = self.step()?;
            if let Some(reason) = stop {
                return Ok(reason);
            }
            if run_time == Duration::ZERO || ui_stop {
                return Ok(StopReason::RefreshUI);
            }
        }
    }

    /// Execute up to `count` instructions with no wall-clock check at all —
    /// for callers implementing their own instructions-per-second throttle
    /// (see hack_olc's main.rs). `execute_instructions`'s per-call
    /// `Instant::now()` overhead is negligible when it amortizes over
    /// hundreds of instructions between checks, but becomes the dominant
    /// cost — and an artificial ceiling on achievable speed — if called
    /// once per instruction the way a fixed-count-per-frame throttle would.
    pub fn execute_count(&mut self, count: u32) -> Result<StopReason> {
        for _ in 0..count {
            let (stop, _ui_stop) = self.step()?;
            if let Some(reason) = stop {
                return Ok(reason);
            }
        }
        Ok(StopReason::RefreshUI)
    }
    pub fn get_registers(&self) -> (u16, u16, u16) {
        (self.pc, self.a, self.d)
    }

    pub fn add_breakpoint(&mut self, address: u16) {
        self.break_points
            .insert(address, BreakPoint { enabled: true });
    }

    pub fn remove_breakpoint(&mut self, address: u16) {
        self.break_points.remove(&address);
    }

    pub fn remove_all_breakpoints(&mut self) {
        self.break_points.clear();
    }

    pub fn add_watchpoint(&mut self, address: u16, read: bool, write: bool) {
        self.watch_points.insert(
            address,
            WatchPoint {
                read,
                write,
                enabled: true,
            },
        );
    }

    pub fn remove_watchpoint(&mut self, address: u16) {
        self.watch_points.remove(&address);
    }

    pub fn remove_all_watchpoints(&mut self) {
        self.watch_points.clear();
    }

    /// Disassemble a single 16-bit Hack instruction word into a mnemonic string.
    pub fn disassemble_one(word: u16) -> String {
        if word >> 15 == 0 {
            // A-instruction: @value
            return format!("@{}", word & 0x7FFF);
        }

        // C-instruction: dest=comp;jump
        let a_bit = (word >> 12) & 0x1;
        let comp = (word >> 6) & 0x3F;
        let dest = (word >> 3) & 0x7;
        let jump = word & 0x7;

        let comp_str = if a_bit == 0 {
            match comp {
                0b101010 => "0",
                0b111111 => "1",
                0b111010 => "-1",
                0b001100 => "D",
                0b110000 => "A",
                0b001101 => "!D",
                0b110001 => "!A",
                0b001111 => "-D",
                0b110011 => "-A",
                0b011111 => "D+1",
                0b110111 => "A+1",
                0b001110 => "D-1",
                0b110010 => "A-1",
                0b000010 => "D+A",
                0b010011 => "D-A",
                0b000111 => "A-D",
                0b000000 => "D&A",
                0b010101 => "D|A",
                _ => "???",
            }
        } else {
            match comp {
                0b101010 => "0",
                0b111111 => "1",
                0b111010 => "-1",
                0b001100 => "D",
                0b110000 => "M",
                0b001101 => "!D",
                0b110001 => "!M",
                0b001111 => "-D",
                0b110011 => "-M",
                0b011111 => "D+1",
                0b110111 => "M+1",
                0b001110 => "D-1",
                0b110010 => "M-1",
                0b000010 => "D+M",
                0b010011 => "D-M",
                0b000111 => "M-D",
                0b000000 => "D&M",
                0b010101 => "D|M",
                _ => "???",
            }
        };

        let dest_str = match dest {
            0b000 => "",
            0b001 => "M=",
            0b010 => "D=",
            0b011 => "MD=",
            0b100 => "A=",
            0b101 => "AM=",
            0b110 => "AD=",
            0b111 => "AMD=",
            _ => unreachable!(),
        };

        let jump_str = match jump {
            0b000 => "",
            0b001 => ";JGT",
            0b010 => ";JEQ",
            0b011 => ";JGE",
            0b100 => ";JLT",
            0b101 => ";JNE",
            0b110 => ";JLE",
            0b111 => ";JMP",
            _ => unreachable!(),
        };

        format!("{}{}{}", dest_str, comp_str, jump_str)
    }

    /// Disassemble `count` instructions starting at `start` address.
    /// Returns (address, raw_word, mnemonic) for each instruction.
    pub fn disassemble_range(&self, start: u16, count: u16) -> Vec<(u16, u16, String)> {
        let mut result = Vec::with_capacity(count as usize);
        for i in 0..count {
            let addr = start.wrapping_add(i);
            if addr as usize >= self.rom.len() {
                break;
            }
            let word = self.rom[addr as usize];
            result.push((addr, word, Self::disassemble_one(word)));
        }
        result
    }
}
#[macro_export]
macro_rules! trace {
    ($fmt:literal, $($arg:expr),*) => {
        #[cfg(debug_assertions)]
        {
            if cfg!(test){
                println!($fmt, $($arg),*);
            } else {
                log::warn!($fmt, $($arg),*);
            }
        }
    };
    ($msg:expr) => {
        #[cfg(debug_assertions)]
        {
            if cfg!(test){
                println!($msg);
            } else {
                log::warn!($msg);
            }
        }
    };
}
#[cfg(test)]
mod tests {

    use super::*;
    fn test_alu_core(x: u16, y: u16) {
        assert!(HackEngine::alu(x, y, 0x2a) == 0);
        assert!(HackEngine::alu(x, y, 0x3f) == 1);
        assert!(HackEngine::alu(x, y, 0x3a) == 0xffff);
        assert!(HackEngine::alu(x, y, 0x0c) == x);
        assert!(HackEngine::alu(x, y, 0x30) == y);
        assert!(HackEngine::alu(x, y, 0x0d) == !x);
        assert!(HackEngine::alu(x, y, 0x31) == !y);
        assert!(HackEngine::alu(x, y, 0x0f) == u16::wrapping_sub(0, x));
        assert!(HackEngine::alu(x, y, 0x33) == u16::wrapping_sub(0, y));
        assert!(HackEngine::alu(x, y, 0x1f) == u16::wrapping_add(x, 1));
        assert!(HackEngine::alu(x, y, 0x37) == u16::wrapping_add(y, 1));
        assert!(HackEngine::alu(x, y, 0x0e) == u16::wrapping_sub(x, 1));
        assert!(HackEngine::alu(x, y, 0x32) == u16::wrapping_sub(y, 1));
        assert!(HackEngine::alu(x, y, 0x02) == u16::wrapping_add(x, y));
        assert!(HackEngine::alu(x, y, 0x13) == u16::wrapping_sub(x, y));
        assert!(HackEngine::alu(x, y, 0x07) == u16::wrapping_sub(y, x));
        assert!(HackEngine::alu(x, y, 0x00) == x & y);
        assert!(HackEngine::alu(x, y, 0x15) == x | y);
    }

    #[test]
    fn test_alu() {
        for x in 0..=0xffff {
            for y in 0..=0xff {
                test_alu_core(x, y);
            }
        }
        for y in 0..=0xffff {
            for x in 0..=0xff {
                test_alu_core(x, y);
            }
        }
    }

    // write some tests for the cpu
    #[test]
    fn test_cpu() {
        let mut cpu = HackEngine::new();
        //  let mut ram = [0; 0x8000];
        // @2
        // D=A
        // @x
        // M=D
        // @POOP
        // D;JLT
        // @3
        // D=A
        // @y
        // M=D
        // (POP)
        // (HALT)
        // @HALT
        // D;JMP
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

        loop {
            if cpu.execute_instructions(Duration::ZERO).unwrap() == StopReason::HardLoop {
                break;
            }
        }
        assert!(cpu.ram[17] == 2);
        assert!(cpu.ram[16] == 3);
    }

    #[test]
    fn test_jumps_1_gt() {
        let mut cpu = HackEngine::new();

        // at the end of test ram[1]
        //  == 1 means branch not taken
        //  == 0 means branch taken
        // D=1
        // @L1
        // D;JGT
        // @1
        // M=1
        // (L1)
        // @L1
        // D;JMP
        cpu.rom[0] = 0xefd0;
        cpu.rom[1] = 0x0005;
        cpu.rom[2] = 0xe301;
        cpu.rom[3] = 0x0001;
        cpu.rom[4] = 0xefc8;
        cpu.rom[5] = 0x0005;
        cpu.rom[6] = 0xe307;
        loop {
            if cpu.execute_instructions(Duration::ZERO).unwrap() == StopReason::HardLoop {
                break;
            }
        }
        assert!(cpu.ram[1] == 0);
    }
    #[test]
    fn test_jumps_1_eq() {
        let mut cpu = HackEngine::new();
        // D=1
        // @L1
        // D;JEQ
        // @1
        // M=1
        // (L1)
        // @L1
        // D;JMP
        cpu.rom[0] = 0xefd0;
        cpu.rom[1] = 0x0005;
        cpu.rom[2] = 0xe302;
        cpu.rom[3] = 0x0001;
        cpu.rom[4] = 0xefc8;
        cpu.rom[5] = 0x0005;
        cpu.rom[6] = 0xe307;
        loop {
            if cpu.execute_instructions(Duration::ZERO).unwrap() == StopReason::HardLoop {
                break;
            }
        }
        assert!(cpu.ram[1] == 1);
    }
    #[test]
    fn test_jumps_1_ge() {
        let mut cpu = HackEngine::new();
        // D=1
        // @L1
        // D;Jge
        // @1
        // M=1
        // (L1)
        // @L1
        // D;JMP
        cpu.rom[0] = 0xefd0;
        cpu.rom[1] = 0x0005;
        cpu.rom[2] = 0xe303;
        cpu.rom[3] = 0x0001;
        cpu.rom[4] = 0xefc8;
        cpu.rom[5] = 0x0005;
        cpu.rom[6] = 0xe307;
        loop {
            if cpu.execute_instructions(Duration::ZERO).unwrap() == StopReason::HardLoop {
                break;
            }
        }
        assert!(cpu.ram[1] == 0);
    }
    #[test]
    fn test_jumps_1_ne() {
        let mut cpu = HackEngine::new();
        // D=1
        // @L1
        // D;Jlt
        // @1
        // M=1
        // (L1)
        // @L1
        // D;JMP
        cpu.rom[0] = 0xefd0;
        cpu.rom[1] = 0x0005;
        cpu.rom[2] = 0xe305;
        cpu.rom[3] = 0x0001;
        cpu.rom[4] = 0xefc8;
        cpu.rom[5] = 0x0005;
        cpu.rom[6] = 0xe307;
        loop {
            if cpu.execute_instructions(Duration::ZERO).unwrap() == StopReason::HardLoop {
                break;
            }
        }
        assert!(cpu.ram[1] == 0);
    }
    #[test]
    fn test_jumps_1_le() {
        let mut cpu = HackEngine::new();
        // D=1
        // @L1
        // D;Jlt
        // @1
        // M=1
        // (L1)
        // @L1
        // D;JMP
        cpu.rom[0] = 0xefd0;
        cpu.rom[1] = 0x0005;
        cpu.rom[2] = 0xe306;
        cpu.rom[3] = 0x0001;
        cpu.rom[4] = 0xefc8;
        cpu.rom[5] = 0x0005;
        cpu.rom[6] = 0xe307;
        loop {
            if cpu.execute_instructions(Duration::ZERO).unwrap() == StopReason::HardLoop {
                break;
            }
        }
        assert!(cpu.ram[1] == 1);
    }
    #[test]
    fn test_jumps_1_jmp() {
        let mut cpu = HackEngine::new();
        // D=1
        // @L1
        // D;Jmp
        // @1
        // M=1
        // (L1)
        // @L1
        // D;JMP
        cpu.rom[0] = 0xefd0;
        cpu.rom[1] = 0x0005;
        cpu.rom[2] = 0xe307;
        cpu.rom[3] = 0x0001;
        cpu.rom[4] = 0xefc8;
        cpu.rom[5] = 0x0005;
        cpu.rom[6] = 0xe307;
        loop {
            if cpu.execute_instructions(Duration::ZERO).unwrap() == StopReason::HardLoop {
                break;
            }
        }
        assert!(cpu.ram[1] == 0);
    }
    #[test]
    fn test_jumps_0_gt() {
        let mut cpu = HackEngine::new();
        // D=0
        // @L1
        // D;JGT
        // @1
        // M=1
        // (L1)
        // @L1
        // D;JMP
        cpu.rom[0] = 0xea90;
        cpu.rom[1] = 0x0005;
        cpu.rom[2] = 0xe301;
        cpu.rom[3] = 0x0001;
        cpu.rom[4] = 0xefc8;
        cpu.rom[5] = 0x0005;
        cpu.rom[6] = 0xe307;
        loop {
            if cpu.execute_instructions(Duration::ZERO).unwrap() == StopReason::HardLoop {
                break;
            }
        }
        assert!(cpu.ram[1] == 1);
    }
    #[test]
    fn test_jumps_0_eq() {
        let mut cpu = HackEngine::new();
        // D=0
        // @L1
        // D;Jeq
        // @1
        // M=1
        // (L1)
        // @L1
        // D;JMP
        cpu.rom[0] = 0xea90;
        cpu.rom[1] = 0x0005;
        cpu.rom[2] = 0xe302;
        cpu.rom[3] = 0x0001;
        cpu.rom[4] = 0xefc8;
        cpu.rom[5] = 0x0005;
        cpu.rom[6] = 0xe307;
        loop {
            if cpu.execute_instructions(Duration::ZERO).unwrap() == StopReason::HardLoop {
                break;
            }
        }
        assert!(cpu.ram[1] == 0);
    }
    #[test]
    fn test_jumps_0_ge() {
        let mut cpu = HackEngine::new();
        // D=0
        // @L1
        // D;JGe
        // @1
        // M=1
        // (L1)
        // @L1
        // D;JMP
        cpu.rom[0] = 0xea90;
        cpu.rom[1] = 0x0005;
        cpu.rom[2] = 0xe303;
        cpu.rom[3] = 0x0001;
        cpu.rom[4] = 0xefc8;
        cpu.rom[5] = 0x0005;
        cpu.rom[6] = 0xe307;
        loop {
            if cpu.execute_instructions(Duration::ZERO).unwrap() == StopReason::HardLoop {
                break;
            }
        }
        assert!(cpu.ram[1] == 0);
    }
    #[test]
    fn test_jumps_1_lt() {
        let mut cpu = HackEngine::new();
        // D=1
        // @L1
        // D;Jlt
        // @1
        // M=1
        // (L1)
        // @L1
        // D;JMP
        cpu.rom[0] = 0xefd0;
        cpu.rom[1] = 0x0005;
        cpu.rom[2] = 0xe304;
        cpu.rom[3] = 0x0001;
        cpu.rom[4] = 0xefc8;
        cpu.rom[5] = 0x0005;
        cpu.rom[6] = 0xe307;
        loop {
            if cpu.execute_instructions(Duration::ZERO).unwrap() == StopReason::HardLoop {
                break;
            }
        }
        assert!(cpu.ram[1] == 1);
    }
    #[test]
    fn test_jumps_0_lt() {
        let mut cpu = HackEngine::new();
        // D=0
        // @L1
        // D;Jlt
        // @1
        // M=1
        // (L1)
        // @L1
        // D;JMP
        cpu.rom[0] = 0xea90;
        cpu.rom[1] = 0x0005;
        cpu.rom[2] = 0xe304;
        cpu.rom[3] = 0x0001;
        cpu.rom[4] = 0xefc8;
        cpu.rom[5] = 0x0005;
        cpu.rom[6] = 0xe307;
        loop {
            if cpu.execute_instructions(Duration::ZERO).unwrap() == StopReason::HardLoop {
                break;
            }
        }
        assert!(cpu.ram[1] == 1);
    }
    #[test]
    fn test_jumps_0_ne() {
        let mut cpu = HackEngine::new();
        // D=0
        // @L1
        // D;Jne
        // @1
        // M=1
        // (L1)
        // @L1
        // D;JMP
        cpu.rom[0] = 0xea90;
        cpu.rom[1] = 0x0005;
        cpu.rom[2] = 0xe305;
        cpu.rom[3] = 0x0001;
        cpu.rom[4] = 0xefc8;
        cpu.rom[5] = 0x0005;
        cpu.rom[6] = 0xe307;
        loop {
            if cpu.execute_instructions(Duration::ZERO).unwrap() == StopReason::HardLoop {
                break;
            }
        }
        assert!(cpu.ram[1] == 1);
    }
    #[test]
    fn test_jumps_0_le() {
        let mut cpu = HackEngine::new();
        // D=0
        // @L1
        // D;JGT
        // @1
        // M=1
        // (L1)
        // @L1
        // D;JMP
        cpu.rom[0] = 0xea90;
        cpu.rom[1] = 0x0005;
        cpu.rom[2] = 0xe306;
        cpu.rom[3] = 0x0001;
        cpu.rom[4] = 0xefc8;
        cpu.rom[5] = 0x0005;
        cpu.rom[6] = 0xe307;
        loop {
            if cpu.execute_instructions(Duration::ZERO).unwrap() == StopReason::HardLoop {
                break;
            }
        }
        assert!(cpu.ram[1] == 0);
    }
    #[test]
    fn test_jumps_0_jmp() {
        let mut cpu = HackEngine::new();
        // D=0
        // @L1
        // D;JGT
        // @1
        // M=1
        // (L1)
        // @L1
        // D;JMP
        cpu.rom[0] = 0xea90;
        cpu.rom[1] = 0x0005;
        cpu.rom[2] = 0xe307;
        cpu.rom[3] = 0x0001;
        cpu.rom[4] = 0xefc8;
        cpu.rom[5] = 0x0005;
        cpu.rom[6] = 0xe307;
        loop {
            if cpu.execute_instructions(Duration::ZERO).unwrap() == StopReason::HardLoop {
                break;
            }
        }
        assert!(cpu.ram[1] == 0);
    }
    #[test]
    fn test_jumps_neg1_gt() {
        let mut cpu = HackEngine::new();
        // D=-1
        // @L1
        // D;JGT
        // @1
        // M=1
        // (L1)
        // @L1
        // D;JMP
        cpu.rom[0] = 0xee90;
        cpu.rom[1] = 0x0005;
        cpu.rom[2] = 0xe301;
        cpu.rom[3] = 0x0001;
        cpu.rom[4] = 0xefc8;
        cpu.rom[5] = 0x0005;
        cpu.rom[6] = 0xe307;
        loop {
            if cpu.execute_instructions(Duration::ZERO).unwrap() == StopReason::HardLoop {
                break;
            }
        }
        assert!(cpu.ram[1] == 1);
    }
    #[test]
    fn test_jumps_neg1_eq() {
        let mut cpu = HackEngine::new();
        // D=0
        // @L1
        // D;Jeq
        // @1
        // M=1
        // (L1)
        // @L1
        // D;JMP
        cpu.rom[0] = 0xee90;
        cpu.rom[1] = 0x0005;
        cpu.rom[2] = 0xe302;
        cpu.rom[3] = 0x0001;
        cpu.rom[4] = 0xefc8;
        cpu.rom[5] = 0x0005;
        cpu.rom[6] = 0xe307;
        loop {
            if cpu.execute_instructions(Duration::ZERO).unwrap() == StopReason::HardLoop {
                break;
            }
        }
        assert!(cpu.ram[1] == 1);
    }
    #[test]
    fn test_jumps_neg1_ge() {
        let mut cpu = HackEngine::new();
        // D=0
        // @L1
        // D;JGe
        // @1
        // M=1
        // (L1)
        // @L1
        // D;JMP
        cpu.rom[0] = 0xee90;
        cpu.rom[1] = 0x0005;
        cpu.rom[2] = 0xe303;
        cpu.rom[3] = 0x0001;
        cpu.rom[4] = 0xefc8;
        cpu.rom[5] = 0x0005;
        cpu.rom[6] = 0xe307;
        loop {
            if cpu.execute_instructions(Duration::ZERO).unwrap() == StopReason::HardLoop {
                break;
            }
        }
        assert!(cpu.ram[1] == 1);
    }
    #[test]
    fn test_jumps_neg1_lt() {
        let mut cpu = HackEngine::new();
        // D=0
        // @L1
        // D;Jlt
        // @1
        // M=1
        // (L1)
        // @L1
        // D;JMP
        cpu.rom[0] = 0xee90;
        cpu.rom[1] = 0x0005;
        cpu.rom[2] = 0xe304;
        cpu.rom[3] = 0x0001;
        cpu.rom[4] = 0xefc8;
        cpu.rom[5] = 0x0005;
        cpu.rom[6] = 0xe307;
        loop {
            if cpu.execute_instructions(Duration::ZERO).unwrap() == StopReason::HardLoop {
                break;
            }
        }
        assert!(cpu.ram[1] == 0);
    }
    #[test]
    fn test_jumps_neg1_ne() {
        let mut cpu = HackEngine::new();
        // D=0
        // @L1
        // D;Jne
        // @1
        // M=1
        // (L1)
        // @L1
        // D;JMP
        cpu.rom[0] = 0xee90;
        cpu.rom[1] = 0x0005;
        cpu.rom[2] = 0xe305;
        cpu.rom[3] = 0x0001;
        cpu.rom[4] = 0xefc8;
        cpu.rom[5] = 0x0005;
        cpu.rom[6] = 0xe307;
        loop {
            if cpu.execute_instructions(Duration::ZERO).unwrap() == StopReason::HardLoop {
                break;
            }
        }
        assert!(cpu.ram[1] == 0);
    }
    #[test]
    fn test_jumps_neg1_le() {
        let mut cpu = HackEngine::new();
        // D=0
        // @L1
        // D;JGT
        // @1
        // M=1
        // (L1)
        // @L1
        // D;JMP
        cpu.rom[0] = 0xee90;
        cpu.rom[1] = 0x0005;
        cpu.rom[2] = 0xe306;
        cpu.rom[3] = 0x0001;
        cpu.rom[4] = 0xefc8;
        cpu.rom[5] = 0x0005;
        cpu.rom[6] = 0xe307;
        loop {
            if cpu.execute_instructions(Duration::ZERO).unwrap() == StopReason::HardLoop {
                break;
            }
        }
        assert!(cpu.ram[1] == 0);
    }
    #[test]
    fn test_jumps_neg1_jmp() {
        let mut cpu = HackEngine::new();
        // D=0
        // @L1
        // D;JGT
        // @1
        // M=1
        // (L1)
        // @L1
        // D;JMP
        cpu.rom[0] = 0xee90;
        cpu.rom[1] = 0x0005;
        cpu.rom[2] = 0xe307;
        cpu.rom[3] = 0x0001;
        cpu.rom[4] = 0xefc8;
        cpu.rom[5] = 0x0005;
        cpu.rom[6] = 0xe307;
        loop {
            if cpu.execute_instructions(Duration::ZERO).unwrap() == StopReason::HardLoop {
                break;
            }
        }
        assert!(cpu.ram[1] == 0);
    }

    // Run a real C program compiled by hack_cc:
    //   factorial(5) + fib(7) = 120 + 13 = 133
    // The return value of main ends up at RAM[256] (top of the call stack).
    #[test]
    fn test_c_program_factorial_fib() {
        let mut engine = HackEngine::new();
        let content = include_str!("../../tests/data/test2.hackem");
        engine.load_file(content).unwrap();

        let result = loop {
            let stop = engine
                .execute_instructions(Duration::from_secs(10))
                .unwrap();
            match stop {
                StopReason::SysHalt | StopReason::HardLoop => break stop,
                StopReason::RefreshUI => continue,
                other => panic!("unexpected stop: {:?}", other),
            }
        };
        assert_eq!(result, StopReason::SysHalt);
        assert_eq!(engine.ram[256] as i16, 133);
    }

    #[test]
    fn test_hello_screen_output() {
        let mut engine = HackEngine::new();
        let content = include_str!("../../hello.hackem");
        engine.load_file(content).unwrap();

        let result = loop {
            let stop = engine
                .execute_instructions(Duration::from_secs(10))
                .unwrap();
            match stop {
                StopReason::SysHalt | StopReason::HardLoop => break stop,
                StopReason::RefreshUI => continue,
                other => panic!("unexpected stop: {:?}", other),
            }
        };
        assert_eq!(result, StopReason::SysHalt);

        let screen = &engine.ram[0x4000..0x6000];
        let nonzero_count = screen.iter().filter(|&&v| v != 0).count();
        assert!(
            nonzero_count > 0,
            "Screen should have non-zero pixels after hello program runs"
        );
    }

    #[test]
    fn test_hack_binary_screen_write() {
        // Minimal .hack binary: @16384; M=-1; @16416; M=-1; @4; 0;JMP
        let hack_binary = "\
0100000000000000
1110111010001000
0100000000100000
1110111010001000
0000000000000100
1110101010000111";
        let mut engine = HackEngine::new();
        engine.load_file(hack_binary).unwrap();
        assert_eq!(engine.rom_words_loaded, 6);

        let result = loop {
            let stop = engine.execute_instructions(Duration::from_secs(1)).unwrap();
            match stop {
                StopReason::HardLoop => break stop,
                StopReason::RefreshUI => continue,
                other => panic!("unexpected stop: {:?}", other),
            }
        };
        assert_eq!(result, StopReason::HardLoop);
        assert_eq!(engine.ram[0x4000], 0xFFFF, "screen word 0 should be all-1s");
        assert_eq!(
            engine.ram[0x4020], 0xFFFF,
            "screen word 32 should be all-1s"
        );
    }

    #[test]
    fn test_screen_tracks_ram_writes() {
        let mut engine = HackEngine::new();
        engine.set_ram(0x4000, 0x0003).unwrap();
        assert!(engine.screen.get_pixel(0, 0));
        assert!(engine.screen.get_pixel(1, 0));
        assert!(!engine.screen.get_pixel(2, 0));

        let screen_words_per_row = crate::screen::SCREEN_WIDTH / 16;
        let next_row = 0x4000 + screen_words_per_row as u16;
        engine.set_ram(next_row, 0x8000).unwrap();
        assert!(engine.screen.get_pixel(15, 1));
    }

    /// Diagnostic only: dumps what our engine+HackScreen actually render for
    /// hello.hackem to a PPM, to compare orientation against the known-good
    /// reference at hack_cc/demo/hello.ppm. Not a real regression test.
    #[test]
    #[ignore]
    fn dump_hello_screen_ppm() {
        use crate::screen::{SCREEN_HEIGHT, SCREEN_WIDTH};
        use std::io::Write;

        let mut engine = HackEngine::new();
        engine
            .load_file(include_str!("../../hello.hackem"))
            .unwrap();
        loop {
            match engine.execute_instructions(Duration::from_secs(10)).unwrap() {
                StopReason::SysHalt | StopReason::HardLoop => break,
                StopReason::RefreshUI => continue,
                other => panic!("unexpected stop: {:?}", other),
            }
        }

        let path = std::env::temp_dir().join("hack_olc_dump.ppm");
        let mut f = std::fs::File::create(&path).unwrap();
        write!(f, "P6\n{SCREEN_WIDTH} {SCREEN_HEIGHT}\n255\n").unwrap();
        for y in 0..SCREEN_HEIGHT {
            for x in 0..SCREEN_WIDTH {
                let v = if engine.screen.get_pixel(x, y) { 0u8 } else { 255u8 };
                f.write_all(&[v, v, v]).unwrap();
            }
        }
        println!("wrote {}", path.display());
    }

    /// Diagnostic only: tetris.hackem never halts (it's an interactive game
    /// loop), so run it for a bounded number of frame-sized slices instead
    /// and dump whatever's drawn so far — enough to see the title/board.
    #[test]
    #[ignore]
    fn dump_tetris_screen_ppm() {
        use crate::screen::{SCREEN_HEIGHT, SCREEN_WIDTH};
        use std::io::Write;

        let mut engine = HackEngine::new();
        engine
            .load_file(include_str!("../../../hack_cc/demo/tetris.hackem"))
            .unwrap();
        // Duration::ZERO steps exactly one instruction per call — fully
        // deterministic, unlike time-sliced stepping (see the equivalent
        // diagnostic in hackem's own engine.rs for the cross-check this is
        // meant to line up with).
        for _ in 0..200_000 {
            match engine.execute_instructions(Duration::ZERO).unwrap() {
                StopReason::SysHalt | StopReason::HardLoop => break,
                _ => {}
            }
        }

        let path = std::env::temp_dir().join("hack_olc_tetris_dump.ppm");
        let mut f = std::fs::File::create(&path).unwrap();
        write!(f, "P6\n{SCREEN_WIDTH} {SCREEN_HEIGHT}\n255\n").unwrap();
        for y in 0..SCREEN_HEIGHT {
            for x in 0..SCREEN_WIDTH {
                let v = if engine.screen.get_pixel(x, y) { 0u8 } else { 255u8 };
                f.write_all(&[v, v, v]).unwrap();
            }
        }
        println!("wrote {}", path.display());
    }

    /// Diagnostic only: reproduces "keyboard stops responding after the
    /// first piece" headlessly — run some ticks with no key, then hold LEFT,
    /// mirroring main.rs's real per-frame keyboard-refresh granularity, and
    /// report exactly when/where a HardLoop or SysHalt fires.
    #[test]
    #[ignore]
    fn tetris_left_key_hard_loop() {
        let mut engine = HackEngine::new();
        engine
            .load_file(include_str!("../../../hack_cc/demo/tetris.hackem"))
            .unwrap();

        const KEY_LEFT: u16 = 130;
        let mut frame = 0;
        // ~60fps-equivalent chunks at the app's default 2,000,000 Hz.
        let per_frame_budget = 33_000u32;

        // A few seconds of "no key" to get past init and see a piece fall.
        for _ in 0..120 {
            frame += 1;
            engine.keyboard = 0;
            match engine.execute_count(per_frame_budget).unwrap() {
                StopReason::RefreshUI => {}
                other => {
                    println!("frame {frame} (no key): stopped with {other:?} at pc={:#06x}", engine.pc);
                    return;
                }
            }
        }
        println!("survived {frame} no-key frames, pc={:#06x}", engine.pc);

        // Now hold LEFT for a while, exactly as the live app would.
        for _ in 0..600 {
            frame += 1;
            engine.keyboard = KEY_LEFT;
            match engine.execute_count(per_frame_budget).unwrap() {
                StopReason::RefreshUI => {}
                other => {
                    println!(
                        "frame {frame} (holding LEFT): stopped with {other:?} at pc={:#06x}",
                        engine.pc
                    );
                    return;
                }
            }
        }
        println!("survived {frame} total frames without a HardLoop/SysHalt, pc={:#06x}", engine.pc);
    }
}
