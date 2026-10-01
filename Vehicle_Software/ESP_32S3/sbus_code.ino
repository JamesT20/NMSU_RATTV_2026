/*
  ESP32-S3 SBUS Reader
  Receiver: RadioLink R9DS
  SBUS signal connected to GPIO 18

  Connections:
  R9DS SBUS/S.BUS  -> ESP32-S3 GPIO 18
  R9DS GND         -> ESP32-S3 GND

  IMPORTANT:
  Make sure the receiver's signal voltage is compatible
  with the ESP32-S3's 3.3V GPIO.
*/

#include <Arduino.h>

#define SBUS_RX_PIN 18

HardwareSerial SBUS(1);

uint8_t sbusData[25];
uint16_t channels[16];

bool failsafe = false;
bool lostFrame = false;

// ------------------------------------------------------
// Decode a complete 25-byte SBUS frame
// ------------------------------------------------------
void decodeSBUS(uint8_t *data) {

  channels[0]  = ((data[1]       | data[2]  << 8)                         & 0x07FF);
  channels[1]  = ((data[2] >> 3  | data[3]  << 5)                         & 0x07FF);
  channels[2]  = ((data[3] >> 6  | data[4]  << 2 | data[5] << 10)         & 0x07FF);
  channels[3]  = ((data[5] >> 1  | data[6]  << 7)                         & 0x07FF);
  channels[4]  = ((data[6] >> 4  | data[7]  << 4)                         & 0x07FF);
  channels[5]  = ((data[7] >> 7  | data[8]  << 1 | data[9] << 9)          & 0x07FF);
  channels[6]  = ((data[9] >> 2  | data[10] << 6)                         & 0x07FF);
  channels[7]  = ((data[10] >> 5 | data[11] << 3)                         & 0x07FF);

  channels[8]  = ((data[12]      | data[13] << 8)                         & 0x07FF);
  channels[9]  = ((data[13] >> 3 | data[14] << 5)                         & 0x07FF);
  channels[10] = ((data[14] >> 6 | data[15] << 2 | data[16] << 10)        & 0x07FF);
  channels[11] = ((data[16] >> 1 | data[17] << 7)                         & 0x07FF);
  channels[12] = ((data[17] >> 4 | data[18] << 4)                         & 0x07FF);
  channels[13] = ((data[18] >> 7 | data[19] << 1 | data[20] << 9)         & 0x07FF);
  channels[14] = ((data[20] >> 2 | data[21] << 6)                         & 0x07FF);
  channels[15] = ((data[21] >> 5 | data[22] << 3)                         & 0x07FF);

  // Byte 23 contains SBUS status flags
  lostFrame = data[23] & (1 << 2);
  failsafe  = data[23] & (1 << 3);
}


// ------------------------------------------------------
// Look for and receive an SBUS frame
// ------------------------------------------------------
bool readSBUS() {

  static uint8_t index = 0;

  while (SBUS.available()) {

    uint8_t incomingByte = SBUS.read();

    // Wait for SBUS start byte
    if (index == 0) {
      if (incomingByte != 0x0F) {
        continue;
      }
    }

    sbusData[index++] = incomingByte;

    // Complete SBUS frame received
    if (index == 25) {
      index = 0;

      decodeSBUS(sbusData);

      return true;
    }
  }

  return false;
}


// ------------------------------------------------------
// Setup
// ------------------------------------------------------
void setup() {

  Serial.begin(115200);

  delay(1000);

  Serial.println();
  Serial.println("ESP32-S3 R9DS SBUS Reader");
  Serial.println("--------------------------");

  /*
    SBUS:
    100000 baud
    8 data bits
    Even parity
    2 stop bits

    Last argument = true
    enables RX inversion.
  */

  SBUS.begin(
    100000,
    SERIAL_8E2,
    SBUS_RX_PIN,
    -1,
    true
  );

  Serial.println("Waiting for SBUS...");
}


// ------------------------------------------------------
// Main Loop
// ------------------------------------------------------
void loop() {

  if (readSBUS()) {

    Serial.print("CH: ");

    for (int i = 0; i < 16; i++) {

      Serial.print(i + 1);
      Serial.print("=");

      Serial.print(channels[i]);

      if (i < 15) {
        Serial.print(" | ");
      }
    }

    Serial.print(" | Lost:");
    Serial.print(lostFrame);

    Serial.print(" | Failsafe:");
    Serial.println(failsafe);
  }
}