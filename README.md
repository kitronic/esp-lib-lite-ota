# LiteOTA

<p align="center">
  <strong>Kitronic</strong><br>
  <a href="mailto:info@kitronic.tech">info@kitronic.tech</a> ·
  <a href="https://www.kitronic.tech">www.kitronic.tech</a> ·
  <a href="https://github.com/kitronic/esp-lib-lite-ota">GitHub</a>
</p>

---

> **Ultra-lightweight non-blocking OTA updater for ESP8266 (Wemos D1 mini / D1 R2)**

Reads a remote manifest (JSON **or** plain text), compares versions, and
downloads + applies new firmware using `ESP8266HTTPUpdate` — all without
blocking your main loop.

**No `String`. No heap fragmentation. Optional HTTPS with MFLN.**
**Built for Web + MQTT + OTA projects on tight RAM.**

---

## ✨ Features

### Non-blocking by design
- ✅ `tick()`-driven state machine — never blocks the main loop
- ✅ Web server keeps responding during download
- ✅ MQTT stays connected throughout OTA (with cooperative handoff)
- ✅ Chunked download (~512 B per tick, configurable)

### Memory safety
- ✅ Zero `String` usage — all `char[]` buffers
- ✅ `StaticJsonDocument<512>` — stack, not heap
- ✅ Heap guard: refuses to start if free heap < threshold
- ✅ No `malloc`, no fragmentation

### Manifest support
- ✅ JSON manifest (auto-detected)
- ✅ Plain-text manifest (auto-detected)
- ✅ Optional `notes` field (ignored)

### TLS / HTTPS (optional)
- ✅ Enable with `#define LITEOTA_USE_TLS` (compile-time)
- ✅ **MFLN** shrinks TLS buffers from 16 KB → 1 KB
- ✅ `setInsecure()` for zero-config HTTPS
- ✅ `setCACert()` for cert-pinned HTTPS
- ✅ Separate plain + secure clients (mixed-mode safe)

### Control
- ✅ Scheduled checks (`setCheckInterval`)
- ✅ Manual trigger (`requestUpdate()`)
- ✅ Abort mid-download (`abort()`)
- ✅ Configurable retries and chunk size

### Observability
- ✅ Progress callback (`onProgress`)
- ✅ State-change callback (`onStateChange`)
- ✅ Cooperative handoff (`onBeforeRequest` / `onAfterRequest`)
- ✅ `getState()`, `getLastError()`, `getProgressPercent()`
- ✅ Human-readable state and error names

### Integration
- ✅ Works alongside `ESP8266WebServer`
- ✅ Works alongside `PubSubClient` (MQTT)
- ✅ Compatible with Deep Sleep projects
- ✅ Wemos D1 mini / D1 R2 / NodeMCU

---

## 📦 Installation

### Arduino IDE (manual)
1. Download or clone this repo
2. Place the `LiteOTA` folder in your Arduino `libraries/` directory:
   - Windows: `Documents/Arduino/libraries/LiteOTA`
   - Linux/macOS: `~/Arduino/libraries/LiteOTA`
3. Restart Arduino IDE

### PlatformIO
Add to your `platformio.ini`:

```ini
lib_deps =
    https://github.com/kitronic/esp-lib-lite-ota.git
```

### Dependencies
- `ArduinoJson` (>= 6.19.0)
- ESP8266 Arduino Core (>= 3.0.0)

---

## 🚀 Quick Start

```cpp
#include <ESP8266WiFi.h>
#include <LiteOTA.h>

#define CURRENT_VERSION "1.0"

LiteOTA ota(CURRENT_VERSION, "http://your-server.com/project.json");

void setup() {
    Serial.begin(115200);
    WiFi.begin("ssid", "pass");
    while (WiFi.status() != WL_CONNECTED) { delay(500); yield(); }

    ota.begin();
    ota.requestUpdate();       // trigger once at boot
}

void loop() {
    ota.tick();                // ⚡ non-blocking
    yield();
}
```

---

## 📄 Manifest Formats

LiteOTA auto-detects the format from the first non-whitespace byte.

### JSON (starts with `{`)
```json
{
  "version": "1.1",
  "url": "http://your-server.com/firmware_v1.1.bin",
  "notes": "Optional. Ignored by the library."
}
```

