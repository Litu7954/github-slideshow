# REST API Reference

## Base URL
`http://192.168.4.1` (ESP32 Access Point default)

---

## Status

### `GET /api/status`
Returns current device status.

**Response:**
```json
{
  "device": "ESP32 Signal Injector",
  "version": "1.0.0",
  "mode": 0,
  "protocol": 1,
  "protocol_name": "UART",
  "injector_running": false,
  "analyzer_capturing": false,
  "fault_active": false,
  "tester_running": false,
  "packets_sent": 0
}
```

**Modes:** 0=Idle, 1=Inject, 2=Analyze, 3=Fault, 4=Loopback, 5=BER, 6=Bridge, 7=SigGen

---

## Protocol Selection

### `POST /api/protocol`
Select and initialize a protocol driver.

**Request:**
```json
{ "protocol": 1 }
```

**Protocol IDs:** 1=UART, 2=SPI, 3=I2C, 4=RS-232, 5=RS-485, 6=CAN, 8=Modbus RTU, 11=PWM, 12=GPIO

---

## Signal Injection

### `POST /api/inject`

| Action | Description | Parameters |
|--------|-------------|------------|
| 0 | Start continuous injection | - |
| 1 | Stop injection | - |
| 2 | Send test pattern | `pattern` (0=walking 1s, 1=PRBS, 2=counter), `count` |
| 3 | Start waveform | `type` (1-6), `freq`, `amp` |
| 4 | Stop waveform | - |

**Waveform types:** 1=Sine, 2=Square, 3=Triangle, 4=Sawtooth, 5=Noise, 6=Pulse

---

## Signal Analysis

### `POST /api/analyze`

| Action | Description | Parameters |
|--------|-------------|------------|
| 0 | Start capture | `duration` (ms) |
| 1 | Stop capture | - |
| 2 | Get captured data | - |
| 3 | Scan I2C bus | - |

**Capture response:**
```json
{
  "stats": { "total": 100, "valid": 98, "errors": 2, "duration_ms": 5000, "avg_rate": 20.0 },
  "frames": [
    { "proto": "UART", "ts": 123456, "text": "UART RX [8 bytes]: 01 02 03 ...", "err": false }
  ]
}
```

---

## Fault Injection

### `POST /api/fault`

| Action | Description | Parameters |
|--------|-------------|------------|
| 0 | Start fault injection | - |
| 1 | Stop fault injection | - |
| 2 | Add bit flip fault | `prob` (0.0-1.0) |
| 3 | Add delay fault | `delay` (microseconds) |
| 4 | Add packet drop fault | `rate` (0.0-1.0) |
| 5 | Get fault statistics | - |
| 6 | Clear all rules/stats | - |

---

## Testing

### `POST /api/test`

| Action | Description | Parameters |
|--------|-------------|------------|
| 0 | Run loopback test | `packets`, `size` |
| 1 | Run BER test | `duration` (seconds) |
| 2 | Stop test | - |
| 3 | Get progress | - |

**Loopback response:**
```json
{
  "protocol": "UART",
  "total_packets": 100,
  "error_packets": 0,
  "dropped_packets": 0,
  "bit_error_rate": 0.0,
  "throughput_bps": 115200.0,
  "latency": { "min_us": 50, "max_us": 200, "avg_us": 100 },
  "duration_ms": 2000
}
```

**BER response:**
```json
{
  "bits_sent": 1000000,
  "bits_received": 999990,
  "errors": 5,
  "ber": 5.0e-06,
  "elapsed": 10.5
}
```
