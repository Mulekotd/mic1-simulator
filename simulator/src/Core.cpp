#include "Core.hpp"

#include "ALU.hpp"
#include "AMUX.hpp"
#include "Constants.hpp"
#include "MMUX.hpp"
#include "Shifter.hpp"
#include "generated/Mic1Rom.hpp"

#include <cstddef>
#include <stdexcept>

namespace {

struct MicroFields {
    explicit MicroFields(std::uint64_t word)
        : a((word >> 9) & mic1::constants::registerSelectorMask),
          b((word >> 14) & mic1::constants::registerSelectorMask),
          c((word >> 19) & mic1::constants::registerSelectorMask),
          alu((word >> 31) & mic1::constants::operationSelectorMask),
          shift((word >> 29) & mic1::constants::operationSelectorMask),
          cond((word >> 33) & mic1::constants::operationSelectorMask),
          address((word >> 1) & mic1::constants::branchAddressMask),
          useMbr((word >> 35) & 1), writeMbr((word >> 28) & 1),
          writeMar((word >> 27) & 1), read((word >> 26) & 1),
          write((word >> 25) & 1), writeC((word >> 24) & 1),
          ifetch(word & 1) {}

    std::uint8_t a, b, c, alu, shift, cond, address;
    bool useMbr, writeMbr, writeMar, read, write, writeC, ifetch; // sinais da microinstrução
};

}  // namespace

Core::Core(Memory& memory) : memory(memory) {
    for (std::size_t i = 0; i < microasm.size(); ++i)
        control.write(static_cast<std::uint8_t>(i), microasm[i]); // carrega as microinstruções na memória de controle
}

Core::Snapshot Core::snapshot() const {
    return { cpuState, clock, pending, instructionCount, haltedFlag, memoryLatency,
             justCompleted, instructionCache, lastCacheAccess };
}

void Core::restore(const Snapshot& state) {
    cpuState = state.cpu;
    clock = state.clock;
    pending = state.pending;
    instructionCount = state.instructions;
    haltedFlag = state.halted;
    memoryLatency = state.memoryLatency;
    justCompleted = state.justCompleted;
    instructionCache = state.instructionCache;
    lastCacheAccess = state.lastInstructionCacheAccess;
}

SubcycleEvent Core::step() {
    if (haltedFlag)
        throw std::logic_error("simulação já encerrada");

    SubcycleEvent event;
    event.mpc = cpuState.mpc;
    event.word = control.read(cpuState.mpc);
    event.memoryStall = pending.active && pending.repeat && pending.remaining > 1;

    // a resposta da memória chega antes da ULA consumir os registradores
    clock.tick([&] {
        justCompleted = false;
        advanceMemory(event);

        // uma operação RD/WR repetida aguarda a transação pendente antes de avançar
        if (event.memoryStall || (haltedFlag && pending.repeat))
            return;

        executeMicro(event);
    });

    event.halted = haltedFlag;
    return event;
}

void Core::validateAddress(std::uint64_t address, bool write) const {
    // inicia os endereços reservados para E/S
    if (address >= memory.sizeWords() || (!write && address >= mic1::constants::haltAddress))
        throw std::out_of_range("endereço fora da memória principal");
}

void Core::validateInstruction(Word word) {
    const unsigned opcode = word >> 28; // opcode ocupa os quatro bits mais altos da instrução

    if (opcode < 15) {
        // no formato A, os bits 12 a 27 são reservados e devem estar zerados
        if (word & mic1::constants::formatAReservedBitsMask)
            throw std::runtime_error("instrução A com bits reservados não nulos");
    } else {
        // no formato B, o subopcode ocupa os bits 24 a 27
        const unsigned sub = (word >> 24) & mic1::constants::subopcodeMask;

        // as máscaras testam bits reservados e o operando baixo quando ausente
        if ((word & mic1::constants::formatBReservedBitsMask) ||
            (sub & 1u) || (sub < 12 && (word & mic1::constants::lowByteMask)))
            throw std::runtime_error("instrução B inválida");
    }
}

void Core::advanceMemory(SubcycleEvent& event) {
    if (!pending.active || --pending.remaining != 0)
        return;

    if (pending.read) {
        validateAddress(pending.address);

        const Word value = pending.instructionFetch
            ? (pending.cacheHit ? pending.value : instructionCache.fill(memory, pending.address))
            : memory.read(pending.address);

        cpuState.mbr.write(value);
    } else {
        validateAddress(pending.address, true);

        if (pending.address == mic1::constants::haltAddress) { // encerra a execução
            haltedFlag = true;
        } else if (pending.address == mic1::constants::characterOutputAddress) { // emite o byte menos significativo como caractere
            event.characterOutput = true;
            event.output = pending.value & mic1::constants::lowByteMask;
        } else if (pending.address == mic1::constants::integerOutputAddress) { // emite a palavra de 32 bits como inteiro
            event.integerOutput = true;
            event.output = pending.value;
        } else {
            const Word previousValue = memory.read(pending.address);
            memory.write(pending.address, pending.value);
            event.memoryWrite = true;
            event.memoryWriteAddress = pending.address;
            event.memoryPreviousValue = previousValue;
            event.memoryNewValue = pending.value;
        }
    }

    pending.active = false;
    justCompleted = true;
}

