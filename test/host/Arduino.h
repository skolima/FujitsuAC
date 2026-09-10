/*
  FujitsuAC - ESP32 libary for controlling FujitsuAC through MQTT
  Copyright (c) 2025 Benas Ragauskas. All rights reserved.

  Project home: https://github.com/Benas09/FujitsuAC
*/

// Minimal host stand-in for the Arduino core: just enough of it to compile the
// hardware-independent sources under src/ off-device for the tests in this
// directory. This is not a general Arduino emulation - add only what a source
// under test actually needs.
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>

// Arduino's millis(). The test binary provides the definition and drives the
// clock so time-dependent behaviour (the inter-frame gap) is deterministic.
unsigned long millis();

// Arduino's Stream, pared down to the two methods Buffer uses.
class Stream {
    public:
        virtual ~Stream() = default;

        virtual int available() = 0;
        virtual int read() = 0;
        virtual int peek() { return -1; }
};
