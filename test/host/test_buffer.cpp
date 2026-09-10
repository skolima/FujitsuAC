/*
  FujitsuAC - ESP32 libary for controlling FujitsuAC through MQTT
  Copyright (c) 2025 Benas Ragauskas. All rights reserved.

  Project home: https://github.com/Benas09/FujitsuAC
*/

// Host-native tests for FujitsuAC::Buffer - the UART frame assembler in
// src/Buffer.cpp. No board required: compile with the stub in test/host/Arduino.h
// and run (see test/README.md). Against a Buffer::loop without the frame-length
// guards, dropsAFrameLongerThanTheBuffer overruns buffer[128] and the process
// crashes - a diagnosed heap-buffer-overflow under -fsanitize=address, a
// SIGSEGV without it.

#include "Arduino.h"

#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <memory>
#include <vector>

#include "Buffer.h"

using FujitsuAC::Buffer;

// --- host clock -------------------------------------------------------------
static unsigned long g_now = 0;
unsigned long millis() { return g_now; }

// --- fake UART -------------------------------------------------------------
class FakeStream : public Stream {
    public:
        void feed(std::initializer_list<uint8_t> bytes) {
            queue_.insert(queue_.end(), bytes.begin(), bytes.end());
        }
        void feed(const std::vector<uint8_t> &bytes) {
            queue_.insert(queue_.end(), bytes.begin(), bytes.end());
        }

        int available() override { return static_cast<int>(queue_.size() - pos_); }
        int read() override { return pos_ < queue_.size() ? queue_[pos_++] : -1; }

    private:
        std::vector<uint8_t> queue_;
        std::size_t pos_ = 0;
};

// --- tiny assert harness --------------------------------------------------
static int g_checks = 0;
static int g_failed = 0;

#define CHECK(cond)                                                            \
    do {                                                                      \
        ++g_checks;                                                           \
        if (!(cond)) {                                                        \
            ++g_failed;                                                       \
            std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);     \
        }                                                                     \
    } while (0)

// --- helpers -------------------------------------------------------------
struct CapturedFrame {
    std::vector<uint8_t> bytes;
    int size = 0;
    bool valid = false;
};

// Append the trailing 16-bit checksum Buffer::isValidFrame expects: 0xFFFF
// minus every preceding byte, big-endian.
static std::vector<uint8_t> withChecksum(std::vector<uint8_t> frame) {
    uint16_t checksum = 0xFFFF;
    for (uint8_t b : frame) {
        checksum -= b;
    }
    frame.push_back(static_cast<uint8_t>(checksum >> 8));
    frame.push_back(static_cast<uint8_t>(checksum & 0xFF));
    return frame;
}

// A well-formed frame: bytes 0..3 zero, byte 4 = payloadLen, payload zeroed,
// then the checksum. Total length is payloadLen + 7, matching Buffer's
// buffer[4] + 7.
static std::vector<uint8_t> makeFrame(uint8_t payloadLen) {
    std::vector<uint8_t> frame(5 + payloadLen, 0x00);
    frame[4] = payloadLen;
    return withChecksum(std::move(frame));
}

static std::vector<CapturedFrame> run(Buffer &buffer) {
    std::vector<CapturedFrame> frames;
    buffer.loop([&frames](uint8_t data[128], int size, bool isValid) {
        CapturedFrame f;
        f.size = size;
        f.valid = isValid;
        if (size > 0 && size <= 128) {
            f.bytes.assign(data, data + size);
        }
        frames.push_back(f);
    });
    return frames;
}

// --- tests -------------------------------------------------------------
// The real UTY-TFSXW1 "Init1" reply, straight from TFSXW1Controller.
static const std::vector<uint8_t> kInit1Reply = {
    0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0xFF, 0xFD};

static void deliversAValidFrame() {
    std::printf("deliversAValidFrame\n");
    FakeStream uart;
    auto buffer = std::make_unique<Buffer>(uart);

    uart.feed(kInit1Reply);
    auto frames = run(*buffer);

    CHECK(frames.size() == 1);
    if (frames.size() == 1) {
        CHECK(frames[0].size == 8);
        CHECK(frames[0].valid);
        CHECK(frames[0].bytes == kInit1Reply);
    }
}

