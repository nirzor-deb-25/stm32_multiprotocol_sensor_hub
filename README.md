# STM32 Multi-Protocol Sensor Hub

A bare-metal sensor hub for the STM32F407G-DISC1 that combines:

- BME280 temperature measurements over software I2C.
- Onboard LIS3DSH acceleration measurements over hardware SPI1.
- Combined reports over hardware USART2.
- A NUCLEO-F439ZI UART bridge connecting the reports to a Windows PC.

Written in C using direct register access, without STM32 HAL or FreeRTOS.

## Hardware

- STM32F407G-DISC1.
- Olimex MOD-BME280 Rev.B.
- NUCLEO-F439ZI.
- Two USB data cables.
- Jumper wires.

Both boards use their own ST-LINK USB connections for power and programming.

## Wiring

Disconnect USB power from both boards before changing wires.

### BME280 to Discovery

The colours below refer to the wires used in this build. Follow the module's printed signal labels if your colours differ.

| MOD-BME280 signal | Wire colour | Discovery pin |
|---|---|---|
| GND | Orange | GND |
| VDD | Yellow | 3V |
| SDA | Brown | PB9 |
| SCL | Red | PB8 |

Use the Discovery's 3 V supply. Do not connect the module to 5 V.

The module's large UEXT connector is unused.

The tested sensor responds at the seven-bit address 0x77 and returns chip ID 0x60 from register 0xD0.

### UART bridge connections

| NUCLEO-F439ZI | Discovery STM32F407G-DISC1 | Purpose |
|---|---|---|
| PC10, UART4 TX | PA3, USART2 RX | Reverse path reserved; unused by this application |
| PC11, UART4 RX | PA2, USART2 TX | Sensor reports to PC |
| GND | GND | Common signal reference |

Do not connect the boards' 3V/3V3 or 5V supply pins together.

The Discovery application transmits reports only. It does not implement UART commands or enable USART2 reception.

### Onboard accelerometer

No additional wiring is needed for the LIS3DSH:

| Signal | Discovery pin |
|---|---|
| Chip select | PE3 |
| SPI1 clock | PA5 |
| SPI1 MISO | PA6 |
| SPI1 MOSI | PA7 |

## Architecture

The Discovery runs a sequential main loop:

1. Initialize or retry the BME280 as needed.
2. Trigger and read a temperature measurement.
3. Initialize or retry the accelerometer as needed.
4. Check acceleration data readiness and read X/Y/Z.
5. Format and transmit one combined UART report.
6. Toggle blue LD6 and wait 500 ms.

Each sensor is handled independently. An error from one sensor does not intentionally skip the other sensor's update.

The reporting interval includes sensor transactions, conversion waiting, UART transmission, and the 500 ms delay. It is not an exact fixed-frequency acquisition schedule.

## Driver details

### Software I2C

Despite its original i2c_probe filename, this module provides address probing, single-register reads, and single-register writes.

- PB8 is SCL; PB9 is SDA.
- GPIO outputs use open-drain configuration.
- High signals are released rather than actively driven high.
- Internal pull-ups supplement module pull-ups.
- Nominal half-bit delay is 10 microseconds, plus software overhead.
- SCL-high waiting has a 2 ms timeout.
- Register reads use a repeated START and a final NACK.
- The hardware I2C peripheral is not used.
- No nine-clock bus-recovery sequence is implemented.

### BME280 temperature

- Address: 0x77.
- Chip ID: 0x60.
- Factory temperature calibration is read from the sensor.
- Temperature oversampling: x1.
- Pressure and humidity measurements: disabled.
- IIR filter: disabled.
- Forced mode triggers a fresh measurement for each update.
- Conversion is followed by a 10 ms wait and bounded status polling.
- Integer compensation uses 64-bit intermediates.
- Temperature is stored in hundredths of a degree Celsius.

The BME280 measures its sensor temperature, which can be influenced by handling, board heating, and its surroundings. No comparison against a calibrated reference thermometer was performed.

### LIS3DSH acceleration

