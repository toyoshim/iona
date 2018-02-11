// Copyright 2018 Takashi Toyoshima <toyoshim@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "JVSIO.h"

#include "Arduino.h"

namespace {

constexpr uint8_t kHostAddress = 0x00;
constexpr uint8_t kBroadcastAddress = 0xFF;
constexpr uint8_t kMarker = 0xD0;
constexpr uint8_t kSync = 0xE0;

void dump(const char* str, uint8_t* data, int len) {
  Serial.print(str);
  Serial.print(": ");
  for (size_t i = 0; i < len; ++i) {
    if (data[i] < 16)
      Serial.print("0");
    Serial.print(data[i], HEX);
    Serial.print(" ");
  }
  Serial.println("");
}

void writeByte(uint8_t data) {
  // 138t for each bit.
  asm (
    "rjmp 4f\n"

   // Spends 134t = 8 + 1 + 3 x N - 1 + 2 + 4; N = 40
   "1:\n"
    "brcs 2f\n"      // 2t (1t for not taken)
    "nop\n"          // 1t
    "cbi 0x0b, 0\n"  // 2t
    "sbi 0x0b, 2\n"  // 2t
    "rjmp 3f\n"      // 2t (1 + 1 + 2 + 2 + 2)
   "2:\n"
    "sbi 0x0b, 0\n"  // 2t
    "cbi 0x0b, 2\n"  // 2t
    "rjmp 3f\n"      // 2t (2 + 2 + 2 + 2)
   "3:\n"
    "ldi r19, 40\n"  // 1t
   "2:\n"
    "dec r19\n"      // 1t
    "brne 2b\n"      // 2t (1t for not taken)
    "nop\n"          // 1t
    "nop\n"          // 1t
    "ret\n"          // 4t

   // Sends Start, bit 0, ..., bit 7, Stop
   "4:\n"
    "mov r18, %0\n"
    // Start bit
    "sec\n"         // 1t
    "rcall 1b\n"    // 3t
    "clc\n"         // 1t
    "rcall 1b\n"    // 3t
    // Bit 0
    "ror r18\n"     // 1t
    "rcall 1b\n"    // 3t
    // Bit 1
    "ror r18\n"     // 1t
    "rcall 1b\n"    // 3t
    // Bit 2
    "ror r18\n"     // 1t
    "rcall 1b\n"    // 3t
    // Bit 3
    "ror r18\n"     // 1t
    "rcall 1b\n"    // 3t
    // Bit 4
    "ror r18\n"     // 1t
    "rcall 1b\n"    // 3t
    // Bit 5
    "ror r18\n"     // 1t
    "rcall 1b\n"    // 3t
    // Bit 6
    "ror r18\n"     // 1t
    "rcall 1b\n"    // 3t
    // Bit 7
    "ror r18\n"     // 1t
    "rcall 1b\n"    // 3t
    // Stop bit
    "sec\n"         // 1t
    "rcall 1b\n"    // 3t
   :: "r" (data));
}

void writeEscapedByte(uint8_t data) {
  if (data == kMarker || data == kSync) {
    writeByte(kMarker);
    writeByte(data - 1);
  } else {
    writeByte(data);
  }
}

uint8_t getCommandSize(uint8_t* command, uint8_t len) {
  switch(*command) {
   case JVSIO::kCmdReset:
   case JVSIO::kCmdAddressSet:
    return 2;
   case JVSIO::kCmdIoId:
   case JVSIO::kCmdCommandRev:
   case JVSIO::kCmdJvRev:
   case JVSIO::kCmdProtocolVer:
   case JVSIO::kCmdFunctionCheck:
    return 1;
   case JVSIO::kCmdMainId:
    break;  // handled later
   case JVSIO::kCmdSwInput:
    return 3;
   case JVSIO::kCmdCoinInput:
    return 2;
   case JVSIO::kCmdRetry:
    return 1;
   case JVSIO::kCmdCoinSub:
    return 4;
   default:
    dump("unknown command", command, 1);
    return 0;  // not supported
  }
  uint8_t size = 2;
  for (uint8_t i = 1; i < len && command[i]; ++i)
    size++;
  return size;
}

}  // namespace

JVSIO::JVSIO() :
    rx_size_(0),
    rx_read_ptr_(0),
    rx_receiving_(false),
    rx_escaping_(false),
    rx_available_(false),
    address_(kBroadcastAddress),
    tx_report_size_(0) {}

JVSIO::~JVSIO() {}

void JVSIO::begin() {
  pinMode(0, INPUT);
  pinMode(2, INPUT);
  Serial.begin(115200);

  // CTC mode
  // Toggle output on matching the counter for Ch.B (Pin 3)
  TCCR2A = 0x12;
  // Count from 0 to 1
  OCR2A = 1;
  // Stop
  TCCR2B = (TCCR2B & ~7) | 0;
  // Run at CLK/1
  TCCR2B = (TCCR2B & ~7) | 1;
  pinMode(3, OUTPUT);
  digitalWrite(3, LOW);
}

void JVSIO::end() {
  pinMode(0, INPUT);
  pinMode(2, INPUT);
  Serial.end();
}

