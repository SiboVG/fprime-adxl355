#ifndef ADXL355_MANAGER_TESTER_HPP
#define ADXL355_MANAGER_TESTER_HPP

#include "Adxl355/Manager/Manager.hpp"
#include "Adxl355/Manager/ManagerGTestBase.hpp"
#include "tests/TestSupport.hpp"

namespace Adxl355 {

class ManagerTester final : public ManagerGTestBase {
  public:
    static const NATIVE_INT_TYPE MAX_HISTORY_SIZE = 100;
    static const NATIVE_INT_TYPE TEST_INSTANCE_ID = 0;

    ManagerTester();
    ~ManagerTester();

    void testConfigureAndRead();
    void testControlCommands();
    void testHighRateFailuresAreEdgeReported();
    void testReadBeforeInitializationIsRejected();

  private:
    void connectPorts();
    void initComponents();

    void from_spi_handler(const NATIVE_INT_TYPE portNum, Fw::Buffer& writeBuffer, Fw::Buffer& readBuffer);

    Adxl355Test::RegisterSpi m_emulator;
    Manager component;
};

}  // namespace Adxl355

#endif
