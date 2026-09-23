# Changelog

All notable changes to **LiteOTA** are documented here.
Format follows [Keep a Changelog](https://keepachangelog.com/en/1.0.0/).
Versioning follows [Semantic Versioning](https://semver.org/).

---

## [1.0.0] — 2024-XX-XX

### Added

#### Core
- Non-blocking `tick()` API — state-machine driven OTA
- JSON manifest support (`version`, `url`, optional `notes`)
- Plain-text manifest support (line 1 = version, line 2 = URL)
- Automatic manifest type detection (JSON vs TEXT)
- Version comparison and update application via `ESP8266HTTPUpdate`

#### Memory safety
- Zero `String` usage — all fixed-size `char[]` buffers
- `StaticJsonDocument<512>` — stack-allocated JSON parsing
- Heap guard (`setMinFreeHeap`) — refuses update if free heap below threshold
- No `malloc`, no heap fragmentation

#### TLS / HTTPS (optional)
- Compile-time flag `LITEOTA_USE_TLS`
- `setInsecure()` — skip certificate validation
- `setCACert()` — install a CA certificate (PEM)
- **MFLN support** (`enableMFLN`) — shrinks TLS buffers from 16 KB to 1 KB
- `setTLSBufferSizes()` — manual BearSSL buffer tuning
- Separate plain + secure HTTP clients (mixed-mode safe)
- Automatic heap guard raise (25 KB) when TLS is enabled

#### Control
- Scheduled checks (`setCheckInterval`)
- Manual trigger (`requestUpdate()`)
- Abort mid-download (`abort()`)
- Configurable retry count (`setMaxRetries`)
- Configurable chunk size (`setChunkSize`, clamped to 512 B)
- Runtime manifest URL change (`setManifestUrl()`)

#### Cooperative handoff
- `onBeforeRequest()` — pause MQTT or other services before an HTTP request
- `onAfterRequest()` — resume services after the request
- `_servicesPaused` flag prevents double-firing

#### Observability
- Progress callback (`onProgress`)
- State-change callback (`onStateChange`)
- `getState()`, `getLastError()`, `getProgressPercent()`
- Human-readable `getStateName()` / `getLastErrorName()`
- `getRemoteVersion()`, `getFirmwareUrl()`, `getManifestType()`

#### Integration
- Works alongside `ESP8266WebServer` without blocking
- Works alongside `PubSubClient` (MQTT) without disconnects
- Compatible with Deep Sleep projects
- Tested on Wemos D1 mini / D1 R2

#### Examples
- `Basic` — check once at boot
- `Scheduled` — daily check at fixed hour (NTP)
- `ManualTrigger` — HTTP endpoint + MQTT trigger
- `NonBlocking` — full Web + MQTT + OTA integration
- `HTTPS-MixedMode` — TLS manifest + HTTP firmware + MFLN + handoff

#### Tests
- Native tests (`test/native/`) — g++ / make, no PlatformIO required
- Embedded tests (`test/embedded/`) — 6 sketches for on-device verification
- Local test server (`test/server/`)

#### Misc
- `library.properties` for Arduino IDE
- `library.json` for PlatformIO
- Sample manifests in `extras/server/`
- Server setup guide
- MIT license

### Known Limitations
- HTTP only by default — HTTPS requires `#define LITEOTA_USE_TLS`
- No firmware signature verification
- No rollback mechanism
- Buffer sizes fixed at compile time (`_firmwareUrl[192]`)
- Final apply step (`Update.end()`) triggers an unavoidable reboot

---

## [Unreleased]

### Planned
- MD5/SHA256 checksum verification
- Runtime buffer size tuning
- Multi-file / SPIFFS update support
- Fallback AP mode on boot failure
- ESP32 port

---

## Maintainers

**Kitronic** — [info@kitronic.tech](mailto:info@kitronic.tech) · [www.kitronic.tech](https://www.kitronic.tech)