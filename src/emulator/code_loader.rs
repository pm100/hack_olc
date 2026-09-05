//! `.hx` (hackem) and raw `.hack` binary loader. Ported unchanged from
//! hackem's `emulator::code_loader`, aside from calling our renamed
//! `sync_screen_from_ram`.

use anyhow::{bail, Context, Result};

use super::engine::HackEngine;

enum LoadTarget {
    Ram,
    Rom,
    None,
}

impl HackEngine {
    pub fn load_file(&mut self, bin: &str) -> Result<()> {
        let mut address = 0u16;
        let mut rom_count = 0usize;
        let mut ram_count = 0usize;

        if bin.starts_with("hackem") {
            let mut target = LoadTarget::None;
            for (lineno, raw_line) in bin.lines().enumerate() {
                let line = raw_line.trim();
                if line.is_empty() {
                    continue;
                }
                if line.starts_with("hackem") {
                    let parts = line.split_whitespace().collect::<Vec<&str>>();
                    if parts.len() != 3 {
                        bail!(
                            "line {}: invalid hackem header (expected 3 tokens)",
                            lineno + 1
                        );
                    }
                    if parts[1] != "v1.0" {
                        bail!("line {}: unsupported version '{}'", lineno + 1, parts[1]);
                    }
                    let halt_str = parts[2];
                    let halt_hex = halt_str.strip_prefix("0x").with_context(|| {
                        format!("line {}: halt address missing 0x prefix", lineno + 1)
                    })?;
                    self.halt_addr = u16::from_str_radix(halt_hex, 16).with_context(|| {
                        format!("line {}: invalid halt address '{}'", lineno + 1, halt_str)
                    })?;
                    continue;
                }
                if line.starts_with("//") {
                    continue;
                }
                if let Some(stripped) = line.strip_prefix("RAM@") {
                    address = u16::from_str_radix(stripped, 16).with_context(|| {
                        format!("line {}: invalid RAM address '{}'", lineno + 1, stripped)
                    })?;
                    target = LoadTarget::Ram;
                } else if let Some(stripped) = line.strip_prefix("ROM@") {
                    address = u16::from_str_radix(stripped, 16).with_context(|| {
                        format!("line {}: invalid ROM address '{}'", lineno + 1, stripped)
                    })?;
                    target = LoadTarget::Rom;
                } else {
                    let value = u16::from_str_radix(line, 16).with_context(|| {
                        format!("line {}: invalid hex word '{}'", lineno + 1, line)
                    })?;
                    match target {
                        LoadTarget::Ram => {
                            self.ram[address as usize] = value;
                            ram_count += 1;
                        }
                        LoadTarget::Rom => {
                            self.rom[address as usize] = value;
                            rom_count += 1;
                        }
                        LoadTarget::None => {
                            bail!("line {}: data before any section header", lineno + 1)
                        }
                    }
                    address = address.wrapping_add(1);
                }
            }
        } else if bin.lines().any(|l| {
            let t = l.trim();
            !t.is_empty() && !t.starts_with("//") && !t.chars().all(|c| c == '0' || c == '1')
        }) {
            bail!("unrecognised file format (not hackem binary or .hack binary)");
        } else {
            for (lineno, raw_line) in bin.lines().enumerate() {
                let line = raw_line.trim();
                if line.is_empty() || line.starts_with("//") {
                    continue;
                }
                let value = u16::from_str_radix(line, 2).with_context(|| {
                    format!("line {}: invalid binary word '{}'", lineno + 1, line)
                })?;
                self.rom[address as usize] = value;
                address = address.wrapping_add(1);
                rom_count += 1;
            }
        }

        self.rom_words_loaded = rom_count;
        self.ram_words_loaded = ram_count;
        self.pc = 0;
        self.sync_screen_from_ram();
        Ok(())
    }
}
#[cfg(test)]
mod tests {

    use crate::emulator::engine::HackEngine;

    #[test]
    fn test_load_file() {
        let binfile = r#"hackem v1.0 0x0000
ROM@0000
0002
8c10
0011
8308
000a
8304
0003
8c10
0010
8308
000a
8307
RAM@0000
1234
2345
RAM@3333
abcd
ffff"#;

        let mut hack = HackEngine::new();
        hack.load_file(binfile).unwrap();

        assert_eq!(hack.rom[0], 0x0002);
        assert_eq!(hack.rom[1], 0x8c10);
        //  assert_eq!(hack.ram[0], 0x1234);
        //  assert_eq!(hack.ram[0x3334], 0xffff);
    }
}
