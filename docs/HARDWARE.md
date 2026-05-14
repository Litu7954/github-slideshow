# Hardware Design Documentation

## ESP32 Multi-Protocol Signal Injector & Testing Device

### Block Diagram

```
                    ┌─────────────────────────────────────────┐
                    │           ESP32-WROOM-32                │
                    │                                         │
   USB ─────────── │  UART0 (Debug/Programming)              │
                    │                                         │
   UART ─────────  │  GPIO16 (RX1) ─┐                       │
   RS-232 ───────  │  GPIO17 (TX1) ─┤  UART1                │
   RS-485 ───────  │  GPIO4  (DE/RE)│  + Transceivers       │
                    │                                         │
   SPI Device ──── │  GPIO23 (MOSI) ┐                       │
                    │  GPIO19 (MISO) ├─ SPI (VSPI)          │
                    │  GPIO18 (SCLK) │                       │
                    │  GPIO5  (CS0)  ┘                       │
                    │                                         │
   I2C Bus ─────── │  GPIO21 (SDA)  ┐  I2C Master/Slave    │
                    │  GPIO22 (SCL)  ┘                       │
                    │                                         │
   CAN Bus ─────── │  GPIO25 (TX)   ┐  TWAI Controller     │
                    │  GPIO26 (RX)   ┘  + Transceiver       │
                    │                                         │
   PWM Outputs ─── │  GPIO27 (CH0)  ┐                       │
                    │  GPIO14 (CH1)  ├─ LEDC PWM            │
                    │  GPIO12 (CH2)  │                       │
                    │  GPIO13 (CH3)  ┘                       │
                    │                                         │
   GPIO I/O ────── │  GPIO32, 33 (Output)                   │
                    │  GPIO34, 35, 36, 39 (Input Only)       │
                    │                                         │
   DAC Outputs ─── │  GPIO25 (DAC1) ─ Waveform Gen         │
                    │  GPIO26 (DAC2)                         │
                    │                                         │
   ADC Inputs ──── │  GPIO36 (ADC1_CH0) ┐ Signal Analysis  │
                    │  GPIO39 (ADC1_CH3) ├─ 12-bit ADC      │
                    │  GPIO34 (ADC1_CH6) │                   │
                    │  GPIO35 (ADC1_CH7) ┘                   │
                    │                                         │
   Status ──────── │  GPIO2  (Built-in LED)                 │
                    │  GPIO15 (Activity LED)                  │
                    │                                         │
   WiFi ────────── │  802.11 b/g/n  (AP Mode)              │
                    │  SSID: SignalInjector                   │
                    │  Web UI: http://192.168.4.1            │
                    └─────────────────────────────────────────┘
```

### Pin Assignment Table

| Pin   | Function          | Direction | Notes                              |
|-------|-------------------|-----------|-------------------------------------|
| GPIO0 | Boot Button       | Input     | Boot mode selection                 |
| GPIO1 | UART2 TX          | Output    | Secondary UART                      |
| GPIO2 | Status LED        | Output    | Built-in LED                        |
| GPIO3 | UART2 RX          | Input     | Secondary UART                      |
| GPIO4 | RS-485 DE/RE      | Output    | Driver/Receiver Enable              |
| GPIO5 | SPI CS0           | Output    | Chip Select                         |
| GPIO12| PWM CH2           | Output    | LEDC Channel 2                      |
| GPIO13| PWM CH3           | Output    | LEDC Channel 3                      |
| GPIO14| PWM CH1           | Output    | LEDC Channel 1                      |
| GPIO15| Activity LED      | Output    | External LED                        |
| GPIO16| UART1 RX          | Input     | Primary UART / RS-232 / RS-485 RX   |
| GPIO17| UART1 TX          | Output    | Primary UART / RS-232 / RS-485 TX   |
| GPIO18| SPI SCLK          | Output    | SPI Clock                           |
| GPIO19| SPI MISO          | Input     | SPI Master In Slave Out             |
| GPIO21| I2C SDA           | I/O       | I2C Data (with pull-up)             |
| GPIO22| I2C SCL           | Output    | I2C Clock (with pull-up)            |
| GPIO23| SPI MOSI          | Output    | SPI Master Out Slave In             |
| GPIO25| CAN TX / DAC1     | Output    | Shared: CAN or DAC output           |
| GPIO26| CAN RX / DAC2     | Input     | Shared: CAN or DAC output           |
| GPIO27| PWM CH0           | Output    | LEDC Channel 0                      |
| GPIO32| Digital Output 0  | Output    | General GPIO                        |
| GPIO33| Digital Output 1  | Output    | General GPIO                        |
| GPIO34| Digital Input 0   | Input     | Input only (no pull-up)             |
| GPIO35| Digital Input 1   | Input     | Input only (no pull-up)             |
| GPIO36| ADC CH0 / Input   | Input     | Analog input (VP), Input only       |
| GPIO39| ADC CH1 / Input   | Input     | Analog input (VN), Input only       |

