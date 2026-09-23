# LiteOTA Tests

Tests are organized in two layers:

1. **`test/native/`** — host-side unit tests (run on your PC, no ESP needed)
2. **`test/embedded/`** — on-device test sketches (uploaded to Wemos D1 mini)

---

## Native Tests (PlatformIO)

Requires [PlatformIO Core](https://docs.platformio.org/en/latest/core/installation/index.html).

```bash
cd LiteOTA
pio test -e native