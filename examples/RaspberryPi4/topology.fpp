module CubeSpecRpi {

  topology Adxl355RaspberryPi4Topology {

    instance adxl355App
    instance adxl355Manager0
    instance adxl355Manager1
    instance adxl355Spi0
    instance adxl355Spi1

    connections Adxl355Configuration {
      adxl355App.configureSensor[0] -> adxl355Manager0.configure
      adxl355App.configureSensor[1] -> adxl355Manager1.configure
    }

    connections Adxl355Acquisition {
      adxl355App.readSensor[0] -> adxl355Manager0.readAcceleration
      adxl355App.readSensor[1] -> adxl355Manager1.readAcceleration
    }

    connections Adxl355Spi {
      adxl355Manager0.spi -> adxl355Spi0.SpiReadWrite
      adxl355Manager1.spi -> adxl355Spi1.SpiReadWrite
    }

    # The containing deployment supplies these mission/platform connections:
    # timingSource.sampleReady -> adxl355App.acquireFrame
    # adxl355App.frameReady -> frameConsumer.frameIn

  }

}
