// Copyright 2018 Takashi Toyoshima <toyoshim@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "jvsio/clients/NanoClient.cpp"
#include "jvsio/JVSIO.cpp"

class SnesController {
 public:
  enum Player : int {
    P1 = 0,
    P2 = 1,
  };
  enum Button : int {
    B = 0,
    Y = 1,
    Select = 2,
    Start = 3,
    Up = 4,
    Down = 5,
    Left = 6,
    Right = 7,
    A = 8,
    X = 9,
    L = 10,
    R = 11,
  };
  SnesController(int clock1, int latch1, int serial1, int clock2, int latch2, int serial2)
  : clock1_(clock1), latch1_(latch1), serial1_(serial1)
  , clock2_(clock2), latch2_(latch2), serial2_(serial2) {}

  void begin() {
    pinMode(clock1_, OUTPUT);
    digitalWrite(clock1_, HIGH);
    pinMode(clock2_, OUTPUT);
    digitalWrite(clock2_, HIGH);

    pinMode(latch1_, OUTPUT);
    digitalWrite(latch1_, LOW);
    pinMode(latch2_, OUTPUT);
    digitalWrite(latch2_, LOW);

    pinMode(serial1_, INPUT_PULLUP);
    pinMode(serial2_, INPUT_PULLUP);
  }

  void update() {
    digitalWrite(latch1_, HIGH);
    digitalWrite(latch2_, HIGH);
    delayMicroseconds(12);
    digitalWrite(latch1_, LOW);
    digitalWrite(latch2_, LOW);
    delayMicroseconds(6);

    for (int i = 0; i < 16; ++i) {
      digitalWrite(clock1_, LOW);
      digitalWrite(clock2_, LOW);
      delayMicroseconds(6);
      if (i <= 11) {
        buttons[0][i] = digitalRead(serial1_);
        buttons[1][i] = digitalRead(serial2_);
      }
      digitalWrite(clock1_, HIGH);
      digitalWrite(clock2_, HIGH);
      delayMicroseconds(6);
    }
  }

  int button(int player, int button) {
    return (buttons[player][button] == LOW) ? 0 : 1;
  }

 private:
  const int clock1_;
  const int clock2_;
  const int latch1_;
  const int latch2_;
  const int serial1_;
  const int serial2_;
  int buttons[2][12];
};

NanoDataClient data;
NanoSenseClient sense;
NanoLedClient led;
JVSIO io(&data, &sense, &led);

// Note: You can pass other available GPIO pins if you want.
// A0: SNES P1 Clock
// A1: SNES P1 Latch
// A2: SNES P1 Serial
// A3: SNES P2 Clock
// A4: SNES P2 Latch
// A5: SNES P2 Serial
SnesController snesc(A0, A1, A2, A3, A4, A5);

static const char io_id[] = "SEGA ENTERPRISES,LTD.compat;IONA-NANO;ver0.94;Experimental SNES Support";
uint8_t ios[5] = { 0x00, 0x00, 0x00, 0x00, 0x00 };
uint8_t coinCount = 0;
uint8_t coin = 0;
uint8_t gpout = 0;

void setup() {
  io.begin();
  snesc.begin();
}

void loop() {
  uint8_t len;
  uint8_t* data = io.getNextCommand(&len);
  if (!data) {
    snesc.update();

    // Note: You can map controllers' inputs as you want.
    // See https://github.com/toyoshim/iona/wiki/SW-Standard for details.
    // Common SW: TEST TILT1 TILT2 TILT3 UND UND UND UND
    ios[0] = 0;
    // START SERVICE UP DOWN LEFT RIGHT PUSH1 PISH2
    ios[1] =
        (snesc.button(SnesController::P1, SnesController::Start) << 7) |
        (snesc.button(SnesController::P1, SnesController::Up   ) << 5) |
        (snesc.button(SnesController::P1, SnesController::Down ) << 4) |
        (snesc.button(SnesController::P1, SnesController::Left ) << 3) |
        (snesc.button(SnesController::P1, SnesController::Right) << 2) |
        (snesc.button(SnesController::P1, SnesController::A    ) << 1) |
        (snesc.button(SnesController::P1, SnesController::B    ) << 0);
      // PUSH3 PUSH4 PUSH5 PUSH6 (PUSH7) (PUSH8) UND UND
    ios[2] =
        (snesc.button(SnesController::P1, SnesController::X    ) << 7) |
        (snesc.button(SnesController::P1, SnesController::Y    ) << 6) |
        (snesc.button(SnesController::P1, SnesController::L    ) << 5) |
        (snesc.button(SnesController::P1, SnesController::R    ) << 4);
    // START SERVICE UP DOWN LEFT RIGHT PUSH1 PISH2
    ios[3] =
        (snesc.button(SnesController::P2, SnesController::Start) << 7) |
        (snesc.button(SnesController::P2, SnesController::Up   ) << 5) |
        (snesc.button(SnesController::P2, SnesController::Down ) << 4) |
        (snesc.button(SnesController::P2, SnesController::Left ) << 3) |
        (snesc.button(SnesController::P2, SnesController::Right) << 2) |
        (snesc.button(SnesController::P2, SnesController::A    ) << 1) |
        (snesc.button(SnesController::P2, SnesController::B    ) << 0);
      // PUSH3 PUSH4 PUSH5 PUSH6 (PUSH7) (PUSH8) UND UND
    ios[4] =
        (snesc.button(SnesController::P2, SnesController::X    ) << 7) |
        (snesc.button(SnesController::P2, SnesController::Y    ) << 6) |
        (snesc.button(SnesController::P2, SnesController::L    ) << 5) |
        (snesc.button(SnesController::P2, SnesController::R    ) << 4);

    // Update coin
    uint8_t newCoin = snesc.button(SnesController::P1, SnesController::Select);
    if (coin && !newCoin)
      coinCount++;
    coin = newCoin;
    return;
  }
  switch (*data) {
   case JVSIO::kCmdIoId:
    io.pushReport(JVSIO::kReportOk);
    {
      for (size_t i = 0; io_id[i]; ++i)
        io.pushReport(io_id[i]);
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
        size_t offset = player * 2 + line;
        io.pushReport(offset < sizeof(ios) ? ios[offset] : 0x00);
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
    if (data[1] == 0)
      coinCount -= data[3];
    io.pushReport(JVSIO::kReportOk);
    break;
   case JVSIO::kCmdDriverOutput:
    gpout = data[2];
    io.pushReport(JVSIO::kReportOk);
    break;
  }
}
