module CubeSpec {

  topology Adxl355SevenSensorTopology {

    instance adxl355App

    instance adxl355Manager0
    instance adxl355Manager1
    instance adxl355Manager2
    instance adxl355Manager3
    instance adxl355Manager4
    instance adxl355Manager5
    instance adxl355Manager6

    instance adxl355Spi0
    instance adxl355Spi1
    instance adxl355Spi2
    instance adxl355Spi3
    instance adxl355Spi4
    instance adxl355Spi5
    instance adxl355Spi6

    connections Adxl355Configuration {
      adxl355App.configureSensor[0] -> adxl355Manager0.configure
      adxl355App.configureSensor[1] -> adxl355Manager1.configure
      adxl355App.configureSensor[2] -> adxl355Manager2.configure
      adxl355App.configureSensor[3] -> adxl355Manager3.configure
      adxl355App.configureSensor[4] -> adxl355Manager4.configure
      adxl355App.configureSensor[5] -> adxl355Manager5.configure
      adxl355App.configureSensor[6] -> adxl355Manager6.configure
    }

    connections Adxl355Acquisition {
      adxl355App.readSensor[0] -> adxl355Manager0.readAcceleration
      adxl355App.readSensor[1] -> adxl355Manager1.readAcceleration
      adxl355App.readSensor[2] -> adxl355Manager2.readAcceleration
      adxl355App.readSensor[3] -> adxl355Manager3.readAcceleration
      adxl355App.readSensor[4] -> adxl355Manager4.readAcceleration
      adxl355App.readSensor[5] -> adxl355Manager5.readAcceleration
      adxl355App.readSensor[6] -> adxl355Manager6.readAcceleration
    }

    connections Adxl355Spi {
      adxl355Manager0.spi -> adxl355Spi0.SpiWriteRead
      adxl355Manager1.spi -> adxl355Spi1.SpiWriteRead
      adxl355Manager2.spi -> adxl355Spi2.SpiWriteRead
      adxl355Manager3.spi -> adxl355Spi3.SpiWriteRead
      adxl355Manager4.spi -> adxl355Spi4.SpiWriteRead
      adxl355Manager5.spi -> adxl355Spi5.SpiWriteRead
      adxl355Manager6.spi -> adxl355Spi6.SpiWriteRead
    }

    # Add these connections in the containing deployment, using its concrete
    # FPGA timing source and frame consumer:
    # fpgaSync.sampleReady -> adxl355App.acquireFrame
    # adxl355App.frameReady -> accelerationPipeline.frameIn

  }

}