- Hardware SPI1, mode 3.
- Nominal SPI clock: 500 kHz with the 16 MHz CPU clock.
- Expected device ID: 0x3F.
- Range: +/-2 g.
- Sensor output data rate: 100 Hz.
- Block data update and automatic register increment enabled.
- Configuration registers are read back for verification.
- Fresh data is checked before a seven-byte SPI transaction reads all axes.
- Acceleration is converted to mg: 1000 mg = 1 g.

The hub reads much less frequently than the sensor's 100 Hz output rate. It reports recent samples rather than capturing every accelerometer output sample.

### UART reporting

- Hardware USART2 TX on PA2.
- 115200 baud, eight data bits, no parity, one stop bit.
- No flow control.
- Polled transmission.
- 100 ms timeout for a complete write.
- No DMA or UART interrupts in the Discovery application.

The NUCLEO bridge uses USART3 for its ST-LINK virtual COM port and UART4 for the connection to Discovery. It uses interrupt-driven reception and polled transmission.

The tested PC port was COM7 for the NUCLEO. Discovery also appeared as COM5, but COM5 is not the reporting path used here.

## Report format

Example captured report:

    HUB seq=272 T=26.29C temp_valid=1 X=207 Y=52 Z=1010 mg accel_valid=1 temp_err=0 accel_err=0 tx_timeout=0

| Field | Meaning |
|---|---|
| seq | Completed main-loop count |
| T | Stored temperature in degrees Celsius |
| temp_valid | Fresh temperature measurement succeeded in this loop |
| X, Y, Z | Stored acceleration in mg |
| accel_valid | Fresh acceleration measurement succeeded in this loop |
| temp_err | Cumulative temperature initialization or measurement errors |
| accel_err | Cumulative accelerometer initialization or transfer errors |
| tx_timeout | Cumulative UART transmitter timeouts |

A validity flag of zero means the stored reading must not be treated as a successful fresh measurement for that report.

Stored readings remain available after errors. For example, T=0.00C with temp_valid=0 after startup is not evidence of a real zero-degree measurement.

Acceleration data-not-ready events are counted separately in debugger diagnostics.

Reports contain measurements taken sequentially, not simultaneous samples.

## Build tools

Tested using Windows PowerShell with:

- GNU Arm tools supplied with STM32CubeIDE.
- GNU Make.
- STM32CubeProgrammer CLI.
- Git.

No external kernel or Git submodule is required.

## Build and flash

From the project root:

    . .\setup.ps1
    make -j4 --output-sync=target

Flash the Discovery application:

    make flash

Build and flash the NUCLEO bridge:

    make -C Bridge -j4 --output-sync=target
    make -C Bridge flash

The Makefiles select different boards by ST-LINK serial number:

| Firmware | Tested ST-LINK serial |
|---|---|
| Discovery sensor hub | 0671FF495170535067222342 |
| NUCLEO UART bridge | 066EFF555380535067043227 |

Update the settings for your own boards.

List connected probes and virtual COM ports:

    STM32_Programmer_CLI.exe -l

Discovery outputs:

    build/stm32_multiprotocol_sensor_hub.elf
    build/stm32_multiprotocol_sensor_hub.bin
    build/stm32_multiprotocol_sensor_hub.hex

Bridge outputs are under:

    build/bridge/

Root make clean removes both sets of build outputs.

## Read reports on Windows

Keep both boards powered and connected through the UART wiring.

Display ten reports:

    .\Tools\read_hub.ps1 -Port COM7 -Samples 10 |
        Format-Table -AutoSize

Require valid readings, zero reported errors, and consecutive sequence numbers:

    .\Tools\read_hub.ps1 -Port COM7 -Samples 10 -RequireHealthy |
        Format-Table -AutoSize

Export structured reports to CSV:

    .\Tools\read_hub.ps1 -Port COM7 -Samples 100 |
        Export-Csv -NoTypeInformation -Encoding utf8 .\hub_capture.csv

CSV export is supported by the reader's structured output but was not separately demonstrated during the recorded tests.

The reader:

- Uses 115200 baud and LF-terminated lines.
- Discards a potentially incomplete first line.
- Parses signed acceleration and temperature.
- Allows limited resynchronization after malformed lines.
- Closes and disposes the serial port after reading or on failure.

RequireHealthy treats historical nonzero error counters as unhealthy, even after sensor communication recovers.

Sequence discontinuities can indicate missed reports or a device restart. Sequence numbers alone do not prove reliable delivery.

