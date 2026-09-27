#ifndef ADXL355_DEVICE_REGISTERS_HPP
#define ADXL355_DEVICE_REGISTERS_HPP

#include <cstddef>
#include <cstdint>

namespace Adxl355 {
namespace Registers {

// Register map and bit definitions follow ADXL354/ADXL355 data sheet Rev. D,
// Table 15 and Tables 16 through 51.
static const std::uint8_t DEVID_AD = 0x00U;
static const std::uint8_t DEVID_MST = 0x01U;
static const std::uint8_t PARTID = 0x02U;
static const std::uint8_t REVID = 0x03U;
static const std::uint8_t STATUS = 0x04U;
static const std::uint8_t FIFO_ENTRIES = 0x05U;
static const std::uint8_t TEMP2 = 0x06U;
static const std::uint8_t XDATA3 = 0x08U;
static const std::uint8_t FIFO_DATA = 0x11U;
static const std::uint8_t OFFSET_X_H = 0x1EU;
static const std::uint8_t OFFSET_Y_H = 0x20U;
static const std::uint8_t OFFSET_Z_H = 0x22U;
static const std::uint8_t ACT_EN = 0x24U;
static const std::uint8_t ACT_THRESH_H = 0x25U;
static const std::uint8_t ACT_COUNT = 0x27U;
static const std::uint8_t FILTER = 0x28U;
static const std::uint8_t FIFO_SAMPLES = 0x29U;
static const std::uint8_t INT_MAP = 0x2AU;
static const std::uint8_t SYNC = 0x2BU;
static const std::uint8_t RANGE = 0x2CU;
static const std::uint8_t POWER_CTL = 0x2DU;
static const std::uint8_t SELF_TEST = 0x2EU;
static const std::uint8_t RESET = 0x2FU;

// Factory-trim shadow registers used by the data sheet's software-reset NVM
// load check (Rev. D, Reset register description).
static const std::uint8_t SHADOW_START = 0x50U;
static const std::size_t SHADOW_LENGTH = 5U;

static const std::uint8_t EXPECTED_DEVID_AD = 0xADU;
static const std::uint8_t EXPECTED_DEVID_MST = 0x1DU;
static const std::uint8_t EXPECTED_PARTID = 0xEDU;
static const std::uint8_t RESET_CODE = 0x52U;

static const std::uint8_t STATUS_DATA_READY = 0x01U;
static const std::uint8_t STATUS_FIFO_FULL = 0x02U;
static const std::uint8_t STATUS_FIFO_OVERRUN = 0x04U;
static const std::uint8_t STATUS_ACTIVITY = 0x08U;
static const std::uint8_t STATUS_NVM_BUSY = 0x10U;
static const std::uint8_t STATUS_RESERVED = 0xE0U;

static const std::uint8_t POWER_STANDBY = 0x01U;
static const std::uint8_t POWER_TEMP_OFF = 0x02U;
static const std::uint8_t POWER_DRDY_OFF = 0x04U;

static const std::uint8_t SYNC_EXTERNAL_CLOCK = 0x04U;
static const std::uint8_t RANGE_MASK = 0x03U;
static const std::uint8_t RANGE_INTERRUPT_POLARITY = 0x40U;
static const std::uint8_t RANGE_I2C_HIGH_SPEED = 0x80U;

static const std::uint8_t SELF_TEST_MODE = 0x01U;
static const std::uint8_t SELF_TEST_FORCE = 0x02U;

static const std::uint8_t INT_MAP_INT2 = 0xF0U;
static const std::size_t FIFO_MAX_ENTRIES = 96U;

static const std::size_t MAX_REGISTER_TRANSFER = 288U;
static const std::size_t MAX_SPI_TRANSACTION = MAX_REGISTER_TRANSFER + 1U;

inline std::uint8_t readCommand(std::uint8_t address) {
    return static_cast<std::uint8_t>((address << 1U) | 0x01U);
}

inline std::uint8_t writeCommand(std::uint8_t address) {
    return static_cast<std::uint8_t>(address << 1U);
}

}  // namespace Registers
}  // namespace Adxl355

#endif