### Plain text (anything else)
```
1.1
http://your-server.com/firmware_v1.1.bin
```
Line 1 = version, line 2 = firmware URL. Trailing newline optional.

---

## 🧠 How It Works

The library is a **state machine** driven by `tick()`:

```
IDLE ─(schedule/manual)─▶ FETCH_MANIFEST ─▶ PARSE_MANIFEST
                                                    │
                                          ┌─────────┴─────────┐
                                          ▼                   ▼
                                    (same version)      (new version)
                                          │                   │
                                          ▼                   ▼
                                       IDLE ◀── DOWNLOADING ◀─ OPEN_FIRMWARE
                                                     │
                                                     ▼
                                                 FINALIZING ─▶ reboot
```

Each call to `tick()` advances one step:
- HTTP GET (1–2 s, but cooperative — you can abort)
- Parse manifest (instant)
- Download one chunk (~512 B, ~10 ms)
- Finalize (reboot)

Between steps, your Web and MQTT code runs normally.

---

## 🔐 HTTPS Support (Optional)

TLS is **disabled by default**. Enable it at the top of your sketch:

```cpp
#define LITEOTA_USE_TLS   // ⚠️ must come BEFORE #include <LiteOTA.h>
#include <LiteOTA.h>
```

### Configuration

```cpp
// Option A: no cert validation (simplest, least setup)
ota.setInsecure();

// Option B: CA certificate
static const char CA_CERT[] PROGMEM = R"EOF(
-----BEGIN CERTIFICATE-----
...
-----END CERTIFICATE-----
)EOF";
ota.setCACert(CA_CERT);
```

### The MFLN trick (important)

By default, TLS on ESP8266 eats **~18 KB of RAM** because the BearSSL RX
buffer is 16 KB. Enable **MFLN** to negotiate smaller fragments:

```cpp
ota.enableMFLN(1024);              // 🔑 shrinks RX buffer to 1 KB
ota.setTLSBufferSizes(1024, 512);  // 🔑 applies to BearSSL directly
```

Combined, this drops TLS RAM usage to **~2–3 KB** — small enough to
coexist with MQTT + Web on a Wemos D1 mini.

### Mixed-mode (recommended)

Serve the **manifest over HTTPS** (tiny file, brief TLS session) and the
**firmware over HTTP** (large file, no TLS overhead):

```cpp
LiteOTA ota(CURRENT_VERSION, "https://kitronic.tech/ota/project.json");
// firmware url inside the manifest → http://...
```

LiteOTA picks the right client per URL automatically.

### ⚠️ RAM Warning

Even with MFLN, combining **TLS + Web + MQTT + OTA** is tight. Use the
cooperative handoff callbacks to pause MQTT during TLS:

```cpp
ota.onBeforeRequest([]() {
    if (mqtt.connected()) mqtt.disconnect();
});
ota.onAfterRequest([]() {
    mqtt.connect("my-client-id");
});
```

If it still doesn't fit, move to **ESP32**.

---

## 🔧 API Reference

| Method | Returns | Description |
|--------|---------|-------------|
| `LiteOTA(version, url)` | — | Constructor |
| `begin()` | `void` | Initializes the state machine |
| `tick()` | `void` | ⚡ Must be called every loop |
| `requestUpdate()` | `void` | Trigger an OTA cycle |
| `abort()` | `void` | Cancel an in-flight update |
| `reset()` | `void` | Reset state and error |
| `setCheckInterval(sec)` | `void` | Scheduled checks (default 24 h) |
| `setMinFreeHeap(bytes)` | `void` | Heap guard (default 8 KB, 25 KB with TLS) |
| `setMaxRetries(n)` | `void` | Retries on failure (default 3) |
| `setChunkSize(bytes)` | `void` | Bytes per tick (default 512, max 512) |
| `setManifestUrl(url)` | `void` | Change manifest at runtime |
| `setInsecure()` | `void` | TLS: skip cert validation |
| `setCACert(pem)` | `void` | TLS: install CA cert |
| `setTLSBufferSizes(rx, tx)` | `void` | TLS: manual BearSSL buffers |
| `enableMFLN(len)` | `void` | TLS: negotiate smaller fragments |
| `onProgress(cb)` | `void` | Called during download |
| `onStateChange(cb)` | `void` | Called on state transitions |
| `onBeforeRequest(cb)` | `void` | Pause services before an HTTP request |
| `onAfterRequest(cb)` | `void` | Resume services after an HTTP request |
| `isUpdating()` | `bool` | True if an OTA cycle is active |
| `isError()` | `bool` | True if in ERROR_STATE |
| `getState()` | `LiteOTAState` | Current state enum |
| `getLastError()` | `LiteOTAError` | Last error enum |
| `getStateName()` | `const char*` | Human-readable state |
| `getLastErrorName()` | `const char*` | Human-readable error |
| `getRemoteVersion()` | `const char*` | Version from manifest |
| `getFirmwareUrl()` | `const char*` | Firmware URL from manifest |
| `getManifestType()` | `const char*` | `"JSON"` / `"TEXT"` / `"UNKNOWN"` |
| `getProgressPercent()` | `uint8_t` | 0–100 |
| `getBytesReceived()` | `size_t` | Bytes downloaded so far |
| `getBytesTotal()` | `size_t` | Total firmware size |

