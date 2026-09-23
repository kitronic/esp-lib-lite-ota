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
**Safe Rollback on boot failure. Built for Web + MQTT + OTA on tight RAM.**

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

### Safe Rollback (optional)
- ✅ Automatic backup of current firmware before OTA
- ✅ Boot-loop detection via RTC memory
- ✅ Automatic restore after N failed boots (default: 3)
- ✅ Manual rollback trigger (`rollbackToPrevious()`)
- ✅ Backup stored in LittleFS
- ✅ Enable with `#define LITEOTA_USE_ROLLBACK`

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
                                       IDLE ◀─ BACKUP_FIRMWARE (if rollback)
                                                                 │
                                                                 ▼
                                              OPEN_FIRMWARE ─▶ DOWNLOADING
                                                                 │
                                                                 ▼
                                                             FINALIZING ─▶ reboot
```

Each call to `tick()` advances one step:
- HTTP GET (1–2 s, but cooperative — you can abort)
- Parse manifest (instant)
- Backup current firmware to LittleFS (rollback only, ~1 s)
- Download one chunk (~512 B, ~10 ms)
- Finalize (reboot)

Between steps, your Web and MQTT code runs normally.

---

## 🔄 Safe Rollback

Prevents bricking when a new firmware fails to boot.

### How it works

1. **Before OTA** — current firmware is copied to `/liteota/backup.bin` in LittleFS
2. **RTC flag set** — marks "update pending"
3. **New firmware boots** — must call `confirmBoot()` (or let `tick()` do it after 30 s)
4. **If it crashes** before confirmation, RTC boot counter increments
5. **After 3 failed boots** — old firmware is restored from backup

### Enable it

```cpp
#define LITEOTA_USE_ROLLBACK
#include <LiteOTA.h>

void setup() {
    // ...
    ota.enableSafeRollback(true);
    ota.setRollbackTimeout(30);   // seconds to confirm boot
    ota.begin();
    ota.requestUpdate();
}

void loop() {
    ota.tick();   // auto-confirms boot after 30 s
    yield();
}
```

### API

| Method | Description |
|--------|-------------|
| `enableSafeRollback(bool)` | Turn the feature on/off |
| `setRollbackTimeout(sec)` | Seconds before boot is considered OK |
| `setRollbackBackupPath(path)` | Where to store the backup (default `/liteota/backup.bin`) |
| `isRollbackAvailable()` | True if a backup exists |
| `rollbackToPrevious()` | Trigger a manual rollback (reboots) |
| `confirmBoot()` | Manually confirm boot |
| `deleteRollbackBackup()` | Delete the backup file |

### Requirements

- `#define LITEOTA_USE_ROLLBACK` before `#include <LiteOTA.h>`
- LittleFS partition with **~500 KB free**
- New firmware must reach `setup()` — if it crashes earlier, only serial recovery works

### ⚠️ Notes

- 3 failed boots trigger rollback (configurable via `setMaxRetries()` — no, this is fixed at 3)
- Each OTA writes ~400 KB to flash — flash wear is a real consideration
- Rollback will **not** trigger if the new firmware never runs `begin()`

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

### Lifecycle

| Method | Returns | Description |
|--------|---------|-------------|
| `LiteOTA(version, url)` | — | Constructor |
| `begin()` | `void` | Initializes the state machine |
| `tick()` | `void` | ⚡ Must be called every loop |

### Control

| Method | Returns | Description |
|--------|---------|-------------|
| `requestUpdate()` | `void` | Trigger an OTA cycle |
| `abort()` | `void` | Cancel an in-flight update |
| `reset()` | `void` | Reset state and error |

### Configuration

| Method | Returns | Description |
|--------|---------|-------------|
| `setCheckInterval(sec)` | `void` | Scheduled checks (default 24 h) |
| `setMinFreeHeap(bytes)` | `void` | Heap guard (default 8 KB, 25 KB with TLS) |
| `setMaxRetries(n)` | `void` | Retries on failure (default 3) |
| `setChunkSize(bytes)` | `void` | Bytes per tick (default 512, max 512) |
| `setManifestUrl(url)` | `void` | Change manifest at runtime |

### Rollback

| Method | Returns | Description |
|--------|---------|-------------|
| `enableSafeRollback(bool)` | `void` | Enable/disable |
| `setRollbackTimeout(sec)` | `void` | Boot confirmation window |
| `setRollbackBackupPath(path)` | `void` | Backup location |
| `isRollbackAvailable()` | `bool` | Backup exists? |
| `rollbackToPrevious()` | `bool` | Manual rollback |
| `confirmBoot()` | `void` | Confirm successful boot |
| `deleteRollbackBackup()` | `void` | Remove backup |

### TLS / HTTPS

| Method | Returns | Description |
|--------|---------|-------------|
| `setInsecure()` | `void` | Skip cert validation |
| `setCACert(pem)` | `void` | Install CA cert |
| `isTLSEnabled()` | `bool` | TLS compiled in? |
| `setTLSBufferSizes(rx, tx)` | `void` | Manual BearSSL buffers |
| `enableMFLN(len)` | `void` | Negotiate smaller fragments |

### Callbacks

| Method | Returns | Description |
|--------|---------|-------------|
| `onProgress(cb)` | `void` | Called during download |
| `onStateChange(cb)` | `void` | Called on state transitions |
| `onBeforeRequest(cb)` | `void` | Pause services before an HTTP request |
| `onAfterRequest(cb)` | `void` | Resume services after an HTTP request |

### Status

| Method | Returns | Description |
|--------|---------|-------------|
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
| `SafeRollback` | Auto-rollback on boot failure |

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
[LiteOTA] Init. TLS:OFF RB:ON Heap:45216
[LiteOTA] State: FETCH_MANIFEST
[LiteOTA] State: PARSE_MANIFEST
[LiteOTA] Remote:1.1 Current:1.0 Type:JSON
[LiteOTA] State: BACKUP_FIRMWARE
[LiteOTA] Backing up 345920 bytes...
[LiteOTA] Backup complete.
[LiteOTA] State: OPEN_FIRMWARE
[LiteOTA] Downloading 345920 bytes...
[PROGRESS] 1024/345920 (0%)
...
[LiteOTA] State: FINALIZING
[LiteOTA] OK. Rebooting...
[LiteOTA] Boot pending (1/3)
[LiteOTA] Boot confirmed.
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
- **Rollback optional** — requires `#define LITEOTA_USE_ROLLBACK` + ~500 KB LittleFS
- **No signature verification** — don't use over untrusted networks
- **Single firmware URL** — no delta or multi-part updates
- **Buffer sizes fixed** — `_firmwareUrl[192]`, `_remoteVersion[16]`
- **One OTA at a time** — no concurrent downloads
- **Rollback can't recover if `begin()` never runs** — needs serial in that case

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