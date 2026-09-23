#ifndef LiteOTA_h
#define LiteOTA_h

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266httpUpdate.h>
#include <ArduinoJson.h>

// ═══════════════════════════════════════════════════════════════
//  Compile-time options
// ═══════════════════════════════════════════════════════════════

// Uncomment to enable TLS/HTTPS support.
// TLS costs ~25-35KB RAM during handshake. Consider using MFLN.
// #define LITEOTA_USE_TLS

#if defined(LITEOTA_USE_TLS)
  #include <WiFiClientSecure.h>
  #include <BearSSLHelpers.h>
  #define LITEOTA_DEFAULT_MIN_HEAP 25000
#else
  #define LITEOTA_DEFAULT_MIN_HEAP 8000
#endif

// Maximum firmware bytes read per tick() call.
// Larger = faster download, more stack usage. Must be >= 128.
#define LITEOTA_MAX_CHUNK 512

// ═══════════════════════════════════════════════════════════════
//  LiteOTA — Non-blocking OTA updater for ESP8266
//
//  Kitronic
//  info@kitronic.tech
//  www.kitronic.tech
//  https://github.com/kitronic/esp-lib-lite-ota
//
//  State-machine based, tick()-driven. Never blocks the main loop.
//  Reads JSON or plain-text manifest, auto-detects format.
//  Supports optional HTTPS with MFLN buffer tuning.
//  Designed for Web + MQTT + OTA projects on Wemos D1 mini / D1 R2.
//
//  MIT License — Copyright (c) 2024 Kitronic
// ═══════════════════════════════════════════════════════════════

enum class LiteOTAState : uint8_t {
    IDLE = 0,
    FETCH_MANIFEST,
    PARSE_MANIFEST,
    OPEN_FIRMWARE,
    DOWNLOADING,
    FINALIZING,
    ERROR_STATE,
    SUCCESS_REBOOT
};

enum class LiteOTAError : uint8_t {
    NONE = 0,
    WIFI_DOWN,
    LOW_HEAP,
    TLS_NOT_ENABLED,
    TLS_SETUP_FAIL,
    HTTP_MANIFEST_FAIL,
    PARSE_FAIL,
    SAME_VERSION,
    HTTP_FIRMWARE_FAIL,
    UPDATE_BEGIN_FAIL,
    UPDATE_WRITE_FAIL,
    TIMEOUT
};

typedef void (*LiteOTAProgressCallback)(size_t bytesReceived, size_t bytesTotal, uint8_t percent);
typedef void (*LiteOTAStateCallback)(LiteOTAState state, LiteOTAError error);
typedef void (*LiteOTAVoidCallback)();

class LiteOTA {
public:
    LiteOTA(const char* currentVersion, const char* manifestUrl);

    // ─── Lifecycle ───
    void begin();
    void tick();

    // ─── Configuration ───
    void setCheckInterval(uint32_t seconds);
    void setMinFreeHeap(size_t bytes);
    void setMaxRetries(uint8_t retries);
    void setChunkSize(size_t bytes);
    void setManifestUrl(const char* url);

    // ─── TLS / HTTPS ───
    void setInsecure();
    void setCACert(const char* caCert);
    bool isTLSEnabled() const;

    // ─── TLS buffer tuning (MFLN) ───
    void setTLSBufferSizes(uint16_t rx, uint16_t tx);
    void enableMFLN(uint16_t maxFragLen = 1024);

    // ─── Cooperative handoff ───
    void onBeforeRequest(LiteOTAVoidCallback cb);
    void onAfterRequest(LiteOTAVoidCallback cb);

    // ─── Control ───
    void requestUpdate();
    void abort();
    void reset();

    // ─── Status ───
    bool isUpdating() const;
    bool isError() const;
    LiteOTAState getState() const { return _state; }
    LiteOTAError getLastError() const { return _lastError; }
    const char* getStateName() const;
    const char* getLastErrorName() const;
    const char* getRemoteVersion() const { return _remoteVersion; }
    const char* getFirmwareUrl() const { return _firmwareUrl; }
    const char* getManifestType() const;
    uint8_t getProgressPercent() const { return _progressPercent; }
    size_t getBytesReceived() const { return _bytesReceived; }
    size_t getBytesTotal() const { return _bytesTotal; }

    // ─── Callbacks ───
    void onProgress(LiteOTAProgressCallback cb) { _progressCb = cb; }
    void onStateChange(LiteOTAStateCallback cb) { _stateCb = cb; }

private:
    // Config
    const char* _currentVersion;
    const char* _manifestUrl;
    uint32_t _checkInterval = 24UL * 3600UL;
    size_t   _minFreeHeap   = LITEOTA_DEFAULT_MIN_HEAP;
    uint8_t  _maxRetries    = 3;
    size_t   _chunkSize     = LITEOTA_MAX_CHUNK;

    // Runtime
    LiteOTAState  _state = LiteOTAState::IDLE;
    LiteOTAError  _lastError = LiteOTAError::NONE;
    unsigned long _stateEnteredAt = 0;
    unsigned long _lastCheckAt = 0;
    bool          _updateRequested = false;
    uint8_t       _retries = 0;
    bool          _servicesPaused = false;   // لحالة beforeCb/afterCb

    // Parsed manifest
    char _remoteVersion[16];
    char _firmwareUrl[192];
    enum ManifestType { MT_UNKNOWN = 0, MT_JSON, MT_TEXT };
    ManifestType _manifestType = MT_UNKNOWN;

    // HTTP / download — separate plain and secure clients
    WiFiClient _plainClient;
#if defined(LITEOTA_USE_TLS)
    WiFiClientSecure   _secureClient;
    BearSSL::X509List* _caCertList = nullptr;
    bool               _insecure   = false;
    uint16_t           _tlsRxBuffer = 16384;
    uint16_t           _tlsTxBuffer = 512;
    uint16_t           _mfln        = 0;
#endif

    HTTPClient    _http;
    bool          _httpOpen = false;
    size_t        _bytesReceived = 0;
    size_t        _bytesTotal    = 0;
    uint8_t       _progressPercent = 0;
    unsigned long _lastDataAt = 0;

    // Callbacks
    LiteOTAProgressCallback _progressCb = nullptr;
    LiteOTAStateCallback    _stateCb    = nullptr;
    LiteOTAVoidCallback     _beforeCb   = nullptr;
    LiteOTAVoidCallback     _afterCb    = nullptr;

    // Internals
    void _setState(LiteOTAState s, LiteOTAError e = LiteOTAError::NONE);
    void _fail(LiteOTAError e);
    bool _checkHeap() const;
    bool _shouldCheck() const;
    bool _isHttps(const char* url) const;
    WiFiClient* _pickClient(const char* url);
    bool _prepareSecureClient();
    void _invokeBefore();
    void _invokeAfter();

    // State handlers
    void _stepFetchManifest();
    void _stepParseManifest();
    void _stepOpenFirmware();
    void _stepDownload();
    void _stepFinalize();

    // Helpers
    ManifestType _detectType(WiFiClient* stream);
    bool _parseJson(WiFiClient* stream);
    bool _parseText(WiFiClient* stream);
};

#endif