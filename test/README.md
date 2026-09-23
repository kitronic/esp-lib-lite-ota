# LiteOTA Tests

**Kitronic** — [info@kitronic.tech](mailto:info@kitronic.tech)

Two layers of tests:

1. **`native/`** — host-side unit tests (run on your PC with `g++`, no hardware needed)
2. **`embedded/`** — on-device test sketches (uploaded to a Wemos D1 mini)

---

## Native Tests (g++)

No PlatformIO, no Unity — just `g++` and `make`.

```bash
cd test/native
make
```

Or without `make`:

```bash
cd test/native
./run.sh
```

Or straight `g++`:

```bash
cd test/native
g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -o test_runner test_native.cpp
./test_runner
```

Expected output:

```
Total:  29
Passed: 29
Failed: 0
✅ ALL PASSED
```

### Coverage
- Manifest type detection (JSON vs TEXT)
- URL protocol detection (http vs https)
- Version comparison (equal, different, case-sensitive)
- JSON parsing (simple, whitespace, missing fields, empty)
- Plain-text parsing (simple, CRLF, no trailing newline, incomplete)
- Chunk size clamping (zero, oversized, in-range)

---

## Embedded Tests (Wemos D1 mini)

### Prerequisites
1. Start the local test server:
   ```bash
   cd test/server
   ./serve.sh
   ```
2. Note your PC's IP (`ip addr` / `ifconfig` / `ipconfig`)
3. Edit `SERVER_HOST` in each sketch below

### Sketches

| Sketch | Purpose |
|--------|---------|
| `test_manifest_parse` | JSON + TXT parsing against local server |
| `test_version_compare` | Version compare logic (self-hosted manifest) |
| `test_heap_guard` | Confirms OTA refused when heap is low |
| `test_callbacks` | Verifies before/after callbacks fire correctly |
| `test_mfln` | Confirms MFLN shrinks TLS buffers |
| `test_abort` | Confirms abort() releases resources and resumes callbacks |
| `test_rollback` | Confirms rollback feature initializes and boots |

Upload with PlatformIO:

```bash
pio run -e d1_mini -t upload --upload-port /dev/ttyUSB0
pio device monitor -b 115200
```

---

## Test Server

```bash
cd test/server
./serve.sh            # defaults to port 8080
./serve.sh 9000       # custom port
```

Serves:
- `http://<your-ip>:8080/project.json`
- `http://<your-ip>:8080/project.txt`

---

## Links

- 🐙 [github.com/kitronic/esp-lib-lite-ota](https://github.com/kitronic/esp-lib-lite-ota)
- 📧 [info@kitronic.tech](mailto:info@kitronic.tech)