static void flagsABadChecksum() {
    std::printf("flagsABadChecksum\n");
    FakeStream uart;
    auto buffer = std::make_unique<Buffer>(uart);

    std::vector<uint8_t> corrupt = kInit1Reply;
    corrupt.back() ^= 0xFF;
    uart.feed(corrupt);
    auto frames = run(*buffer);

    CHECK(frames.size() == 1);
    if (frames.size() == 1) {
        CHECK(frames[0].size == 8);
        CHECK(!frames[0].valid);
    }
}

static void resynchronisesAfterInterByteGap() {
    std::printf("resynchronisesAfterInterByteGap\n");
    FakeStream uart;
    auto buffer = std::make_unique<Buffer>(uart);

    // A partial/garbled burst, then a >= 20 ms gap, then a clean frame.
    uart.feed({0x03, 0x11, 0x22});
    run(*buffer);

    g_now += 25;
    uart.feed(kInit1Reply);
    auto frames = run(*buffer);

    CHECK(frames.size() == 1);
    if (frames.size() == 1) {
        CHECK(frames[0].valid);
        CHECK(frames[0].bytes == kInit1Reply);
    }
}

static void dropsAFrameLongerThanTheBuffer() {
    std::printf("dropsAFrameLongerThanTheBuffer\n");
    FakeStream uart;
    auto buffer = std::make_unique<Buffer>(uart);

    // Length byte 0xFF => a claimed 262-byte frame. Feed well over 128 bytes
    // with no inter-byte gap. Without the guards in Buffer::loop this overruns
    // buffer[128] and the process crashes here; with them the frame is dropped.
    std::vector<uint8_t> flood(400, 0x00);
    flood[4] = 0xFF;
    uart.feed(flood);
    auto frames = run(*buffer);

    for (const auto &f : frames) {
        CHECK(f.size <= 128);
    }
    // Nothing valid can come out of an all-zero flood.
    for (const auto &f : frames) {
        CHECK(!f.valid);
    }
}

static void acceptsTheLargestFittingFrame() {
    std::printf("acceptsTheLargestFittingFrame\n");
    FakeStream uart;
    auto buffer = std::make_unique<Buffer>(uart);

    // payloadLen 121 => total 128, exactly sizeof(buffer). Must still be
    // delivered - the "too long" check has to be strictly greater-than.
    auto frame = makeFrame(121);
    CHECK(frame.size() == 128);
    uart.feed(frame);
    auto frames = run(*buffer);

    CHECK(frames.size() == 1);
    if (frames.size() == 1) {
        CHECK(frames[0].size == 128);
        CHECK(frames[0].valid);
    }
}

static void survivesContinuousNoise() {
    std::printf("survivesContinuousNoise\n");
    FakeStream uart;
    auto buffer = std::make_unique<Buffer>(uart);

    // 4 KB of deterministic pseudo-random bytes, no gap. The only invariant is
    // that Buffer never reports a frame larger than its buffer and never
    // overruns it (ASan).
    std::uint32_t state = 0x12345678u;
    std::vector<uint8_t> noise;
    noise.reserve(4096);
    for (int i = 0; i < 4096; ++i) {
        state = state * 1664525u + 1013904223u;
        noise.push_back(static_cast<uint8_t>(state >> 24));
    }
    uart.feed(noise);
    auto frames = run(*buffer);

    for (const auto &f : frames) {
        CHECK(f.size >= 5);
        CHECK(f.size <= 128);
    }
}

int main() {
    deliversAValidFrame();
    flagsABadChecksum();
    resynchronisesAfterInterByteGap();
    dropsAFrameLongerThanTheBuffer();
    acceptsTheLargestFittingFrame();
    survivesContinuousNoise();

    std::printf("\n%d checks, %d failed\n", g_checks, g_failed);
    return g_failed == 0 ? 0 : 1;
}
