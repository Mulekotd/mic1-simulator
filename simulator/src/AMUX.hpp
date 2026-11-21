#pragma once

#include <cstdint>
#include <stdexcept>

class AMUX {
public:
    static std::uint32_t select(std::uint32_t aLatch, std::uint32_t mbr, std::uint8_t amux);
};

inline std::uint32_t AMUX::select(std::uint32_t aLatch, std::uint32_t mbr, std::uint8_t amux) {
    switch (amux) {
        case 0b00: return aLatch; // seleciona o registrador indicado pelo campo A
        case 0b01: return mbr;    // seleciona o dado lido da memória em MBR
        default: throw std::invalid_argument("amux deve ter 1 bit");
    }
}
