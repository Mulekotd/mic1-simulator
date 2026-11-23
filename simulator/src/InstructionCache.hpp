#pragma once

#include "Constants.hpp"
#include "Memory.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

// Cache de instruções de mapeamento direto com 16 linhas de quatro palavras
class InstructionCache {
public:
    static constexpr std::size_t lineWords = 4;
    static constexpr std::size_t lineCount = 16;

    enum class Access { None, Hit, Miss, Bypass };

    struct Stats {
        std::uint64_t hits = 0;
        std::uint64_t misses = 0;
    };

    bool cacheable(std::size_t address, std::size_t memoryWords) const {
        // o bloco inteiro deve caber na memória e ficar antes dos endereços de E/S
        const std::size_t first = (address / lineWords) * lineWords;
        return first + lineWords <= memoryWords &&
               first + lineWords <= mic1::constants::haltAddress;
    }

    bool lookup(std::size_t address, Word& value) {
        // o índice escolhe uma única linha e a tag identifica qual bloco está nela
        const std::size_t block = address / lineWords;
        const Line& line = lines[block % lineCount];

        if (line.valid && line.tag == block / lineCount) {
            ++stats.hits;
            value = line.words[address % lineWords];
            return true;
        }

        ++stats.misses;
        return false;
    }

    Word fill(const Memory& memory, std::size_t address) {
        // um miss substitui a linha escolhida e carrega as quatro palavras do bloco
        const std::size_t block = address / lineWords;
        Line& line = lines[block % lineCount];
        const std::size_t first = block * lineWords;

        line.valid = false; // evita expor dados parciais se a leitura falhar

        for (std::size_t offset = 0; offset < lineWords; ++offset)
            line.words[offset] = memory.read(first + offset);

        line.tag = block / lineCount;
        line.valid = true;

        return line.words[address % lineWords];
    }

    Stats counters() const { return stats; }

private:
    struct Line {
        std::array<Word, lineWords> words{};
        std::size_t tag = 0;
        bool valid = false;
    };

    std::array<Line, lineCount> lines{};
    Stats stats;
};