---

## 🧪 Examples

| Example | What it shows |
|---------|---------------|
| `Basic` | One-shot check on boot |
| `Scheduled` | Daily check via NTP, non-blocking |
| `ManualTrigger` | Web endpoint + MQTT trigger |
| `NonBlocking` | Full Web + MQTT + OTA integration |
| `HTTPS-MixedMode` | TLS manifest + HTTP firmware + MFLN + handoff |

---

## 🧪 Tests

### Native tests (run on your PC)
```bash
cd test/native
make           # builds and runs
```

### Embedded tests (on Wemos D1 mini)
1. Start the local server: `cd test/server && ./serve.sh`
2. Edit `SERVER_HOST` in each sketch under `test/embedded/`
3. Upload and monitor at 115200 baud

See `test/README.md` for details.

---

## 🐛 Debugging

Open Serial Monitor at **115200 baud**. You'll see:

```
[LiteOTA] Init. TLS:OFF Heap:45216
[LiteOTA] State: FETCH_MANIFEST
[LiteOTA] State: PARSE_MANIFEST
[LiteOTA] Remote:1.1 Current:1.0 Type:JSON
[LiteOTA] State: OPEN_FIRMWARE
[LiteOTA] Downloading 345920 bytes...
[PROGRESS] 1024/345920 (0%)
[PROGRESS] 2048/345920 (0%)
...
[LiteOTA] State: FINALIZING
[LiteOTA] OK. Rebooting...
```

Watch heap during a run:
```cpp
Serial.printf("Heap: %u | Frag: %u%%\n",
              ESP.getFreeHeap(), ESP.getHeapFragmentation());
```

If heap drops below 10 KB → expect failure.

---

## ⚠️ Limitations

- **HTTP by default** — HTTPS requires `#define LITEOTA_USE_TLS`
- **No signature verification** — don't use over untrusted networks
- **No automatic rollback** — if new firmware bricks, you need physical access
- **Single firmware URL** — no delta or multi-part updates
- **Buffer sizes fixed** — `_firmwareUrl[192]`, `_remoteVersion[16]`
- **One OTA at a time** — no concurrent downloads

---

## 🛠 Project Layout

```
LiteOTA/
├── src/                # library source
│   ├── LiteOTA.h
│   └── LiteOTA.cpp
├── examples/           # Arduino examples
├── test/               # native + embedded tests
└── extras/server/      # sample manifests + setup guide
```

---

## 📬 Contact

**Kitronic**

- 📧 Email: [info@kitronic.tech](mailto:info@kitronic.tech)
- 🌐 Website: [www.kitronic.tech](https://www.kitronic.tech)
- 🐙 GitHub: [github.com/kitronic/esp-lib-lite-ota](https://github.com/kitronic/esp-lib-lite-ota)

---

## 📜 License

MIT — see [LICENSE](LICENSE). Copyright (c) 2024 Kitronic.

---

## 🙏 Credits

Inspired by `AutoOTA`, `AutoUpdate_ESP`, and the Arduino ESP8266 core.

Built and maintained by **Kitronic**.