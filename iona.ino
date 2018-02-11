// Copyright 2018 Takashi Toyoshima <toyoshim@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "JVSIO.h"

JVSIO io;

static const char io_id[] = "TOYOSHIMA-HOUSE;IONA-AN;ver0.9;experiments";
uint8_t ios[3] = { 0x00, 0x00, 0x00 };
uint8_t coinCount = 0;
uint8_t mode = 0;
uint8_t coin = 0;

int in(int pin, int shift) {
  return (digitalRead(pin) ? 0 : 1) << shift;
}

uint8_t readMode() {
  int value = analogRead(A7);
  if (value < 170)
    return 0;
  if (value < 420)
    return 1;
  if (value < 570)
    return 2;
  return 3;
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
    mode = readMode();
    // Update IO pins.
    // Common SW: TEST TILT1 TILT2 TILT3 UND UND UND UND
    ios[0] = in(12, 7);
    // START SERVICE UP DOWN LEFT RIGHT PUSH1 PISH2
    ios[1] = in(A2, 7) | in(4, 5) | in(5, 4) | in(6, 3) | in(7, 2) | in(8, 1) | in(9, 0);
      // PUSH3 PUSH4 PUSH5 PUSH6 (PUSH7) (PUSH8) UND UND
    ios[2] = in(10, 7) | in(11, 6) | in(A1, 5) | in(A0, 4);

    // Update coin
    uint8_t newCoin = digitalRead(A3);
    if (!coin && newCoin)
      coinCount++;
    coin = newCoin;
    return;
  }
  switch (*data) {
   case JVSIO::kCmdIoId:
    io.pushReport(JVSIO::kReportOk);
    for (int i = 0; io_id[i]; ++i)
      io.pushReport(io_id[i]);
    io.pushReport(0);
    break;
   case JVSIO::kCmdFunctionCheck:
    io.pushReport(JVSIO::kReportOk);

    io.pushReport(0x01);  // sw
    io.pushReport(0x01);  // player
    io.pushReport(0x0C);  // buttons
    io.pushReport(0x00);

    io.pushReport(0x02);  // coin
    io.pushReport(0x01);  // slot
    io.pushReport(0x00);
    io.pushReport(0x00);

    io.pushReport(0x00);
    break;
   case JVSIO::kCmdSwInput:
    if (data[1] != 1 || data[2] != 2) {
     io.pushReport(JVSIO::kReportParamErrorNoResponse);
     Serial.println("sw error");
    } else {
      io.pushReport(JVSIO::kReportOk);
      io.pushReport(ios[0]);
      io.pushReport(ios[1]);
      io.pushReport(ios[2]);
    }
    break;
   case JVSIO::kCmdCoinInput:
    io.pushReport(JVSIO::kReportOk);
    for (size_t slot = 0; slot < data[1]; ++slot) {
      io.pushReport((0 << 6) | 0);
      io.pushReport(coinCount);
    }
    break;
   case JVSIO::kCmdCoinSub:
    coin -= data[3];
    io.pushReport(JVSIO::kReportOk);
    break;
  }
}
