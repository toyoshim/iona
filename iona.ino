// Copyright 2018 Takashi Toyoshima <toyoshim@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "JVSIO.h"

JVSIO io;

// Some NAOMI games expects the first segment starts with "SEGA ENTERPRISES,LTD.".
// E.g. one major official I/O board is "SEGA ENTERPRISES,LTD.;I/O 838-13683B;Ver1.07;99/16".
static const char io_id[] = "SEGA ENTERPRISES,LTD.compat;IONA-NANO;ver0.94;Normal Mode";
static const char suchipai_id[] = "SEGA ENTERPRISES,LTD.compat;IONA-NANO;Ver0.94;Su Chi Pai Mode";
static const char virtualon_id[] = "SEGA ENTERPRISES,LTD.compat;IONA-NANO;Ver0.94;Virtual-On Mode";
uint8_t ios[5] = { 0x00, 0x00, 0x00, 0x00, 0x00 };
uint8_t coinCount = 0;
uint8_t mode = 0;
uint8_t coin = 0;
uint8_t gpout = 0;

bool suchipai_mode = false;
bool virtualon_mode = false;

int in(int pin, int shift) {
  return (digitalRead(pin) ? 0 : 1) << shift;
}

void updateMode() {
  int value = analogRead(A7);
  if (value < 170)       // SW1-OFF, SW2-OFF
    mode = 0;
  else if (value < 420)  // SW1-OFF, SW2-ON
    mode = 1;
  else if (value < 570)  // SW1-ON , SW2-OFF
    mode = 2;
  else
    mode = 3;

  suchipai_mode = mode == 2;
  virtualon_mode = mode == 1;
}

uint8_t suchipaiReport() {
  // OUT  |  B7 |  B6 |  B5 |  B4 |  B3 |  B2 |  B1 |  B0 |
  // -----+-----+-----+-----+-----+-----+-----+-----+-----+
  // 0x40 |  A  |  -  |  E  |  I  |  M  | KAN |  -  |  -  |
  // 0x20 |  B  |  -  |  F  |  J  |  N  |REACH|START|  -  |
  // 0x10 |  C  |  -  |  G  |  K  | CHI | RON |  -  |  -  |
  // 0x80 |  D  |  -  |  H  |  L  | PON |  -  |  -  |  -  |

  // Emulated by D-pad + 4 buttons.
  uint8_t start = (ios[1] & 0x80) ? 2 : 0;
  if ((gpout == 0x40 && !(ios[1] & 0x02)) ||
      (gpout == 0x20 && !(ios[1] & 0x01)) ||
      (gpout == 0x10 && !(ios[2] & 0x80)) ||
      (gpout == 0x80 && !(ios[2] & 0x40))) {
    return start;
  }
  switch (ios[1] & 0x2c) {
   case 0x00:
    return 0x00 | start;
   case 0x04:
    return 0x04 | start;
   case 0x08:
    return 0x80 | start;
   case 0x20:
    return 0x10 | start;
   case 0x24:
    return 0x08 | start;
   case 0x28:
    return 0x20 | start;
  }
  return start;
}

uint8_t virtualonReport(size_t player, size_t line) {
  //       |  B7 |  B6 |  B5 |  B4 |  B3 |  B2 |  B1 |  B0 |
  // ------+-----+-----+-----+-----+-----+-----+-----+-----+
  // P0-L1 |Start|  -  | L U | L D | L L | L R |L sht|L trb|
  // P0-L2 | QM  |  -  |  -  |  -  |  -  |  -  |  -  |  -  |
  // P1-L1 |  -  |  -  | R U | R D | R L | R R |R sht|R trb|
  // P1-L2 |  -  |  -  |  -  |  -  |  -  |  -  |  -  |  -  |

  // Try fullfilling 2 stick controlls avobe via usual single
  // arcade controller that has only 1 stick and 4 buttons.
  // Button 1 - L shot
  // Button 2 - turn or jump w/ stick
  // Button 3 - LR turbo
  // Button 4 - R shot
  if (player >= 2 || line != 1)
    return 0x00;
  uint8_t data = 0x00;
  bool rotate = ios[1] & 1;
  if (rotate) {
    bool up = ios[1] & 32;
    bool down = ios[1] & 16;
    bool left = ios[1] & 8;
    bool right = ios[1] & 4;
    if (player == 0) { // Left Stick
      if (left) data |= 32;
      if (right) data |= 16;
      if (up) data |= 8;
      if (down) data |= 4;
    } else {  // Right Stick
      if (left) data |= 16;
      if (right) data |= 32;
      if (up) data |= 4;
      if (down) data |= 8;
    }
  } else {
    data = ios[1] & ~3;
  }
  if (player == 0 && ios[1] & 2)
    data |= 2;
  if (player == 1 && ios[2] & 64)
    data |= 2;
  if (ios[2] & 128)
    data |= 1;
  return data;
}

