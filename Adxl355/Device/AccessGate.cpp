#include "Adxl355/Device/AccessGate.hpp"

namespace Adxl355 {

// A gate that silently falls back to a lock would reintroduce blocking on the
// acquisition path; require a genuinely lock-free flag on every target.
static_assert(ATOMIC_BOOL_LOCK_FREE == 2, "AccessGate requires a lock-free std::atomic<bool>");

AccessGate::AccessGate() : m_busy(false) {}

bool AccessGate::tryAcquire() {
    bool expected = false;
    return this->m_busy.compare_exchange_strong(expected, true);
}

bool AccessGate::acquire(Delay& delay, std::uint32_t attempts) {
    for (std::uint32_t attempt = 0U; attempt < attempts; ++attempt) {
        if (this->tryAcquire()) {
            return true;
        }
        if ((attempt + 1U) < attempts) {
            delay.delayMilliseconds(1U);
        }
    }
    return false;
}

void AccessGate::release() {
    this->m_busy.store(false);
}

GateHold::GateHold(AccessGate& gate, bool acquired) : m_gate(gate), m_held(acquired) {}

GateHold::~GateHold() {
    if (this->m_held) {
        this->m_gate.release();
    }
}

bool GateHold::held() const {
    return this->m_held;
}

}  // namespace Adxl355