## Debugger diagnostics

Find current addresses from the ELF matching the installed firmware:

    arm-none-eabi-nm -n .\build\stm32_multiprotocol_sensor_hub.elf |
        Select-String 'hub_diagnostics$|hub_uart_diagnostics$|hub_report_errors$'

Addresses can change after modifying source or build settings.

hub_diagnostics contains nineteen consecutive 32-bit words:

| Word | Field |
|---:|---|
| 1 | temperature_initialized |
| 2 | temperature_result |
| 3 | temperature_i2c_result |
| 4 | temperature_count |
| 5 | temperature_errors |
| 6 | temperature_raw |
| 7 | temperature_centi_c, signed |
| 8 | temperature_valid |
| 9 | accelerometer_id |
| 10 | accelerometer_initialized |
| 11 | accelerometer_spi_result |
| 12 | accelerometer_count |
| 13 | accelerometer_errors |
| 14 | accelerometer_not_ready |
| 15 | x_mg, signed |
| 16 | y_mg, signed |
| 17 | z_mg, signed |
| 18 | accelerometer_valid |
| 19 | loop_count |

hub_uart_diagnostics records completed writes, bytes queued into the UART, and timeouts. It does not acknowledge PC reception.

hub_report_errors counts formatting failures and failed UART writes.

Live memory reads are not atomic snapshots. Validity flags can briefly be zero while a measurement is underway.

## Recorded hardware verification

### Sensor identification and temperature

- Address 0x76 returned NACK.
- Address 0x77 acknowledged.
- Chip ID read as 0x60.
- One identification capture showed 59 successful checks and zero errors.
- Temperature-only captures included 23.79 and 23.78 degrees Celsius.
- Successful temperature sample counts increased from 99 to 108 with zero errors.
- The initial handling experiment did not establish a clear warming response.

### Combined I2C and SPI operation

- Both sensor sample counts increased.
- Communication errors were zero in the successful captures.
- A tilted Discovery reading showed X=-903, Y=37, Z=448 mg.
- The corresponding temperature was 23.94 degrees Celsius.

This demonstrated a substantial orientation response from the accelerometer while temperature acquisition continued.

### UART integration and troubleshooting

An initial UART capture showed:

- Valid acceleration reports.
- Invalid temperature reports.
- Increasing temperature errors.

Debugger diagnostics identified an I2C NACK. Communication resumed after connection checks. The precise physical cause was not established.

A later fresh-run capture contained sequences 272 through 281 with:

- Temperatures from 26.27 to 26.31 degrees Celsius.
- Both sensor validity flags set.
- Zero temperature errors.
- Zero acceleration errors.
- Zero UART timeouts.

### Reset and power-cycle checks

After an explicit Discovery reset through ST-LINK, the reader accepted ten consecutive reports, sequences 26 through 35, with both sensors valid and all reported error counters zero.

Following the power-cycle step, another capture contained sequences 22 through 31, temperatures from 25.53 to 25.58 degrees Celsius, both sensors valid, and zero reported errors.

These checks validate the observed startup and reporting conditions. They are not exhaustive reliability or accuracy tests.

## Limitations

- Software I2C, not the STM32 hardware I2C controller.
- Blocking, sequential sensor acquisition and UART transmission.
- No RTOS, DMA, timestamp synchronization, or command parser.
- No pressure or humidity reporting.
- No calibrated temperature-accuracy test.
- No precise external measurement of bus or reporting timing.
- No UART delivery acknowledgement or report checksum.
- No persistent configuration or diagnostic counters across reset.
- No exhaustive fault injection, overflow, or long-duration testing.
- A failed temperature conversion can add delay before the accelerometer update.
- Zero observed error counts apply only to the recorded captures.

This is a learning and portfolio project demonstrating I2C, SPI, and UART integration.

## References

- Bosch BME280 datasheet: https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bme280-ds002.pdf
- Olimex MOD-BME280: https://www.olimex.com/Products/Modules/Sensors/MOD-BME280/
- STM32F407G-DISC1: https://www.st.com/en/evaluation-tools/stm32f407g-disc1.html
- NUCLEO-F439ZI: https://www.st.com/en/evaluation-tools/nucleo-f439zi.html