void Core::validateMarSource(std::uint8_t mpc, Word raw) const {
    // raw é o valor da porta B antes de MAR descartar os bits acima do bit 11
    // haltAddress inicia as portas de E/S; acessos comuns à RAM devem ficar abaixo dele

    // MPC 0 busca a instrução em PC, que deve apontar para uma palavra de programa
    if (mpc == 0 && raw >= mic1::constants::haltAddress)
        throw std::out_of_range("PC fora da memória de programa");

    // MPC 32, 34, 37 e 39 acessam SP + x em LODL, STOL, ADDL e SUBL
    // IR contém também o opcode, então addressFieldMask isola x antes da soma em 64 bits
    // A soma completa impede que o corte em 12 bits faça um endereço inválido voltar à RAM
    if ((mpc == 32 || mpc == 34 || mpc == 37 || mpc == 39) &&
        std::uint64_t{cpuState.bank.read(RegisterBank::SP)} +
            (cpuState.bank.read(RegisterBank::IR) & mic1::constants::addressFieldMask) >=
                mic1::constants::haltAddress)
        throw std::out_of_range("endereço relativo fora da RAM");

    // MPC 48, 55, 56, 61, 62 e 67 acessam a pilha; 53 lê M[AC] em PSHI
    // Esses acessos não podem alcançar E/S nem ultrapassar o fim da RAM
    if ((mpc == 48 || mpc == 53 || mpc == 55 || mpc == 56 ||
         mpc == 61 || mpc == 62 || mpc == 67) && raw >= mic1::constants::haltAddress)
        throw std::out_of_range("endereço de pilha ou indireto fora da RAM");

    // MPC 58 é POPI: escreve M[AC], inclusive nas portas de E/S válidas
    // Apenas rejeita endereços além das 4096 palavras da memória
    if (mpc == 58 && raw >= memory.sizeWords())
        throw std::out_of_range("endereço indireto fora da RAM");
}

void Core::executeMicro(SubcycleEvent& event) {
    const std::uint64_t word = event.word;
    const std::uint8_t mpc = event.mpc;
    const MicroFields fields(word);

    const Word a = cpuState.bank.read(fields.a);
    const Word b = cpuState.bank.read(fields.b);

    const Word aluInput = AMUX::select(a, cpuState.mbr.read(), fields.useMbr);
    const auto result = ALU::execute(aluInput, b, fields.alu);
    const Word shifted = Shifter::execute(result.value, fields.shift);

    if (fields.read && fields.write)
        throw std::runtime_error("RD e WR simultâneos");

    if (fields.ifetch && !fields.read)
        throw std::runtime_error("IF exige RD");

    const Word mar = fields.writeMar ? b & mic1::constants::addressFieldMask : cpuState.mar.read(); // MAR usa os 12 bits baixos do endereço
    const Word mbr = fields.writeMbr ? shifted : cpuState.mbr.read();

    if (fields.writeMar)
        validateMarSource(mpc, b);

    if (fields.read || fields.write) {
        if (!pending.active) {
            if (!(justCompleted && pending.read == fields.read && pending.address == mar)) {
                unsigned latency = memoryLatency;
                Word value = mbr;
                bool cacheHit = false;
                bool instructionFetch = false;

                if (fields.ifetch) {
                    validateAddress(mar);

                    if (instructionCache.cacheable(mar, memory.sizeWords())) {
                        cacheHit = instructionCache.lookup(mar, value);

                        event.instructionCacheAccess = cacheHit
                            ? InstructionCache::Access::Hit : InstructionCache::Access::Miss;

                        instructionFetch = true;

                        if (cacheHit)
                            latency = 1;
                    } else {
                        event.instructionCacheAccess = InstructionCache::Access::Bypass;
                    }

                    lastCacheAccess = event.instructionCacheAccess;
                }

                pending = { true, fields.read, mar, value, latency, false,
                            instructionFetch, cacheHit };
            }
        } else if (pending.read != fields.read) {
            throw std::runtime_error("transações de memória sobrepostas");
        } else {
            pending.repeat = true;
        }
    }

    const std::uint8_t next = MMUX::select(mpc, fields.address, fields.cond, result.n, result.z);

    if (next == 0 && mpc != 0)
        ++instructionCount;

    cpuState.a.write(a);
    cpuState.b.write(b);

    if (fields.writeMar)
        cpuState.mar.write(mar);

    if (fields.writeMbr)
        cpuState.mbr.write(shifted);

    if (fields.writeC) {
        if (fields.c == RegisterBank::IR)
            validateInstruction(shifted);

        cpuState.bank.write(fields.c, shifted);
    }

    cpuState.mir.write(word);
    cpuState.mpc = next;

    // captura as flags do resultado da ULA
    cpuState.n = result.n;
    cpuState.z = result.z;
}
