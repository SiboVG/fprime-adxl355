module CubeSpec {

  # Example base IDs only. Allocate nonoverlapping IDs from the deployment's
  # authoritative ID plan before copying these instances.
  instance adxl355App: Adxl355.SynchronizedApplication base id 0x4000

  instance adxl355Manager0: Adxl355.Manager base id 0x4100
  instance adxl355Manager1: Adxl355.Manager base id 0x4200
  instance adxl355Manager2: Adxl355.Manager base id 0x4300
  instance adxl355Manager3: Adxl355.Manager base id 0x4400
  instance adxl355Manager4: Adxl355.Manager base id 0x4500
  instance adxl355Manager5: Adxl355.Manager base id 0x4600
  instance adxl355Manager6: Adxl355.Manager base id 0x4700

  # Linux bring-up example. A flight deployment may replace these with its
  # Zynq/FPGA SPI driver while retaining the Drv.SpiReadWrite connections.
  instance adxl355Spi0: Drv.LinuxSpiDriver base id 0x5000
  instance adxl355Spi1: Drv.LinuxSpiDriver base id 0x5100
  instance adxl355Spi2: Drv.LinuxSpiDriver base id 0x5200
  instance adxl355Spi3: Drv.LinuxSpiDriver base id 0x5300
  instance adxl355Spi4: Drv.LinuxSpiDriver base id 0x5400
  instance adxl355Spi5: Drv.LinuxSpiDriver base id 0x5500
  instance adxl355Spi6: Drv.LinuxSpiDriver base id 0x5600

}

