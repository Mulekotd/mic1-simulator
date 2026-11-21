#pragma once

#include <cstdint>
#include <stdexcept>

class MMUX {
public:
    static std::uint8_t select(std::uint8_t mpc, std::uint8_t address, std::uint8_t cond, bool n, bool z);
};

inline std::uint8_t MMUX::select(std::uint8_t mpc, std::uint8_t address, std::uint8_t cond, bool n, bool z) {
    switch (cond) {
        case 0b00: break;                        // próxima microinstrução em sequência
        case 0b01: if (n) return address; break; // desvia quando o resultado é negativo
        case 0b10: if (z) return address; break; // desvia quando o resultado é zero
        case 0b11: return address;               // desvio incondicional
        default: throw std::invalid_argument("a condição deve ter 2 bits");
    }

    return static_cast<std::uint8_t>(mpc + 1);
}
