#pragma once

#include <cstdint>

namespace mic1::constants {

// Campos da microinstrução de 36 bits
inline constexpr std::uint64_t controlWordMask = (std::uint64_t{1} << 36) - 1;
inline constexpr std::uint64_t registerSelectorMask = 0x1Fu;
inline constexpr std::uint64_t operationSelectorMask = 0x03u;
inline constexpr std::uint64_t branchAddressMask = 0xFFu;

// Campos da instrução de 32 bits e endereços de palavra
inline constexpr std::uint32_t addressFieldMask = 0xFFFu;
inline constexpr std::uint32_t subopcodeMask = 0xFu;
inline constexpr std::uint32_t formatAReservedBitsMask = 0x0FFFF000u;
inline constexpr std::uint32_t formatBReservedBitsMask = 0x00FFFF00u;
inline constexpr std::uint32_t lowByteMask = 0xFFu;

// As três últimas palavras do espaço de 4096 palavras são portas de E/S
inline constexpr std::uint32_t initialStackPointer = 0xFFCu;
inline constexpr std::uint32_t haltAddress = 0xFFDu;
inline constexpr std::uint32_t characterOutputAddress = 0xFFEu;
inline constexpr std::uint32_t integerOutputAddress = 0xFFFu;

// Representação de inteiros de 32 bits em complemento de dois
inline constexpr std::uint32_t negativeOne = 0xFFFFFFFFu;
inline constexpr std::uint32_t maxPositiveWord = 0x7FFFFFFFu;
inline constexpr std::int64_t wordModulus = 0x100000000LL;

}  // namespace mic1::constants
