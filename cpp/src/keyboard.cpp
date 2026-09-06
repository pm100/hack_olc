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