### External Transceiver Circuits

#### RS-232 Interface (MAX3232)

```
ESP32 GPIO17 (TX) ──── T1IN  ┌──────────┐ T1OUT ──── RS-232 TX
ESP32 GPIO16 (RX) ──── R1OUT │  MAX3232  │ R1IN  ──── RS-232 RX
                       VCC   │          │ GND
                        │    └──────────┘  │
                       3.3V    + 4× 100nF  GND
```

#### RS-485 Interface (MAX485)

```
ESP32 GPIO17 (TX) ──── DI    ┌──────────┐ A ──────── RS-485 A (+)
ESP32 GPIO16 (RX) ──── RO    │  MAX485  │ B ──────── RS-485 B (-)
ESP32 GPIO4  (DE) ──── DE    │          │
ESP32 GPIO4  (RE) ──── /RE   └──────────┘
                              VCC    GND
                               │      │
                              3.3V   GND
```

#### CAN Bus Interface (SN65HVD230 / MCP2551)

```
ESP32 GPIO25 (TX) ──── TXD   ┌──────────┐ CANH ──── CAN High
ESP32 GPIO26 (RX) ──── RXD   │SN65HVD230│ CANL ──── CAN Low
                       VCC   │          │ GND
                        │    └──────────┘  │
                       3.3V               GND
                                    120Ω termination
                               CANH ─┤├── CANL
```

### Bill of Materials (BOM)

| Qty | Component              | Value/Part      | Package    | Notes                    |
|-----|------------------------|-----------------|------------|--------------------------|
| 1   | ESP32 Dev Board        | ESP32-WROOM-32  | Module     | Main MCU                 |
| 1   | RS-232 Transceiver     | MAX3232          | SOIC-16   | RS-232 level shifting    |
| 1   | RS-485 Transceiver     | MAX485 / SP485   | SOIC-8    | Half-duplex RS-485       |
| 1   | CAN Transceiver        | SN65HVD230       | SOIC-8    | 3.3V CAN transceiver     |
| 4   | Capacitors             | 100nF            | 0603      | MAX3232 charge pump      |
| 2   | Capacitors             | 10µF             | 0805      | Power decoupling         |
| 6   | Capacitors             | 100nF            | 0603      | IC decoupling            |
| 2   | Pull-up Resistors      | 4.7kΩ            | 0603      | I2C SDA/SCL              |
| 1   | Termination Resistor   | 120Ω             | 0805      | CAN bus termination      |
| 2   | LEDs                   | Green/Blue       | 0603      | Status/Activity           |
| 2   | Current Limit Resistors| 330Ω             | 0603      | LED current limiting     |
| 1   | USB Micro-B Connector  | -                | SMD       | Power & programming      |
| 2   | DB9 Connectors         | Male/Female      | Through   | RS-232 interface         |
| 1   | 2-pos Terminal Block   | 5.08mm pitch     | Through   | RS-485 A/B               |
| 1   | 2-pos Terminal Block   | 5.08mm pitch     | Through   | CAN H/L                  |
| 1   | Pin Headers            | 2.54mm pitch     | Through   | GPIO/SPI/I2C breakout    |
| 1   | PCB                    | 80×60mm          | 2-layer   | Custom PCB               |

### Power Requirements

| Parameter       | Value          |
|-----------------|----------------|
| Supply Voltage  | 5V (USB)       |
| Operating       | 3.3V (regulated)|
| Current (idle)  | ~80 mA         |
| Current (active)| ~200 mA        |
| Current (WiFi)  | ~300 mA peak   |

### Schematic Notes

1. **Power**: USB 5V → ESP32 onboard LDO → 3.3V for all ICs
2. **I2C**: External 4.7kΩ pull-ups on SDA/SCL to 3.3V
3. **CAN**: 120Ω termination resistor between CANH and CANL
4. **RS-485**: DE and /RE tied together to GPIO4 for direction control
5. **RS-232**: MAX3232 charge pump capacitors (4× 100nF) required
6. **ADC**: Input voltage range 0-3.3V, add voltage dividers for higher voltages
7. **DAC**: 8-bit output (0-3.3V), add op-amp buffer for driving loads

### Shared Pin Considerations

- **GPIO25/26**: Shared between CAN transceiver and DAC outputs
  - Use CAN when CAN protocol is selected
  - Use DAC when waveform generation is active
  - Cannot use both simultaneously
- **GPIO16/17**: Shared between UART1, RS-232, and RS-485
  - RS-232 uses MAX3232 transceiver (active when RS-232 selected)
  - RS-485 uses MAX485 transceiver (active when RS-485 selected)
  - Only one serial protocol can be active at a time
