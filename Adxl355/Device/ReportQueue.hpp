#ifndef ADXL355_DEVICE_REPORT_QUEUE_HPP
#define ADXL355_DEVICE_REPORT_QUEUE_HPP

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace Adxl355 {

// A queue that silently fell back to a lock would block the acquisition path.
static_assert((ATOMIC_INT_LOCK_FREE == 2) && (ATOMIC_LONG_LOCK_FREE == 2),
              "ReportQueue requires lock-free integer atomics");

//! Fixed-capacity queue from one producer to one consumer that never waits and
//! never allocates. The acquisition path records fault edges here instead of
//! emitting events and telemetry, which cost about 140 us per edge on the
//! TE0715 bench (four normal reads); a rate-group port publishes them. Calls
//! on the producer side must not overlap each other (the manager's and
//! application's gates order them), and neither may calls on the consumer
//! side. Record must be trivially copyable.
template <typename Record, std::size_t Capacity>
class ReportQueue {
    static_assert((Capacity != 0U) && ((Capacity & (Capacity - 1U)) == 0U),
                  "capacity must be a power of two, so the indices can wrap");

  public:
    ReportQueue() : m_records(), m_head(0U), m_tail(0U), m_dropped(0U) {}
    ReportQueue(const ReportQueue&) = delete;
    ReportQueue& operator=(const ReportQueue&) = delete;

    //! Producer: append a record. When the queue is full the record is not
    //! stored, counts as dropped, and false is returned.
    bool push(const Record& record) {
        const std::size_t head = this->m_head.load(std::memory_order_relaxed);
        if ((head - this->m_tail.load(std::memory_order_acquire)) >= Capacity) {
            this->m_dropped.fetch_add(1U, std::memory_order_relaxed);
            return false;
        }
        this->m_records[head & (Capacity - 1U)] = record;
        this->m_head.store(head + 1U, std::memory_order_release);
        return true;
    }

    //! Consumer: remove the oldest record; false when the queue is empty.
    bool pop(Record& record) {
        const std::size_t tail = this->m_tail.load(std::memory_order_relaxed);
        if (tail == this->m_head.load(std::memory_order_acquire)) {
            return false;
        }
        record = this->m_records[tail & (Capacity - 1U)];
        this->m_tail.store(tail + 1U, std::memory_order_release);
        return true;
    }

    //! Consumer: records dropped since the previous call.
    std::uint32_t takeDropped() {
        return this->m_dropped.exchange(0U, std::memory_order_relaxed);
    }

    static std::size_t capacity() {
        return Capacity;
    }

  private:
    Record m_records[Capacity];
    std::atomic<std::size_t> m_head;
    std::atomic<std::size_t> m_tail;
    std::atomic<std::uint32_t> m_dropped;
};

}  // namespace Adxl355

#endif