void setup() {
  io.begin();

  int pins[] = { 4, 5, 6, 7, 8, 9, 10, 11, 12, A0, A1, A2, A3, A4, A5 };
  for (int i = 0; i < sizeof(pins); ++i)
    pinMode(pins[i], INPUT_PULLUP);
}

void loop() {
  uint8_t len;
  uint8_t* data = io.getNextCommand(&len);
  if (!data) {
    updateMode();

    // Update IO pins.
    // Common SW: TEST TILT1 TILT2 TILT3 UND UND UND UND
    ios[0] = in(12, 7);
    // START SERVICE UP DOWN LEFT RIGHT PUSH1 PISH2
    ios[1] = in(A2, 7) | in(4, 5) | in(5, 4) | in(6, 3) | in(7, 2) | in(8, 1) | in(9, 0);
      // PUSH3 PUSH4 PUSH5 PUSH6 (PUSH7) (PUSH8) UND UND
    ios[2] = in(10, 7) | in(11, 6) | in(A1, 5) | in(A0, 4);

    // Update coin
    uint8_t newCoin = digitalRead(A3);
    if (coin && !newCoin)
      coinCount++;
    coin = newCoin;
    return;
  }
  switch (*data) {
   case JVSIO::kCmdIoId:
    io.pushReport(JVSIO::kReportOk);
    {
      const char* id = virtualon_mode ? virtualon_id : suchipai_mode ? suchipai_id : io_id;
      for (size_t i = 0; id[i]; ++i)
        io.pushReport(id[i]);
    }
    io.pushReport(0);

    // Initialize.
    coinCount = 0;
    break;
   case JVSIO::kCmdFunctionCheck:
    io.pushReport(JVSIO::kReportOk);

    io.pushReport(0x01);  // sw
    io.pushReport(0x02);  // players
    io.pushReport(0x0C);  // buttons
    io.pushReport(0x00);

    io.pushReport(0x03);  // analog inputs
    io.pushReport(0x08);  // channels
    io.pushReport(0x00);  // bits
    io.pushReport(0x00);

    io.pushReport(0x12);  // general purpose driver
    io.pushReport(0x08);  // slots
    io.pushReport(0x00);
    io.pushReport(0x00);

    io.pushReport(0x02);  // coin
    io.pushReport(0x02);  // slots
    io.pushReport(0x00);
    io.pushReport(0x00);

    io.pushReport(0x00);
    break;
   case JVSIO::kCmdSwInput:
    io.pushReport(JVSIO::kReportOk);
    io.pushReport(ios[0]);
    for (size_t player = 0; player < data[1]; ++player) {
      for (size_t line = 1; line <= data[2]; ++line) {
        if (virtualon_mode)
          io.pushReport(virtualonReport(player, line));
        else if (player)
          io.pushReport(0x00);
        else if (suchipai_mode)
          io.pushReport(suchipaiReport());
        else
          io.pushReport(line < sizeof(ios) ? ios[line] : 0x00);
      }
    }
    break;
   case JVSIO::kCmdCoinInput:
    io.pushReport(JVSIO::kReportOk);
    for (size_t slot = 0; slot < data[1]; ++slot) {
      io.pushReport((0 << 6) | 0);
      io.pushReport(slot ? 0x00 : coinCount);
    }
    break;
   case JVSIO::kCmdAnalogInput:
    io.pushReport(JVSIO::kReportOk);
    for (size_t channel = 0; channel < data[1]; ++channel) {
      io.pushReport(0x80);
      io.pushReport(0x00);
    }
    break;
   case JVSIO::kCmdCoinSub:
    if (!data[1])
      coinCount -= data[3];
    io.pushReport(JVSIO::kReportOk);
    break;
   case JVSIO::kCmdDriverOutput:
    gpout = data[2];
    io.pushReport(JVSIO::kReportOk);
    break;
  }
}
