#pragma once

#include "Constants.hpp"
#include "Core.hpp"
#include "RAM.hpp"
#include "io/BinaryImage.hpp"

#include <cstdint>
#include <deque>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

// Guarda estados anteriores para permitir voltar um ciclo na simulação
class Session {
public:
    void load(const std::string& path) {
        RAM loaded;
        loadBinaryImage(path, loaded);
        ram = loaded;
        machine = std::make_unique<Core>(ram);
        program = path;
        history.clear();
        output.clear();
        lastEventState = {};
        running = false;
    }

    void reset() {
        if (program.empty())
            throw std::runtime_error("nenhum programa carregado");

        load(program);
    }

    bool step() {
        if (!machine || machine->halted())
            return false;

        history.push_back({ ram, machine->snapshot(), output.size(), lastEventState });

        if (history.size() > 1024) // limita o histórico em 1024 ciclos
            history.pop_front();

        try {
            const auto event = machine->step();
            lastEventState = event;

            if (event.integerOutput) {
                // valores acima de maxPositiveWord precisam subtrair wordModulus para recuperar o sinal
                const std::int64_t signedValue = event.output <= mic1::constants::maxPositiveWord
                    ? event.output : std::int64_t{event.output} - mic1::constants::wordModulus;

                output.push_back("PROGRAM_OUT=" + std::to_string(signedValue));
            }

            if (event.characterOutput)
                output.push_back(std::string(1, static_cast<char>(event.output)));

            if (machine->halted())
                running = false;
        } catch (...) {
            restoreLast();
            throw;
        }

        return true;
    }

    bool back() {
        running = false;
        return restoreLast();
    }

    void run() {
        if (machine && !machine->halted())
            running = true;
    }

    void pause() { running = false; }
    bool isRunning() const { return running; }
    bool isLoaded() const { return static_cast<bool>(machine); }
    bool isHalted() const { return machine && machine->halted(); }

    std::uint64_t cycles() const { return machine ? machine->cycles() : 0; }
    std::uint64_t instructions() const { return machine ? machine->instructions() : 0; }
    InstructionCache::Stats instructionCacheStats() const {
        return machine ? machine->instructionCacheStats() : InstructionCache::Stats{};
    }
    InstructionCache::Access lastInstructionCacheAccess() const {
        return machine ? machine->lastInstructionCacheAccess() : InstructionCache::Access::None;
    }

    const std::string& path() const { return program; }
    const std::vector<std::string>& outputs() const { return output; }
    const CPURegisters* cpu() const { return machine ? &machine->cpu() : nullptr; }
    const RAM* memory() const { return isLoaded() ? &ram : nullptr; }
    const MicroCycleEvent& lastEvent() const { return lastEventState; }

private:
    struct History {
        RAM ram;
        Core::Snapshot machine;
        std::size_t outputCount;
        MicroCycleEvent lastEvent;
    };

    bool restoreLast() {
        if (history.empty())
            return false;

        ram = history.back().ram;
        machine->restore(history.back().machine);
        output.resize(history.back().outputCount);
        lastEventState = history.back().lastEvent;
        history.pop_back();

        return true;
    }

    RAM ram;
    std::unique_ptr<Core> machine;
    std::string program;
    std::deque<History> history;
    std::vector<std::string> output;
    MicroCycleEvent lastEventState;

    bool running = false;
};
