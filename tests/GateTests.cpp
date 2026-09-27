#include "TestHarness.hpp"
#include "TestSupport.hpp"

#include "Adxl355/Device/AccessGate.hpp"

#include <atomic>
#include <cstdint>
#include <thread>

ADXL355_TEST(TryAcquireIsExclusiveUntilRelease) {
    Adxl355::AccessGate gate;
    EXPECT_TRUE(gate.tryAcquire());
    EXPECT_FALSE(gate.tryAcquire());
    gate.release();
    EXPECT_TRUE(gate.tryAcquire());
    gate.release();
}

ADXL355_TEST(ControlAcquireRetriesWithABoundedDelay) {
    Adxl355::AccessGate gate;
    Adxl355Test::RecordingDelay delay;

    EXPECT_TRUE(gate.acquire(delay, 3U));
    EXPECT_TRUE(delay.calls.empty());
    EXPECT_FALSE(gate.acquire(delay, 3U));
    EXPECT_EQ(static_cast<std::size_t>(2), delay.calls.size());
    EXPECT_EQ(static_cast<std::uint32_t>(1), delay.calls[0]);
    EXPECT_FALSE(gate.acquire(delay, 0U));
    gate.release();
}

ADXL355_TEST(GateHoldReleasesOnlyWhatItAcquired) {
    Adxl355::AccessGate gate;
    {
        Adxl355::GateHold hold(gate, gate.tryAcquire());
        EXPECT_TRUE(hold.held());
        Adxl355::GateHold rejected(gate, gate.tryAcquire());
        EXPECT_FALSE(rejected.held());
    }
    EXPECT_TRUE(gate.tryAcquire());
    gate.release();
}

ADXL355_TEST(ConcurrentOwnersNeverOverlap) {
    Adxl355::AccessGate gate;
    std::atomic<int> inside(0);
    std::atomic<bool> overlap(false);
    std::atomic<std::uint32_t> entries(0U);

    auto worker = [&]() {
        for (std::uint32_t iteration = 0U; iteration < 200000U; ++iteration) {
            if (!gate.tryAcquire()) {
                continue;
            }
            if (inside.fetch_add(1) != 0) {
                overlap.store(true);
            }
            entries.fetch_add(1U);
            inside.fetch_sub(1);
            gate.release();
        }
    };
    std::thread first(worker);
    std::thread second(worker);
    first.join();
    second.join();

    EXPECT_FALSE(overlap.load());
    EXPECT_TRUE(entries.load() > 0U);
    EXPECT_TRUE(gate.tryAcquire());
}

int main() {
    return Adxl355Test::runAll();
}
