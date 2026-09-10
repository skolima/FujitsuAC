/*
  FujitsuAC - ESP32 libary for controlling FujitsuAC through MQTT
  Copyright (c) 2025 Benas Ragauskas. All rights reserved.
  
  Project home: https://github.com/Benas09/FujitsuAC
*/

#include "Buffer.h"

namespace FujitsuAC {

    Buffer::Buffer(Stream &uart): uart(uart) {}

    bool Buffer::loop(std::function<void(uint8_t buffer[128], int size, bool isValid)> callback) {
        while (this->uart.available()) {
            uint8_t b = uart.read();
            uint32_t now = millis();

            if ((now - this->lastMillis) >= 20) {
                this->currentIndex = 0;
            }

            this->lastMillis = now;

            // currentIndex is only reset on a >= 20 ms inter-byte gap, so a noisy
            // bus keeps appending within one "frame". buffer[4] is the length
            // byte and is peer-supplied: up to 255, i.e. a claimed frame of
            // buffer[4] + 7 = 262 bytes against uint8_t buffer[128]. Without this
            // guard the write below walks off the end of the buffer.
            if (this->currentIndex >= (int) sizeof(this->buffer)) {
                this->currentIndex = 0;
                continue;
            }

            this->buffer[this->currentIndex] = b;
            this->currentIndex++;

            // Drop a frame that cannot fit as soon as its length byte is known,
            // so a bad length byte resynchronises here instead of consuming the
            // next sizeof(buffer) bytes off the bus first.
            if (this->currentIndex == 5 && (int) this->buffer[4] + 7 > (int) sizeof(this->buffer)) {
                this->currentIndex = 0;
                continue;
            }

            if (this->currentIndex > 4 && this->currentIndex == (int) this->buffer[4] + 7) {
                if (callback) {
                    callback(
                        this->buffer, (int) this->buffer[4] + 7,
                        this->isValidFrame(this->buffer, (int) this->buffer[4] + 7)
                    );
                }
            }
        }
        
        return true;
    }

    bool Buffer::isValidFrame(uint8_t buffer[128], int size) {
        uint16_t frameChecksum = (buffer[size - 2] << 8) | buffer[size - 1];
        
        uint16_t checksum = 0xFFFF; 
        
        for (int i = 0; i < size - 2; i++) {
            checksum -= buffer[i];
        }
        
        return frameChecksum == checksum;
    }

}