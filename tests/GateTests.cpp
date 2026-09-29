#include "TestHarness.hpp"
#include "TestSupport.hpp"

#include "Adxl355/Device/AccessGate.hpp"
#include "Adxl355/Device/ReportQueue.hpp"

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

ADXL355_TEST(ReportQueueKeepsOrderAndDropsWhenFull) {
    Adxl355::ReportQueue<std::uint32_t, 4U> queue;
    std::uint32_t value = 0U;
    EXPECT_FALSE(queue.pop(value));
    for (std::uint32_t item = 1U; item <= 4U; ++item) {
        EXPECT_TRUE(queue.push(item));
    }
    EXPECT_FALSE(queue.push(5U));
    EXPECT_FALSE(queue.push(6U));
    EXPECT_EQ(static_cast<std::uint32_t>(2), queue.takeDropped());
    EXPECT_EQ(static_cast<std::uint32_t>(0), queue.takeDropped());
    for (std::uint32_t item = 1U; item <= 4U; ++item) {
        EXPECT_TRUE(queue.pop(value));
        EXPECT_EQ(item, value);
    }
    EXPECT_FALSE(queue.pop(value));
    EXPECT_EQ(static_cast<std::size_t>(4), queue.capacity());
}

ADXL355_TEST(ReportQueueWrapsAroundItsStorage) {
    Adxl355::ReportQueue<std::uint32_t, 4U> queue;
    std::uint32_t value = 0U;
    for (std::uint32_t item = 0U; item < 1000U; ++item) {
        EXPECT_TRUE(queue.push(item));
        if ((item % 3U) == 2U) {
            EXPECT_TRUE(queue.push(item + 100000U));
            EXPECT_TRUE(queue.pop(value));
            EXPECT_EQ(item, value);
            EXPECT_TRUE(queue.pop(value));
            EXPECT_EQ(item + 100000U, value);
        } else {
            EXPECT_TRUE(queue.pop(value));
            EXPECT_EQ(item, value);
        }
    }
    EXPECT_FALSE(queue.pop(value));
    EXPECT_EQ(static_cast<std::uint32_t>(0), queue.takeDropped());
}

ADXL355_TEST(ReportQueueHandsEveryRecordAcrossThreadsInOrder) {
    // One producer and one consumer on separate threads: every record either
    // arrives, in order and intact, or is counted as dropped.
    struct Record {
        std::uint32_t sequence;
        std::uint32_t check;
    };
    Adxl355::ReportQueue<Record, 16U> queue;
    const std::uint32_t count = 200000U;
    std::atomic<bool> done(false);
    std::atomic<bool> corrupt(false);
    std::uint32_t received = 0U;

    std::thread consumer([&]() {
        std::uint32_t next = 0U;
        Record record = {0U, 0U};
        for (;;) {
            const bool finished = done.load();
            while (queue.pop(record)) {
                if ((record.check != ~record.sequence) || (record.sequence < next)) {
                    corrupt.store(true);
                }
                next = record.sequence + 1U;
                ++received;
            }
            if (finished) {
                break;
            }
        }
    });
    std::uint32_t dropped = 0U;
    for (std::uint32_t sequence = 0U; sequence < count; ++sequence) {
        if (!queue.push(Record{sequence, ~sequence})) {
            ++dropped;
        }
    }
    done.store(true);
    consumer.join();

    EXPECT_FALSE(corrupt.load());
    EXPECT_EQ(count, received + dropped);
    EXPECT_EQ(dropped, queue.takeDropped());
    EXPECT_TRUE(received > 0U);
}

int main() {
    return Adxl355Test::runAll();
}
