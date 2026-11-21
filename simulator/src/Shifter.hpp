#pragma once

#include <cstdint>
#include <stdexcept>

class Shifter {
public:
    static std::uint32_t execute(std::uint32_t value, std::uint8_t sh);
};

inline std::uint32_t Shifter::execute(std::uint32_t value, std::uint8_t sh) {
    switch (sh) {
        case 0b00: return value;       // sem deslocamento (pass-through)
        case 0b01: return value >> 1;  // entra 0 no MSB, bit mais significativo à esquerda
        case 0b10: return value << 1;  // entra 0 no LSB, bit menos significativo à direita
        default: throw std::invalid_argument("sh inválido");
    }
}
