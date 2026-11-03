# Store multiple data

This project collects environmental data from sensors connected to a Raspberry Pi and stores it in a MySQL database.

## Objective

The software periodically acquires temperature and humidity measurements and stores them in a database for later analysis, historical logging, or environmental monitoring.

## Supported sensors

The code in this repository mainly handles:

- DS18B20 temperature sensor via the 1-Wire bus;
- DHT11 sensor for temperature and humidity;
- writing values to MySQL tables (`wr_temperature`, `wr_humidity`, and partially `wr_barometer`).

In the current version, the main workflow focuses on temperature and humidity, while the pressure logic is already present but commented out in the code.

## Project structure

- `store_data.cpp`: daemon version, launched as a background process;
- `store_data_nodaemon.cpp`: non-daemon version, useful for testing and debugging;
- `Kalman.h`: Kalman filter used to reduce noise in readings;
- `makefile`: compiles the project binaries.

## Build

To compile the project programs:

```bash
make
```

This produces:

- `store_data` (daemon version);
- `store_data_nodaemon` (non-daemon version).

## Requirements

To build and run the project, the following are needed:

- Raspberry Pi with GPIO access;
- `wiringPi` library;
- MySQL client and development headers (`mysql_config`);
- a local or network-accessible MySQL database;
- sensors connected as indicated in the code (DS18B20 on 1-Wire, DHT11 on GPIO 15).

## Example behavior

The program reads sensor values at regular intervals, applies a Kalman filter to the measurements, and inserts the data into the MySQL database only when the values are valid and have changed from the last relevant reading.

## Note

This repository contains a specific example of environmental monitoring based on Raspberry Pi and MySQL, with an implementation suited for embedded systems and background data acquisition scripts.
