#ifndef ADXL355_TESTS_TEST_SUPPORT_HPP
#define ADXL355_TESTS_TEST_SUPPORT_HPP

#include "Adxl355/Device/Device.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace Adxl355Test {

struct Transaction {
    std::vector<std::uint8_t> transmit;
};

class RecordingDelay final : public Adxl355::Delay {
  public:
    void delayMilliseconds(std::uint32_t milliseconds) override {
        calls.push_back(milliseconds);
    }

    std::vector<std::uint32_t> calls;
};

class RegisterSpi final : public Adxl355::SpiBus {
  public:
    RegisterSpi()
        : registers(),
          fifo(),
          transactions(),
          failNext(false),
          ignorePowerWrites(false),
          failWriteAddress(-1),
          nvmBusyReads(0) {
        registers[0x00] = 0xAD;
        registers[0x01] = 0x1D;
        registers[0x02] = 0xED;
        registers[0x03] = 0x01;
        registers[0x2C] = 0x81;
        registers[0x2D] = 0x01;
    }

    bool transfer(const std::uint8_t* transmit, std::uint8_t* receive, std::size_t size) override {
        transactions.push_back(Transaction{std::vector<std::uint8_t>(transmit, transmit + size)});
        if (failNext) {
            failNext = false;
            return false;
        }
        if ((transmit == nullptr) || (receive == nullptr) || (size < 2U)) {
            return false;
        }

        for (std::size_t i = 0; i < size; ++i) {
            receive[i] = 0U;
        }

        const std::uint8_t address = static_cast<std::uint8_t>(transmit[0] >> 1U);
        const bool isRead = (transmit[0] & 0x01U) != 0U;
        if (!isRead && address == 0x2DU && ignorePowerWrites) {
            return true;
        }
        if (!isRead && (static_cast<int>(address) == failWriteAddress)) {
            failWriteAddress = -1;
            return false;
        }
        if (isRead) {
            if (address == 0x11U) {
                for (std::size_t i = 1; i < size; ++i) {
                    receive[i] = (i - 1U < fifo.size()) ? fifo[i - 1U] : 0x02U;
                }
            } else {
                for (std::size_t i = 1; i < size; ++i) {
                    const std::size_t registerAddress = static_cast<std::size_t>(address) + i - 1U;
                    receive[i] = registers.at(registerAddress);
                }
                if ((address == 0x04U) && (nvmBusyReads > 0U)) {
                    receive[1] = static_cast<std::uint8_t>(receive[1] | 0x10U);
                    --nvmBusyReads;
                }
            }
        } else {
            for (std::size_t i = 1; i < size; ++i) {
                registers.at(static_cast<std::size_t>(address) + i - 1U) = transmit[i];
            }
            if ((address == 0x2FU) && (transmit[1] == 0x52U)) {
                const std::uint8_t idAd = registers[0x00];
                const std::uint8_t idMst = registers[0x01];
                const std::uint8_t partId = registers[0x02];
                const std::uint8_t revision = registers[0x03];
                registers.fill(0U);
                registers[0x00] = idAd;
                registers[0x01] = idMst;
                registers[0x02] = partId;
                registers[0x03] = revision;
                registers[0x29] = 0x60;
                registers[0x2C] = 0x81;
                registers[0x2D] = 0x01;
            }
        }
        return true;
    }

    void setRawSample(std::int32_t x, std::int32_t y, std::int32_t z) {
        encodeDirect(x, &registers[0x08]);
        encodeDirect(y, &registers[0x0B]);
        encodeDirect(z, &registers[0x0E]);
    }

    void setTemperature(std::uint16_t raw) {
        registers[0x06] = static_cast<std::uint8_t>((raw >> 8U) & 0x0FU);
        registers[0x07] = static_cast<std::uint8_t>(raw & 0xFFU);
    }

    static void encodeDirect(std::int32_t raw, std::uint8_t* output) {
        const std::uint32_t value = static_cast<std::uint32_t>(raw) & 0x000FFFFFU;
        const std::uint32_t packed = value << 4U;
        output[0] = static_cast<std::uint8_t>((packed >> 16U) & 0xFFU);
        output[1] = static_cast<std::uint8_t>((packed >> 8U) & 0xFFU);
        output[2] = static_cast<std::uint8_t>(packed & 0xF0U);
    }

    void appendFifoSample(std::int32_t x, std::int32_t y, std::int32_t z) {
        appendFifoAxis(x, true);
        appendFifoAxis(y, false);
        appendFifoAxis(z, false);
        registers[0x05] = static_cast<std::uint8_t>(fifo.size() / 3U);
    }

    void appendFifoAxis(std::int32_t raw, bool xMarker) {
        std::uint8_t packed[3] = {0U, 0U, 0U};
        encodeDirect(raw, packed);
        packed[2] = static_cast<std::uint8_t>(packed[2] | (xMarker ? 0x01U : 0x00U));
        fifo.insert(fifo.end(), packed, packed + 3U);
    }

    std::array<std::uint8_t, 128> registers;
    std::vector<std::uint8_t> fifo;
    std::vector<Transaction> transactions;
    bool failNext;
    bool ignorePowerWrites;
    int failWriteAddress;
    std::size_t nvmBusyReads;
};

inline Adxl355::Configuration synchronizedConfiguration() {
    return Adxl355::Configuration::externalSynchronized4kHz();
}

}  // namespace Adxl355Test

#endif
