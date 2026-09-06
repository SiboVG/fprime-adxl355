module CubeSpecRpi {

  # Example base IDs only. Allocate nonoverlapping IDs from the deployment's
  # authoritative ID plan before copying these instances.
  instance adxl355App: Adxl355.SynchronizedApplication base id 0x4000
  instance adxl355Manager0: Adxl355.Manager base id 0x4100
  instance adxl355Manager1: Adxl355.Manager base id 0x4200

  instance adxl355Spi0: Drv.LinuxSpiDriver base id 0x5000 {
    phase Fpp.ToCpp.Phases.configComponents """
    {
      const bool status = CubeSpecRpi::adxl355Spi0.open(0, 0, Drv::SPI_FREQUENCY_10MHZ,
                                                        Drv::SPI_MODE_CPOL_LOW_CPHA_LOW);
      if (!status) {
        Fw::Logger::log("[ERROR] Could not open ADXL355 SPI device 0.0\\n");
        Init::status = false;
      }
    }
    """
  }

  instance adxl355Spi1: Drv.LinuxSpiDriver base id 0x5100 {
    phase Fpp.ToCpp.Phases.configComponents """
    {
      const bool status = CubeSpecRpi::adxl355Spi1.open(0, 1, Drv::SPI_FREQUENCY_10MHZ,
                                                        Drv::SPI_MODE_CPOL_LOW_CPHA_LOW);
      if (!status) {
        Fw::Logger::log("[ERROR] Could not open ADXL355 SPI device 0.1\\n");
        Init::status = false;
      }
    }
    """
  }

}