uint8_t* JVSIO::getNextCommand(uint8_t* len) {
  for (;;) {
    receive();
    if (!rx_available_)
      return nullptr;

    uint8_t max_command_size = rx_data_[1] - rx_read_ptr_ + 1;
    if (!max_command_size) {
      sendOkStatus();
      continue;
    }
    uint8_t command_size = getCommandSize(&rx_data_[rx_read_ptr_], &max_command_size);
    if (!command_size) {
      sendUnknownCommandStatus();
      continue;
    }
    if (command_size > max_command_size) {
      pushReport(JVSIO::kReportParamErrorNoResponse);
      sendUnknownCommandStatus();
      continue;
    }
    switch (rx_data_[rx_read_ptr_]) {
     case JVSIO::kCmdReset:
      senseNotReady();
      address_ = kBroadcastAddress;
      rx_available_ = false;
      rx_receiving_ = false;
      dump("reset", nullptr, 0);
      break;
     case JVSIO::kCmdAddressSet:
      senseReady();
      address_ = rx_data_[rx_read_ptr_ + 1];
      pushReport(JVSIO::kReportOk);
      dump("address", &rx_data_[rx_read_ptr_ + 1], 1);
      break;
     case JVSIO::kCmdCommandRev:
      pushReport(JVSIO::kReportOk);
      pushReport(0x13);
      break;
     case JVSIO::kCmdJvRev:
      pushReport(JVSIO::kReportOk);
      pushReport(0x30);
      break;
     case JVSIO::kCmdProtocolVer:
      pushReport(JVSIO::kReportOk);
      pushReport(0x10);
      break;
     case JVSIO::kCmdMainId:
      // TODO
      dump("ignore kCmdMainId", nullptr, 0);
      sendUnknownCommandStatus();
      break;
     case JVSIO::kCmdRetry:
      sendStatus();
      break;
     case JVSIO::kCmdIoId:
     case JVSIO::kCmdFunctionCheck:
     case JVSIO::kCmdSwInput:
     case JVSIO::kCmdCoinInput:
     case JVSIO::kCmdCoinSub:
      *len = command_size;
      rx_read_ptr_ += command_size;
      return &rx_data_[rx_read_ptr_ - command_size];

     default:
      sendUnknownCommandStatus();
      break;
    }
    rx_read_ptr_ += command_size;
  }
}

void JVSIO::pushReport(uint8_t report) {
  if (tx_report_size_ == 253) {
    sendOverflowStatus();
    tx_report_size_++;
  } else if (tx_report_size_ < 253) {
    tx_data_[3 + tx_report_size_] = report;
    tx_report_size_++;
  }
}

void JVSIO::senseNotReady() {
  TCCR2A |= 0x10;

  pinMode(13, OUTPUT);
  digitalWrite(13, LOW);
}

void JVSIO::senseReady() {
  TCCR2A &= ~0x30;

  pinMode(13, OUTPUT);
  digitalWrite(13, HIGH);
}

void JVSIO::receive() {
  while (!rx_available_ && Serial.available() > 0) {
    uint8_t data = Serial.read();
    if (data == kSync) {
      rx_size_ = 0;
      rx_receiving_ = true;
      rx_escaping_ = false;
      continue;
    }
    if (!rx_receiving_)
      continue;
    if (data == kMarker) {
      rx_escaping_ = true;
      continue;
    }
    if (rx_escaping_) {
      rx_data_[rx_size_++] = data + 1;
      rx_escaping_ = false;
    } else {
      rx_data_[rx_size_++] = data;
    }
    if (rx_size_ >= 2 && ((rx_data_[1] + 2) == rx_size_)) {
      uint8_t sum = 0;
      for (size_t i = 0; i < (rx_size_ - 1); ++i)
        sum += rx_data_[i];
      if (rx_data_[rx_size_ - 1] != sum) {
        sendSumErrorStatus();
        rx_size_ = 0;
      } else if (rx_data_[0] == kBroadcastAddress || rx_data_[0] == address_) {
        rx_read_ptr_ = 2;  // Skip address and length
        rx_available_ = true;
        tx_report_size_ = 0;
        // Switch to output (should be done in 100us)
        digitalWrite(0, HIGH);
        digitalWrite(2, LOW);
        pinMode(0, OUTPUT);
        pinMode(2, OUTPUT);
      } else {
        rx_size_ = 0;
      }
    }
  }
}

void JVSIO::sendStatus() {
  digitalWrite(0, HIGH);
  digitalWrite(2, LOW);
  pinMode(0, OUTPUT);
  pinMode(2, OUTPUT);
  Serial.end();

  delayMicroseconds(100);

  noInterrupts();
  writeByte(kSync);
  uint8_t sum = 0;

  for (uint8_t i = 0; i <= tx_data_[1]; ++i) {
    sum += tx_data_[i];
    writeEscapedByte(tx_data_[i]);
  }
  writeEscapedByte(sum);
  interrupts();

  pinMode(0, INPUT);
  pinMode(2, INPUT);
  Serial.begin(115200);

  rx_available_ = false;
  rx_receiving_ = false;

  //dump("packet", rx_data_, rx_size_);
  //dump("status", tx_data_, tx_data_[1] + 1);
}

void JVSIO::sendOkStatus() {  
  if (tx_report_size_ > 253)
    return sendOverflowStatus();
  tx_data_[0] = kHostAddress;
  tx_data_[1] = 2 + tx_report_size_;
  tx_data_[2] = 0x01;
  sendStatus();
}

void JVSIO::sendUnknownCommandStatus() {  
  if (tx_report_size_ > 253)
    return sendOverflowStatus();
  tx_data_[0] = kHostAddress;
  tx_data_[1] = 2 + tx_report_size_;
  tx_data_[2] = 0x02;
  sendStatus();
}

void JVSIO::sendSumErrorStatus() {
  tx_data_[0] = kHostAddress;
  tx_data_[1] = 2;
  tx_data_[2] = 0x03;
  sendStatus();
}

void JVSIO::sendOverflowStatus() {
  tx_data_[0] = kHostAddress;
  tx_data_[1] = 2;
  tx_data_[2] = 0x04;
  sendStatus();
}

