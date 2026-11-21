#pragma once

#include "../Constants.hpp"

#include <array>
#include <cstdint>
#include <stdexcept>

class RegisterBank {
public:
    enum Index : std::uint8_t {
        ZERO = 0, PC = 1, AC = 2, SP = 3, IR = 4, TIR = 5,
        ONE = 6, MONE = 7, AMASK = 8, SMASK = 9
    };

    RegisterBank() { reset(); }

    std::uint32_t read(std::uint8_t index) const;
    void write(std::uint8_t index, std::uint32_t value, bool enable = true);
    void reset();

private:
    std::array<std::uint32_t, 32> values{};
};

inline std::uint32_t RegisterBank::read(std::uint8_t index) const {
    if (index >= values.size())
        throw std::out_of_range("registrador inválido");

    return values[index];
}

inline void RegisterBank::write(std::uint8_t index, std::uint32_t value, bool enable) {
    if (index >= values.size())
        throw std::out_of_range("registrador inválido");

    if (!enable || index == ZERO || (index >= ONE && index <= SMASK))
        return;

    values[index] = value;
}

inline void RegisterBank::reset() {
    values.fill(0);             // Zera os 32 registradores
    values[SP] = mic1::constants::initialStackPointer; // Inicia a pilha antes da região de E/S
    values[ONE] = 1;            // Constante +1
    values[MONE] = mic1::constants::negativeOne;       // Constante -1
    values[AMASK] = mic1::constants::addressFieldMask; // Máscara dos 12 bits do campo de endereço x
    values[SMASK] = mic1::constants::lowByteMask;      // Máscara dos 8 bits do campo y
}
