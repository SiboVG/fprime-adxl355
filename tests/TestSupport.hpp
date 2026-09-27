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

//! Register-level ADXL355 emulator. Behavior follows data sheet Rev. D:
//! writable bit masks from Tables 15-51, DATA_RDY/FIFO_OVR/activity cleared by
//! a STATUS read, shadow trim registers 0x50-0x54 reloaded by a software reset.
class RegisterSpi final : public Adxl355::SpiBus {
  public:
    enum class Miso { DRIVEN, STUCK_LOW, STUCK_HIGH };

    RegisterSpi()
        : registers(),
          fifo(),
          transactions(),
          temperatureReads(),
          miso(Miso::DRIVEN),
          failNext(false),
          ignorePowerWrites(false),
          failWriteAddress(-1),
          failWriteOccurrence(1),
          failReadAddress(-1),
          ignoreWriteAddress(-1),
          nvmBusyReads(0),
          corruptShadowResets(0),
          resets(0) {
        powerOnDefaults();
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
            receive[i] = (miso == Miso::STUCK_HIGH) ? 0xFFU : 0x00U;
        }
        if (miso != Miso::DRIVEN) {
            return true;
        }

        const std::uint8_t address = static_cast<std::uint8_t>(transmit[0] >> 1U);
        const bool isRead = (transmit[0] & 0x01U) != 0U;
        if (isRead) {
            if (static_cast<int>(address) == failReadAddress) {
                failReadAddress = -1;
                return false;
            }
            serviceRead(address, receive, size);
            return true;
        }
        if (static_cast<int>(address) == failWriteAddress) {
            if (--failWriteOccurrence <= 0) {
                failWriteAddress = -1;
                failWriteOccurrence = 1;
                return false;
            }
        }
        if (((address == 0x2DU) && ignorePowerWrites) || (static_cast<int>(address) == ignoreWriteAddress)) {
            return true;
        }
        serviceWrite(address, transmit, size);
        return true;
    }

    //! A new conversion result becomes available and sets DATA_RDY.
    void setRawSample(std::int32_t x, std::int32_t y, std::int32_t z) {
        encodeDirect(x, &registers[0x08]);
        encodeDirect(y, &registers[0x0B]);
        encodeDirect(z, &registers[0x0E]);
        registers[0x04] = static_cast<std::uint8_t>(registers[0x04] | 0x01U);
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

    //! Counts transactions whose command byte addresses `address`.
    std::size_t transactionsTo(std::uint8_t address, bool read) const {
        std::size_t count = 0U;
        for (std::size_t i = 0; i < transactions.size(); ++i) {
            const std::uint8_t command = transactions[i].transmit[0];
            if (((command >> 1U) == address) && (((command & 0x01U) != 0U) == read)) {
                ++count;
            }
        }
        return count;
    }

    //! Factory trim contents the shadow registers hold after a correct NVM load.
    static std::uint8_t shadowTruth(std::size_t index) {
        static const std::uint8_t truth[5] = {0x3CU, 0x81U, 0x07U, 0xE2U, 0x19U};
        return truth[index];
    }

    std::array<std::uint8_t, 128> registers;
    std::vector<std::uint8_t> fifo;
    std::vector<Transaction> transactions;
    //! Successive TEMP2/TEMP1 burst reads return these values first.
    std::vector<std::uint16_t> temperatureReads;
    Miso miso;
    bool failNext;
    bool ignorePowerWrites;
    int failWriteAddress;
    int failWriteOccurrence;
    int failReadAddress;
    int ignoreWriteAddress;
    std::size_t nvmBusyReads;
    std::size_t corruptShadowResets;
    std::size_t resets;

  private:
    static std::uint8_t writableMask(std::uint8_t address) {
        switch (address) {
            case 0x1E: case 0x1F: case 0x20: case 0x21: case 0x22: case 0x23:
            case 0x25: case 0x26: case 0x27: case 0x2A:
                return 0xFFU;
            case 0x24:
                return 0x07U;
            case 0x28: case 0x29:
                return 0x7FU;
            case 0x2B: case 0x2D:
                return 0x07U;
            case 0x2C:
                return 0xC3U;
            case 0x2E:
                return 0x03U;
            default:
                return 0x00U;
        }
    }

    void powerOnDefaults() {
        registers.fill(0U);
        registers[0x00] = 0xAD;
        registers[0x01] = 0x1D;
        registers[0x02] = 0xED;
        registers[0x03] = 0x01;
        registers[0x27] = 0x01;
        registers[0x29] = 0x60;
        registers[0x2C] = 0x81;
        registers[0x2D] = 0x01;
        for (std::size_t i = 0; i < 5U; ++i) {
            registers[0x50 + i] = shadowTruth(i);
        }
    }

    void serviceRead(std::uint8_t address, std::uint8_t* receive, std::size_t size) {
        if (address == 0x11U) {
            for (std::size_t i = 1; i < size; ++i) {
                receive[i] = (i - 1U < fifo.size()) ? fifo[i - 1U] : 0x02U;
            }
            return;
        }
        if ((address == 0x06U) && !temperatureReads.empty()) {
            setTemperature(temperatureReads.front());
            temperatureReads.erase(temperatureReads.begin());
        }
        const std::size_t last = static_cast<std::size_t>(address) + size - 2U;
        for (std::size_t i = 1; i < size; ++i) {
            receive[i] = registers.at(static_cast<std::size_t>(address) + i - 1U);
        }
        if ((address <= 0x04U) && (last >= 0x04U)) {
            const std::size_t statusIndex = 1U + (0x04U - address);
            if (nvmBusyReads > 0U) {
                receive[statusIndex] = static_cast<std::uint8_t>(receive[statusIndex] | 0x10U);
                --nvmBusyReads;
            }
            // DATA_RDY, FIFO_OVR, and activity clear on a STATUS read.
            registers[0x04] = static_cast<std::uint8_t>(registers[0x04] & ~0x0DU);
        }
    }

    void serviceWrite(std::uint8_t address, const std::uint8_t* transmit, std::size_t size) {
        for (std::size_t i = 1; i < size; ++i) {
            const std::uint8_t target = static_cast<std::uint8_t>(address + i - 1U);
            const std::uint8_t mask = writableMask(target);
            registers.at(target) = static_cast<std::uint8_t>((registers.at(target) & ~mask) | (transmit[i] & mask));
        }
        if ((address == 0x2FU) && (transmit[1] == 0x52U)) {
            ++resets;
            powerOnDefaults();
            if (corruptShadowResets > 0U) {
                registers[0x52] = static_cast<std::uint8_t>(registers[0x52] ^ 0x5AU);
                --corruptShadowResets;
            }
        }
    }
};

inline Adxl355::Configuration synchronizedConfiguration() {
    return Adxl355::Configuration::externalSynchronized4kHz();
}

}  // namespace Adxl355Test

#endif
