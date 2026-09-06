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
