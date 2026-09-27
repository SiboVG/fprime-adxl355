#ifndef ADXL355_DEVICE_ACCESS_GATE_HPP
#define ADXL355_DEVICE_ACCESS_GATE_HPP

#include "Adxl355/Device/Device.hpp"

#include <atomic>
#include <cstdint>

namespace Adxl355 {

//! Single-owner gate shared by one high-rate caller and lower-rate control
//! callers on other threads. The high-rate caller never waits: it takes the
//! gate or reports BUSY. Control callers retry with a bounded 1 ms delay.
class AccessGate {
  public:
    AccessGate();
    AccessGate(const AccessGate&) = delete;
    AccessGate& operator=(const AccessGate&) = delete;

    //! Take the gate without waiting; false when another caller owns it.
    bool tryAcquire();
    //! Take the gate, sleeping 1 ms between at most `attempts` tries.
    bool acquire(Delay& delay, std::uint32_t attempts);
    //! Release the gate. Only the current owner may call this.
    void release();

  private:
    std::atomic<bool> m_busy;
};

//! Releases an acquired gate on every return path.
class GateHold {
  public:
    GateHold(AccessGate& gate, bool acquired);
    ~GateHold();
    GateHold(const GateHold&) = delete;
    GateHold& operator=(const GateHold&) = delete;

    bool held() const;

  private:
    AccessGate& m_gate;
    const bool m_held;
};

}  // namespace Adxl355

#endif
