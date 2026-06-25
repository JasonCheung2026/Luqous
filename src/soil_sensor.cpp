#include "soil_sensor.h"

static bool g_soilPresent = false;
static unsigned long g_lastAbsentProbeMs = 0;
static const uint32_t SOIL_ABSENT_PROBE_MS = 30000;

static void rs485SetTransmitMode(bool transmit) {
  digitalWrite(SOIL_RS485_DE_RE_PIN, transmit ? HIGH : LOW);
}

static uint16_t modbusCrc16(const uint8_t* data, size_t len) {
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < len; ++i) {
    crc ^= data[i];
    for (uint8_t bit = 0; bit < 8; ++bit) {
      if (crc & 1) {
        crc = static_cast<uint16_t>((crc >> 1) ^ 0xA001);
      } else {
        crc >>= 1;
      }
    }
  }
  return crc;
}

static void flushRx() {
  while (Serial1.available() > 0) {
    Serial1.read();
  }
}

static bool readModbusHoldingRegisters(uint8_t slaveAddr,
                                      uint16_t startReg,
                                      uint16_t regCount,
                                      uint16_t* outRegs,
                                      size_t outLen) {
  if (outLen < regCount) {
    return false;
  }

  uint8_t request[8];
  request[0] = slaveAddr;
  request[1] = 0x03;
  request[2] = static_cast<uint8_t>(startReg >> 8);
  request[3] = static_cast<uint8_t>(startReg & 0xFF);
  request[4] = static_cast<uint8_t>(regCount >> 8);
  request[5] = static_cast<uint8_t>(regCount & 0xFF);
  const uint16_t reqCrc = modbusCrc16(request, 6);
  request[6] = static_cast<uint8_t>(reqCrc & 0xFF);
  request[7] = static_cast<uint8_t>(reqCrc >> 8);

  flushRx();
  rs485SetTransmitMode(true);
  Serial1.write(request, sizeof(request));
  Serial1.flush();
  delay(2);
  rs485SetTransmitMode(false);

  const size_t expectedLen = 5 + (regCount * 2);
  uint8_t response[32];
  size_t received = 0;
  const unsigned long deadline = millis() + 500;

  while (received < expectedLen && millis() < deadline) {
    if (Serial1.available() > 0) {
      response[received++] = static_cast<uint8_t>(Serial1.read());
    } else {
      yield();
      delay(1);
    }
  }

  if (received < expectedLen) {
    Serial.printf("Soil sensor: Modbus timeout (%u/%u bytes)\n",
                  static_cast<unsigned>(received),
                  static_cast<unsigned>(expectedLen));
    return false;
  }

  const uint16_t respCrc = static_cast<uint16_t>(response[received - 2]) |
                         (static_cast<uint16_t>(response[received - 1]) << 8);
  if (modbusCrc16(response, received - 2) != respCrc) {
    Serial.println("Soil sensor: Modbus CRC mismatch");
    return false;
  }

  if (response[0] != slaveAddr) {
    Serial.printf("Soil sensor: unexpected slave address 0x%02X\n", response[0]);
    return false;
  }

  if (response[1] & 0x80) {
    Serial.printf("Soil sensor: Modbus exception 0x%02X\n", response[2]);
    return false;
  }

  if (response[1] != 0x03) {
    Serial.printf("Soil sensor: unexpected function code 0x%02X\n", response[1]);
    return false;
  }

  const uint8_t byteCount = response[2];
  if (byteCount != regCount * 2) {
    Serial.printf("Soil sensor: unexpected byte count %u\n", byteCount);
    return false;
  }

  for (uint16_t i = 0; i < regCount; ++i) {
    const size_t offset = 3 + (i * 2);
    outRegs[i] = static_cast<uint16_t>(
        (static_cast<uint16_t>(response[offset]) << 8) | response[offset + 1]);
  }

  return true;
}

void soilSensorInit() {
  pinMode(SOIL_RS485_DE_RE_PIN, OUTPUT);
  rs485SetTransmitMode(false);

  Serial1.begin(SOIL_BAUD, SERIAL_8N1, SOIL_UART_RX_PIN, SOIL_UART_TX_PIN);
  Serial.printf("Soil sensor: UART1 @ %u baud, TX=GPIO%u RX=GPIO%u DE/RE=GPIO%u\n",
                SOIL_BAUD, SOIL_UART_TX_PIN, SOIL_UART_RX_PIN, SOIL_RS485_DE_RE_PIN);

  SoilSensorReading probe;
  g_soilPresent = soilSensorRead(&probe);
  if (g_soilPresent) {
    Serial.println("Soil sensor: SN-3000-ECTH-N01 detected");
  } else {
    Serial.println("Soil sensor: not detected (check RS485 wiring, 24V power, A/B polarity)");
  }
}

bool soilSensorPresent() {
  return g_soilPresent;
}

bool soilSensorShouldRead() {
  if (g_soilPresent) {
    return true;
  }

  const unsigned long now = millis();
  if (now - g_lastAbsentProbeMs >= SOIL_ABSENT_PROBE_MS) {
    g_lastAbsentProbeMs = now;
    return true;
  }
  return false;
}

bool soilSensorRead(SoilSensorReading* out) {
  if (out == nullptr) {
    return false;
  }

  out->valid = false;

  uint16_t regs[3] = {0, 0, 0};
  if (!readModbusHoldingRegisters(SOIL_MODBUS_ADDR, 0x0000, 3, regs, 3)) {
    return false;
  }

  out->moisturePct  = static_cast<float>(regs[0]) / 10.0f;
  out->temperatureC = static_cast<float>(static_cast<int16_t>(regs[1])) / 10.0f;
  out->ecUsCm       = regs[2];
  out->valid        = true;

  Serial.printf("Soil sensor: moisture=%.1f %% temp=%.1f C EC=%u uS/cm\n",
                out->moisturePct, out->temperatureC, out->ecUsCm);

  g_soilPresent = true;
  return true;
}
