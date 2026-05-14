# ESP32 Multi-Protocol Signal Injector & Testing Device

A comprehensive, ESP32-based signal injection, analysis, and testing platform supporting multiple communication protocols. Features a built-in web UI for real-time control and monitoring.

## Features

### Supported Protocols
| Protocol | Interface | Max Speed | Notes |
|----------|-----------|-----------|-------|
| **UART** | TTL | Up to 5 Mbaud | Configurable baud, data bits, parity, stop bits |
| **SPI** | Master | Up to 40 MHz | Modes 0-3, MSB/LSB first, full/half duplex |
| **I2C** | Master/Slave | 400 kHz | 7/10-bit addressing, bus scanning |
| **RS-232** | via MAX3232 | 115200 baud | Standard DB9 levels (±12V) |
| **RS-485** | via MAX485 | 115200 baud | Half-duplex, auto DE/RE control |
| **CAN** | via SN65HVD230 | 1 Mbps | Standard/Extended frames, filters |
| **Modbus RTU** | via RS-485 | 115200 baud | Master/Slave, CRC-16 |
| **PWM** | LEDC | 40 MHz | 4 channels, 1-16 bit resolution |
| **GPIO** | Digital | - | Read/write/pulse, input/output |
| **Analog** | DAC/ADC | 100 kSps | 8-bit DAC, 12-bit ADC |

### Functional Modules

#### Signal Injection
- Protocol-specific data transmission
- Built-in test patterns (walking ones, PRBS-7/9/15/23/31, counter)
- Custom hex data injection
- Continuous or burst mode
- Configurable repeat count and interval

#### Signal Analysis & Protocol Decoding
- Real-time capture and display
- Protocol-aware frame decoding (UART, I2C, SPI, CAN, Modbus)
- Configurable triggers (level, edge, pattern)
- Frequency measurement and duty cycle analysis
- Pulse width measurement
- I2C bus scanning
- Modbus device scanning
- JSON data export

#### Waveform Generation (DAC)
- Sine, square, triangle, sawtooth, noise, pulse waveforms
- Frequency: 1 Hz - 5 kHz (via 10 kHz DAC timer)
- Amplitude and offset control
- Adjustable duty cycle (square/pulse)

#### Fault Injection
- **Bit Flip**: Random bit corruption with configurable probability
- **Delay**: Add latency to packets
- **Packet Drop**: Randomly drop packets at specified rate
- **CRC Corruption**: Corrupt checksum bytes
- **Noise Injection**: Add random noise to data
- **Glitch**: Generate voltage glitches on GPIO
- **Timing Error**: Introduce timing jitter
- Configurable probability, interval, and max trigger count

#### Loopback & BER Testing
- External loopback testing (TX→RX)
- Bit Error Rate (BER) measurement with PRBS sequences
- Latency measurement (min/max/average)
- Throughput calculation
- Stress testing with configurable load percentage
- Protocol-specific tests (UART, SPI, I2C, CAN, Modbus)
- Detailed JSON test reports

### Web-Based UI
- Single-page responsive dashboard
- Real-time status monitoring
- Protocol selection and configuration
- Signal injection controls
- Capture data viewer with protocol decoding
- Fault injection rule management
- Test result visualization
- PWM slider controls
- GPIO toggle buttons

## Hardware Requirements

### Minimum Setup
- **ESP32-WROOM-32** or **ESP32-S3** development board
- USB cable for power and programming

### Full Setup (all protocols)
- MAX3232 transceiver module (RS-232)
- MAX485 / SP485 transceiver module (RS-485)
- SN65HVD230 / MCP2551 transceiver module (CAN)
- 4.7kΩ pull-up resistors for I2C (if not on module)
- 120Ω termination resistor for CAN bus
- Breadboard / PCB and wires

See [`docs/HARDWARE.md`](docs/HARDWARE.md) for full schematics, pin mapping, and BOM.

## Getting Started

