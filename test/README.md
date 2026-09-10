# Host tests

Tests for the hardware-independent logic in `src/` - the parts that need no
ESP32 to exercise (UART frame assembly, checksums, protocol state). They compile
against a tiny stub of the Arduino core in `host/Arduino.h` and run on any
desktop.

CI runs them under AddressSanitizer + UndefinedBehaviorSanitizer
(`.github/workflows/host-tests.yml`), which is where memory-safety bugs in the
protocol code surface.

## Running locally

```sh
g++ -std=c++17 -Wall -Wextra -Werror \
    -fsanitize=address,undefined -fno-sanitize-recover=all -g -O1 \
    -I test/host -I src \
    test/host/test_buffer.cpp src/Buffer.cpp \
    -o test_buffer
./test_buffer
```

`clang++` works too. Drop `-fsanitize=...` if your toolchain has no sanitizer
runtime (e.g. stock MinGW) - the assertions still run, but a buffer overrun then
shows up as a plain crash rather than a diagnosed one.

## Layout

| Path | What it is |
| --- | --- |
| `host/Arduino.h` | Minimal Arduino-core stub: `Stream`, `millis()`. Extend only as sources under test require. |
| `host/test_buffer.cpp` | Tests for `FujitsuAC::Buffer`, plus a ~30-line assert harness and `main()`. |

Adding a suite: put `test_<unit>.cpp` in `host/`, add whatever core symbols it
needs to `host/Arduino.h`, and add a compile+run step (or job) to the workflow.
