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

The controller suite links more of `src/`. Its sources are not yet clean under
`-Wall -Wextra`, so only the test file gets `-Werror` (and sees `src/` headers
through `-isystem`):

```sh
for f in TFSXW1Controller RegistryTable Buffer; do
    g++ -std=c++17 -fsanitize=address,undefined -fno-sanitize-recover=all -g -O1         -I test/host -I src -c src/$f.cpp -o $f.o
done
g++ -std=c++17 -Wall -Wextra -Werror     -fsanitize=address,undefined -fno-sanitize-recover=all -g -O1     -I test/host -isystem src     -c test/host/test_tfsxw1_controller.cpp -o test_tfsxw1_controller.o
g++ -fsanitize=address,undefined     test_tfsxw1_controller.o TFSXW1Controller.o RegistryTable.o Buffer.o     -o test_tfsxw1_controller
LSAN_OPTIONS=suppressions=test/host/lsan.supp ./test_tfsxw1_controller
```

`host/lsan.supp` suppresses one known allocation: the `RegistryTable` that
`initRegistryTable()` never frees. The device keeps its single controller for
its whole lifetime, but each test case builds a fresh one.

`clang++` works too. Drop `-fsanitize=...` if your toolchain has no sanitizer
runtime (e.g. stock MinGW) - the assertions still run, but a buffer overrun then
shows up as a plain crash rather than a diagnosed one.

## Layout

| Path | What it is |
| --- | --- |
| `host/Arduino.h` | Minimal Arduino-core stub: `Stream` (incl. `write()`), `millis()`, the C headers the core pulls in. Extend only as sources under test require. |
| `host/test_buffer.cpp` | Tests for `FujitsuAC::Buffer`, plus a ~30-line assert harness and `main()`. |
| `host/lsan.supp` | LeakSanitizer suppressions - currently just the controller's `RegistryTable`. |
| `host/test_tfsxw1_controller.cpp` | Tests for the `TFSXW1Controller` Init1/Init2 handshake, observed through the debug callback. |

Adding a suite: put `test_<unit>.cpp` in `host/`, add whatever core symbols it
needs to `host/Arduino.h`, and add a compile+run step (or job) to the workflow.