### Prerequisites
- [PlatformIO](https://platformio.org/) (recommended) or ESP-IDF v5.x
- Python 3.x (for PlatformIO)

### Build & Flash (PlatformIO)

```bash
# Clone the repository
git clone https://github.com/yourusername/esp32-signal-injector.git
cd esp32-signal-injector

# Build for ESP32
pio run -e esp32

# Flash to device
pio run -e esp32 -t upload

# Monitor serial output
pio run -e esp32 -t monitor
```

### Build & Flash (ESP-IDF)

```bash
# Set up ESP-IDF environment
. $IDF_PATH/export.sh

# Configure
idf.py set-target esp32
idf.py menuconfig

# Build and flash
idf.py build flash monitor
```

### Connect & Use

1. Power up the ESP32 board
2. Connect to WiFi network: **`SignalInjector`** (password: `signal1234`)
3. Open browser: **http://192.168.4.1**
4. Select a protocol from the sidebar
5. Use the functional panels to inject/analyze/test

## Project Structure

```
esp32-signal-injector/
├── include/
│   ├── core/
│   │   ├── config.h              # Pin assignments, constants, types
│   │   └── task_manager.h        # FreeRTOS task management
│   ├── protocols/
│   │   └── protocol_driver.h     # Protocol driver interface
│   ├── injection/
│   │   └── signal_injector.h     # Signal injection API
│   ├── analysis/
│   │   └── signal_analyzer.h     # Signal analysis API
│   ├── fault/
│   │   └── fault_injector.h      # Fault injection API
│   ├── testing/
│   │   └── loopback_tester.h     # Loopback/BER testing API
│   └── ui/
│       └── web_server.h          # Web server API
├── src/
│   ├── main.c                    # Application entry point
│   ├── core/
│   │   └── task_manager.c        # Task manager implementation
│   ├── protocols/
│   │   ├── uart_driver.c         # UART/RS-232/RS-485 driver
│   │   ├── spi_driver.c          # SPI master driver
│   │   ├── i2c_driver.c          # I2C master/slave driver
│   │   ├── can_driver.c          # CAN (TWAI) driver
│   │   ├── pwm_driver.c          # PWM/GPIO driver
│   │   ├── modbus_driver.c       # Modbus RTU driver
│   │   └── protocol_registry.c   # Driver registry
│   ├── injection/
│   │   └── signal_injector.c     # Injection engine
│   ├── analysis/
│   │   └── signal_analyzer.c     # Analyzer & decoder
│   ├── fault/
│   │   └── fault_injector.c      # Fault injection engine
│   ├── testing/
│   │   └── loopback_tester.c     # Test framework
│   └── ui/
│       └── web_server.c          # HTTP/WebSocket server
├── data/
│   └── index.html                # Web UI (embedded in flash)
├── docs/
│   ├── HARDWARE.md               # Hardware design & schematics
│   └── API.md                    # REST API reference
├── platformio.ini                # PlatformIO configuration
├── CMakeLists.txt                # ESP-IDF top-level CMake
├── main/
│   └── CMakeLists.txt            # ESP-IDF component CMake
├── partitions.csv                # Flash partition table
└── sdkconfig.defaults            # SDK configuration defaults
```

## API Reference

The device exposes a REST API for programmatic control. See [`docs/API.md`](docs/API.md) for full documentation.

### Quick Examples

```bash
# Get device status
curl http://192.168.4.1/api/status

# Select UART protocol
curl -X POST http://192.168.4.1/api/protocol -d '{"protocol":1}'

# Send PRBS test pattern
curl -X POST http://192.168.4.1/api/inject -d '{"action":2,"pattern":1,"count":10}'

# Start signal capture (5 seconds)
curl -X POST http://192.168.4.1/api/analyze -d '{"action":0,"duration":5000}'

# Run loopback test (100 packets, 32 bytes each)
curl -X POST http://192.168.4.1/api/test -d '{"action":0,"packets":100,"size":32}'

# Generate 1 kHz sine wave on DAC
curl -X POST http://192.168.4.1/api/inject -d '{"action":3,"type":1,"freq":1000,"amp":1.0}'
```

## Architecture

The firmware follows a modular architecture with clear separation of concerns:

- **Protocol Drivers**: Uniform interface (`init`, `send`, `receive`, `configure`) for all protocols
- **Injection Engine**: Pattern generation and scheduled transmission across protocols
- **Analysis Engine**: Capture, decode, and statistical analysis with protocol-aware decoders
- **Fault Injector**: Rule-based fault injection pipeline processing transmitted packets
- **Test Framework**: Loopback, BER, and stress testing with detailed metrics
- **Web UI**: Single-page application served from SPIFFS with REST API backend
- **Task Manager**: FreeRTOS-based coordination with event groups and queues

## License

MIT License - see [LICENSE](LICENSE) file.
