#include "TestHarness.hpp"
#include "TestSupport.hpp"

#include "Adxl355/Array/SynchronizedArray.hpp"

#include <cstddef>
#include <cstdint>

ADXL355_TEST(SevenSensorsInitializeAndProduceOneCoherentFrame) {
    Adxl355Test::RegisterSpi spi[Adxl355::SynchronizedArray::MAX_SENSORS];
    Adxl355Test::RecordingDelay delay[Adxl355::SynchronizedArray::MAX_SENSORS];
    Adxl355::Device devices[Adxl355::SynchronizedArray::MAX_SENSORS] = {
        Adxl355::Device(spi[0], delay[0]), Adxl355::Device(spi[1], delay[1]),
        Adxl355::Device(spi[2], delay[2]), Adxl355::Device(spi[3], delay[3]),
        Adxl355::Device(spi[4], delay[4]), Adxl355::Device(spi[5], delay[5]),
        Adxl355::Device(spi[6], delay[6]),
    };
    Adxl355::SynchronizedArray array;

    for (std::size_t i = 0; i < Adxl355::SynchronizedArray::MAX_SENSORS; ++i) {
        EXPECT_EQ(Adxl355::Status::OK, array.add(devices[i]));
    }

    std::uint8_t initializedMask = 0U;
    EXPECT_EQ(Adxl355::Status::OK,
              array.initializeAll(Adxl355Test::synchronizedConfiguration(), initializedMask));
    EXPECT_EQ(static_cast<std::uint8_t>(0x7F), initializedMask);
    for (std::size_t i = 0; i < Adxl355::SynchronizedArray::MAX_SENSORS; ++i) {
        spi[i].setRawSample(static_cast<std::int32_t>(i + 1U),
                            -static_cast<std::int32_t>(i + 1U),
                            static_cast<std::int32_t>((i + 1U) * 10U));
    }

    Adxl355::SynchronizedFrame frame;
    EXPECT_EQ(Adxl355::Status::OK, array.readFrame(123456789ULL, frame));
    EXPECT_EQ(static_cast<std::uint64_t>(0), frame.sequence);
    EXPECT_EQ(static_cast<std::uint64_t>(123456789ULL), frame.syncTimeMicroseconds);
    EXPECT_EQ(static_cast<std::uint8_t>(0x7F), frame.validMask);
    EXPECT_EQ(static_cast<std::size_t>(7), frame.sensorCount);
    EXPECT_EQ(static_cast<std::int32_t>(1), frame.samples[0].raw.x);
    EXPECT_EQ(static_cast<std::int32_t>(7), frame.samples[6].raw.x);
    EXPECT_EQ(static_cast<std::int32_t>(70), frame.samples[6].raw.z);

    EXPECT_EQ(Adxl355::Status::OK, array.readFrame(123457039ULL, frame));
    EXPECT_EQ(static_cast<std::uint64_t>(1), frame.sequence);
}

ADXL355_TEST(OneFailedSensorProducesPartialFrameWithoutHidingOtherSamples) {
    Adxl355Test::RegisterSpi spi[3];
    Adxl355Test::RecordingDelay delay[3];
    Adxl355::Device first(spi[0], delay[0]);
    Adxl355::Device second(spi[1], delay[1]);
    Adxl355::Device third(spi[2], delay[2]);
    Adxl355::SynchronizedArray array;
    EXPECT_EQ(Adxl355::Status::OK, array.add(first));
    EXPECT_EQ(Adxl355::Status::OK, array.add(second));
    EXPECT_EQ(Adxl355::Status::OK, array.add(third));

    std::uint8_t initializedMask = 0U;
    EXPECT_EQ(Adxl355::Status::OK, array.initializeAll(Adxl355::Configuration(), initializedMask));

    spi[0].setRawSample(10, 11, 12);
    spi[1].setRawSample(20, 21, 22);
    spi[2].setRawSample(30, 31, 32);
    spi[1].failNext = true;

    Adxl355::SynchronizedFrame frame;
    EXPECT_EQ(Adxl355::Status::PARTIAL, array.readFrame(99U, frame));
    EXPECT_EQ(static_cast<std::uint8_t>(0x05), frame.validMask);
    EXPECT_EQ(Adxl355::Status::SPI_ERROR, frame.sensorStatus[1]);
    EXPECT_EQ(static_cast<std::int32_t>(10), frame.samples[0].raw.x);
    EXPECT_EQ(static_cast<std::int32_t>(30), frame.samples[2].raw.x);
}

ADXL355_TEST(ArrayRejectsAnEighthSensor) {
    Adxl355Test::RegisterSpi spi[8];
    Adxl355Test::RecordingDelay delay[8];
    Adxl355::Device devices[8] = {
        Adxl355::Device(spi[0], delay[0]), Adxl355::Device(spi[1], delay[1]),
        Adxl355::Device(spi[2], delay[2]), Adxl355::Device(spi[3], delay[3]),
        Adxl355::Device(spi[4], delay[4]), Adxl355::Device(spi[5], delay[5]),
        Adxl355::Device(spi[6], delay[6]), Adxl355::Device(spi[7], delay[7]),
    };
    Adxl355::SynchronizedArray array;

    for (std::size_t i = 0; i < 7U; ++i) {
        EXPECT_EQ(Adxl355::Status::OK, array.add(devices[i]));
    }
    EXPECT_EQ(Adxl355::Status::CAPACITY_EXCEEDED, array.add(devices[7]));
}

ADXL355_TEST(ArrayRejectsInvalidDeviceStatesWithoutHidingHealthyPeer) {
    Adxl355Test::RegisterSpi spi[2];
    Adxl355Test::RecordingDelay delay[2];
    Adxl355::Device invalid(spi[0], delay[0]), healthy(spi[1], delay[1]);
    Adxl355::SynchronizedArray array;
    array.add(invalid);
    array.add(healthy);
    EXPECT_EQ(Adxl355::Status::OK, healthy.initialize(Adxl355::Configuration()));
    spi[1].setRawSample(42, 0, 0);
    for (int state = 0; state < 3; ++state) {
        if (state == 1) {
            EXPECT_EQ(Adxl355::Status::OK, invalid.initialize(Adxl355::Configuration()));
            EXPECT_EQ(Adxl355::Status::OK, invalid.stopMeasurement());
        } else if (state == 2) {
            EXPECT_EQ(Adxl355::Status::OK, invalid.startMeasurement());
            spi[0].failWriteAddress = 0x2C;
            EXPECT_EQ(Adxl355::Status::SPI_ERROR, invalid.setRange(Adxl355::Range::G8));
        }
        const std::size_t before = spi[0].transactions.size();
        Adxl355::SynchronizedFrame frame;
        EXPECT_EQ(Adxl355::Status::PARTIAL, array.readFrame(99U, frame));
        EXPECT_EQ(static_cast<std::uint8_t>(2), frame.validMask);
        EXPECT_EQ(Adxl355::Status::INVALID_STATE, frame.sensorStatus[0]);
        EXPECT_EQ(42, frame.samples[1].raw.x);
        EXPECT_EQ(before, spi[0].transactions.size());
    }
}

int main() {
    return Adxl355Test::runAll();
}
