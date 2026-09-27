/**
 * LiteOTA — Non-blocking OTA updater for ESP8266
 *
 * Kitronic
 * info@kitronic.tech
 * www.kitronic.tech
 * https://github.com/kitronic/esp-lib-lite-ota
 *
 * MIT License — Copyright (c) 2024 Kitronic
 */

#include "LiteOTA.h"

extern "C"
{
#include <user_interface.h>
}

// ─────────────────────────────────────────────
// RTC memory layout for boot-loop detection
// ─────────────────────────────────────────────
struct LiteOTARTCData
{
    uint32_t magic;    // 0x4C697445 ("LiTe")
    uint8_t state;     // 0=clean, 1=update pending, 2=rollback in progress
    uint8_t bootCount; // consecutive boots without confirmation
    uint16_t reserved; // align to 4 bytes
};

static LiteOTARTCData _rtc;
#define LITEOTA_RTC_OFFSET 128
#define LITEOTA_RTC_MAGIC 0x4C697445UL
#define LITEOTA_MAX_BOOTS 3

// ─────────────────────────────────────────────
// Constructor / Destructor
// ─────────────────────────────────────────────
LiteOTA::LiteOTA(const char *currentVersion, const char *manifestUrl)
{
    _currentVersion = currentVersion;
    _manifestUrl = manifestUrl;

    _remoteVersion[0] = '\0';
    _firmwareUrl[0] = '\0';

    // تهيئة مصفوفة المسار
    strncpy(_rollbackPath, "/liteota/backup.bin", sizeof(_rollbackPath) - 1);
    _rollbackPath[sizeof(_rollbackPath) - 1] = '\0';

#if defined(LITEOTA_USE_WEB)
    _webUser[0] = '\0';
    _webPass[0] = '\0';
    _webPrefix[0] = '\0';
#endif
}

LiteOTA::~LiteOTA()
{
    if (_httpOpen)
    {
        _http.end();
        _httpOpen = false;
    }
#if defined(LITEOTA_USE_ROLLBACK)
    if (_backupFile)
        _backupFile.close();
#endif
#if defined(LITEOTA_USE_TLS)
    if (_caCertList)
    {
        delete _caCertList;
        _caCertList = nullptr;
    }
#endif
#if defined(LITEOTA_USE_WEB)
    // We don't own the server, just release the pointer
    _webServer = nullptr;
#endif
}

// ─────────────────────────────────────────────
// TLS configuration
// ─────────────────────────────────────────────
bool LiteOTA::isTLSEnabled() const
{
#if defined(LITEOTA_USE_TLS)
    return true;
#else
    return false;
#endif
}

void LiteOTA::setInsecure()
{
#if defined(LITEOTA_USE_TLS)
    _insecure = true;
    if (_caCertList)
    {
        delete _caCertList;
        _caCertList = nullptr;
    }
    Serial.println(F("[LiteOTA] TLS: insecure mode"));
#else
    Serial.println(F("[LiteOTA] TLS not compiled. Define LITEOTA_USE_TLS."));
#endif
}

void LiteOTA::setCACert(const char *caCert)
{
#if defined(LITEOTA_USE_TLS)
    if (_caCertList)
    {
        delete _caCertList;
        _caCertList = nullptr;
    }
    if (caCert)
    {
        _caCertList = new BearSSL::X509List(caCert);
        _insecure = false;
        Serial.println(F("[LiteOTA] TLS: CA cert installed"));
    }
    else
    {
        _insecure = true;
        Serial.println(F("[LiteOTA] TLS: null cert, using insecure"));
    }
#else
    (void)caCert;
    Serial.println(F("[LiteOTA] TLS not compiled. Define LITEOTA_USE_TLS."));
#endif
}

void LiteOTA::setTLSBufferSizes(uint16_t rx, uint16_t tx)
{
#if defined(LITEOTA_USE_TLS)
    _tlsRxBuffer = rx;
    _tlsTxBuffer = tx;
    Serial.printf_P(PSTR("[LiteOTA] TLS buffers: rx=%u tx=%u\n"), rx, tx);
#else
    (void)rx;
    (void)tx;
#endif
}

void LiteOTA::enableMFLN(uint16_t maxFragLen)
{
#if defined(LITEOTA_USE_TLS)
    _mfln = maxFragLen;
    if (maxFragLen > 0)
    {
        if (_tlsRxBuffer > maxFragLen)
            _tlsRxBuffer = maxFragLen;
        if (_tlsTxBuffer > 512)
            _tlsTxBuffer = 512;
    }
    Serial.printf_P(PSTR("[LiteOTA] MFLN=%u (rx=%u tx=%u)\n"),
                    _mfln, _tlsRxBuffer, _tlsTxBuffer);
#else
    (void)maxFragLen;
#endif
}

// ─────────────────────────────────────────────
// Cooperative handoff
// ─────────────────────────────────────────────
void LiteOTA::onBeforeRequest(LiteOTAVoidCallback cb) { _beforeCb = cb; }
void LiteOTA::onAfterRequest(LiteOTAVoidCallback cb) { _afterCb = cb; }

void LiteOTA::_invokeBefore()
{
    if (_beforeCb && !_servicesPaused)
    {
        _beforeCb();
        _servicesPaused = true;
    }
}

void LiteOTA::_invokeAfter()
{
    if (_afterCb && _servicesPaused)
    {
        _afterCb();
        _servicesPaused = false;
    }
}

// ─────────────────────────────────────────────
// Safe Rollback — configuration
// ─────────────────────────────────────────────
void LiteOTA::enableSafeRollback(bool enable)
{
    _rollbackEnabled = enable;
    Serial.printf_P(PSTR("[LiteOTA] Rollback: %s\n"), enable ? "ON" : "OFF");
}

void LiteOTA::setRollbackTimeout(uint32_t seconds)
{
    _rollbackTimeout = seconds;
    Serial.printf_P(PSTR("[LiteOTA] Rollback timeout: %u s\n"), seconds);
}

void LiteOTA::setRollbackBackupPath(const char *path)
{
    if (!path)
        return;
    strncpy(_rollbackPath, path, sizeof(_rollbackPath) - 1);
    _rollbackPath[sizeof(_rollbackPath) - 1] = '\0';
}

void LiteOTA::setSketchFlashAddress(uint32_t addr)
{
    _sketchFlashAddr = addr;
    Serial.printf_P(PSTR("[LiteOTA] Sketch flash addr: 0x%06X\n"), addr);
}

bool LiteOTA::isRollbackAvailable() const
{
#if defined(LITEOTA_USE_ROLLBACK)
    if (!_fsMounted)
        return false;
    return LittleFS.exists(_rollbackPath);
#else
    return false;
#endif
}

void LiteOTA::deleteRollbackBackup()
{
#if defined(LITEOTA_USE_ROLLBACK)
    if (_fsMounted && LittleFS.exists(_rollbackPath))
    {
        LittleFS.remove(_rollbackPath);
        Serial.println(F("[LiteOTA] Backup deleted."));
    }
#endif
}

// ─────────────────────────────────────────────
// Safe Rollback — RTC helpers
// ─────────────────────────────────────────────
bool LiteOTA::_loadRTC()
{
    if (!ESP.rtcUserMemoryRead(LITEOTA_RTC_OFFSET,
                               (uint32_t *)&_rtc, sizeof(_rtc)))
    {
        return false;
    }
    if (_rtc.magic != LITEOTA_RTC_MAGIC)
    {
        _rtc.magic = LITEOTA_RTC_MAGIC;
        _rtc.state = 0;
        _rtc.bootCount = 0;
        _rtc.reserved = 0;
        return _saveRTC();
    }
    return true;
}

bool LiteOTA::_saveRTC()
{
    return ESP.rtcUserMemoryWrite(LITEOTA_RTC_OFFSET,
                                  (uint32_t *)&_rtc, sizeof(_rtc));
}

// ─────────────────────────────────────────────
// Safe Rollback — restore firmware from LittleFS
// ─────────────────────────────────────────────
bool LiteOTA::_restoreFirmware()
{
#if defined(LITEOTA_USE_ROLLBACK)
    if (!_fsMounted)
        return false;
    if (!LittleFS.exists(_rollbackPath))
    {
        Serial.println(F("[LiteOTA] No backup file found."));
        return false;
    }

    File f = LittleFS.open(_rollbackPath, "r");
    if (!f)
    {
        Serial.println(F("[LiteOTA] Failed to open backup."));
        return false;
    }

    size_t sz = f.size();
    if (sz < 1024 || sz > 4000000)
    {
        Serial.printf_P(PSTR("[LiteOTA] Backup size suspicious: %u\n"), sz);
        f.close();
        return false;
    }

    // ⚡ Verify firmware magic byte (0xE9 = ESP8266 image)
    uint8_t magic = f.read();
    f.seek(0, SeekSet);
    if (magic != 0xE9)
    {
        Serial.printf_P(PSTR("[LiteOTA] Invalid firmware magic: 0x%02X\n"), magic);
        f.close();
        return false;
    }

    Serial.printf_P(PSTR("[LiteOTA] Restoring %u bytes...\n"), sz);

    if (!Update.begin(sz))
    {
        Serial.println(F("[LiteOTA] Update.begin failed."));
        f.close();
        return false;
    }

    uint8_t buf[1024];
    while (f.available())
    {
        size_t n = f.read(buf, sizeof(buf));
        if (n == 0)
            break;
        if (Update.write(buf, n) != n)
        {
            Serial.println(F("[LiteOTA] Update.write failed."));
            Update.end(false);
            f.close();
            return false;
        }
        yield();
    }

    bool ok = Update.end(true);
    f.close();
    return ok;
#else
    return false;
#endif
}

// ─────────────────────────────────────────────
// Safe Rollback — boot loop detection
// ─────────────────────────────────────────────
void LiteOTA::_checkBootLoop()
{
    if (!_rollbackEnabled)
        return;
    if (!_loadRTC())
        return;

    if (_rtc.state == 1)
    {
        _rtc.bootCount++;
        _saveRTC();

        Serial.printf_P(PSTR("[LiteOTA] Boot pending (%u/%u)\n"),
                        _rtc.bootCount, LITEOTA_MAX_BOOTS);

        if (_rtc.bootCount >= LITEOTA_MAX_BOOTS)
        {
            Serial.println(F("[LiteOTA] BOOT LOOP DETECTED — rolling back"));
            _rtc.state = 2;
            _saveRTC();

            if (_restoreFirmware())
            {
                Serial.println(F("[LiteOTA] Rollback OK. Rebooting..."));
                _rtc.state = 0;
                _rtc.bootCount = 0;
                _saveRTC();
                delay(200);
                ESP.restart();
            }
            else
            {
                Serial.println(F("[LiteOTA] Rollback FAILED"));
                _lastError = LiteOTAError::ROLLBACK_FAIL;
                _rtc.state = 0;
                _rtc.bootCount = 0;
                _saveRTC();
            }
        }
        else
        {
            _bootPending = true;
        }
    }
    else if (_rtc.state == 2)
    {
        // Stale rollback state — clear
        _rtc.state = 0;
        _rtc.bootCount = 0;
        _saveRTC();
    }
}

// ─────────────────────────────────────────────
// Safe Rollback — confirm successful boot
// ─────────────────────────────────────────────
void LiteOTA::confirmBoot()
{
    if (!_rollbackEnabled)
        return;
    if (!_loadRTC())
        return;
    if (_rtc.state == 1 || _rtc.state == 2)
    {
        _rtc.state = 0;
        _rtc.bootCount = 0;
        _saveRTC();
        Serial.println(F("[LiteOTA] Boot confirmed."));
    }
    _bootPending = false;
}

// ─────────────────────────────────────────────
// Safe Rollback — manual rollback trigger
// ─────────────────────────────────────────────
bool LiteOTA::rollbackToPrevious()
{
    if (!_rollbackEnabled)
    {
        Serial.println(F("[LiteOTA] Rollback not enabled."));
        return false;
    }
    if (!isRollbackAvailable())
    {
        Serial.println(F("[LiteOTA] No backup available."));
        _lastError = LiteOTAError::NO_BACKUP;
        return false;
    }
    Serial.println(F("[LiteOTA] Manual rollback..."));
    if (_restoreFirmware())
    {
        delay(200);
        ESP.restart();
        return true;
    }
    _lastError = LiteOTAError::ROLLBACK_FAIL;
    return false;
}

// ─────────────────────────────────────────────
// Client selection
// ─────────────────────────────────────────────
bool LiteOTA::_isHttps(const char *url) const
{
    return url && (strncmp(url, "https://", 8) == 0);
}

WiFiClient *LiteOTA::_pickClient(const char *url)
{
#if defined(LITEOTA_USE_TLS)
    if (_isHttps(url))
        return &_secureClient;
#endif
    (void)url;
    return &_plainClient;
}

bool LiteOTA::_prepareSecureClient()
{
#if defined(LITEOTA_USE_TLS)
    if (_insecure || !_caCertList)
    {
        _secureClient.setInsecure();
    }
    else
    {
        _secureClient.setTrustAnchors(_caCertList);
    }
    if (_mfln > 0)
    {
        _secureClient.setMFLN(_mfln);
    }
    _secureClient.setBufferSizes(_tlsRxBuffer, _tlsTxBuffer);
#endif
    return true;
}

// ─────────────────────────────────────────────
// Lifecycle
// ─────────────────────────────────────────────
void LiteOTA::begin()
{
    _lastCheckAt = millis();
    _bootAt = millis();

#if defined(LITEOTA_USE_ROLLBACK)
    // ⚡ Auto-detect the current running firmware flash address
    if (_sketchFlashAddr == LITEOTA_FLASH_ADDR_AUTO)
    {
        _sketchFlashAddr = system_get_userbin_addr();
        Serial.printf_P(PSTR("[LiteOTA] Auto sketch addr: 0x%06X\n"),
                        _sketchFlashAddr);
    }

    if (_rollbackEnabled)
    {
        if (!LittleFS.begin())
        {
            Serial.println(F("[LiteOTA] FS mount failed — rollback off"));
            _rollbackEnabled = false;
            _lastError = LiteOTAError::FS_MOUNT_FAIL;
        }
        else
        {
            _fsMounted = true;
            _checkBootLoop();
        }
    }
#endif

    _setState(LiteOTAState::IDLE);
    Serial.printf_P(PSTR("[LiteOTA] Init. TLS:%s RB:%s Heap:%u\n"),
                    isTLSEnabled() ? "ON" : "OFF",
                    _rollbackEnabled ? "ON" : "OFF",
                    ESP.getFreeHeap());
}

void LiteOTA::tick()
{
    // Auto-confirm boot after timeout
    if (_rollbackEnabled && _bootPending &&
        millis() - _bootAt > (_rollbackTimeout * 1000UL))
    {
        confirmBoot();
    }

    // Per-state timeout guard (60s)
    if (_state != LiteOTAState::IDLE &&
        _state != LiteOTAState::ERROR_STATE &&
        _state != LiteOTAState::SUCCESS_REBOOT &&
        millis() - _stateEnteredAt > 60000UL)
    {
        _fail(LiteOTAError::TIMEOUT);
        return;
    }

    switch (_state)
    {
    case LiteOTAState::IDLE:
        if (_updateRequested)
        {
            _updateRequested = false;
            if (!_checkHeap())
            {
                _fail(LiteOTAError::LOW_HEAP);
                return;
            }
            _setState(LiteOTAState::FETCH_MANIFEST);
        }
        else if (_shouldCheck())
        {
            if (!_checkHeap())
                return;
            _lastCheckAt = millis();
            _setState(LiteOTAState::FETCH_MANIFEST);
        }
        break;

    case LiteOTAState::FETCH_MANIFEST:
        _stepFetchManifest();
        break;
    case LiteOTAState::PARSE_MANIFEST:
        _stepParseManifest();
        break;
    case LiteOTAState::BACKUP_FIRMWARE:
        _stepBackupFirmware();
        break;
    case LiteOTAState::OPEN_FIRMWARE:
        _stepOpenFirmware();
        break;
    case LiteOTAState::DOWNLOADING:
        _stepDownload();
        break;
    case LiteOTAState::FINALIZING:
        _stepFinalize();
        break;

    case LiteOTAState::ERROR_STATE:
        if (millis() - _stateEnteredAt > 30000UL)
        {
            _setState(LiteOTAState::IDLE);
        }
        break;

    case LiteOTAState::SUCCESS_REBOOT:
        break;
    }
}

// ─────────────────────────────────────────────
// Step: Backup firmware (rollback only)
// ─────────────────────────────────────────────
void LiteOTA::_stepBackupFirmware()
{
#if defined(LITEOTA_USE_ROLLBACK)
    if (!_fsMounted)
    {
        _fail(LiteOTAError::FS_MOUNT_FAIL);
        return;
    }

    // First call: initialize
    if (_backupAddr == 0)
    {
        _backupSize = ESP.getSketchSize();
        if (_backupSize < 1024)
        {
            _fail(LiteOTAError::BACKUP_FAIL);
            return;
        }

        if (!LittleFS.exists("/liteota"))
        {
            LittleFS.mkdir("/liteota");
        }

        FSInfo info;
        LittleFS.info(info);
        if (info.totalBytes - info.usedBytes < _backupSize + 4096)
        {
            Serial.println(F("[LiteOTA] Not enough FS space for backup"));
            _fail(LiteOTAError::BACKUP_FAIL);
            return;
        }

        if (LittleFS.exists(_rollbackPath))
        {
            LittleFS.remove(_rollbackPath);
        }

        _backupFile = LittleFS.open(_rollbackPath, "w");
        if (!_backupFile)
        {
            _fail(LiteOTAError::BACKUP_FAIL);
            return;
        }

        Serial.printf_P(PSTR("[LiteOTA] Backing up %u bytes from 0x%06X...\n"),
                        _backupSize, _sketchFlashAddr);
    }

    // Read one chunk from flash, write to FS
    // Read one chunk from flash, write to FS
    // ⚡ Core 3.x flashRead requires uint32_t* (aligned to 4 bytes)
    uint32_t buf[256]; // 1024 bytes
    size_t toRead = (_backupSize - _backupAddr) > sizeof(buf)
                        ? sizeof(buf)
                        : (_backupSize - _backupAddr);

    // Round up to multiple of 4 for flashRead
    size_t aligned = (toRead + 3u) & ~3u;
    if (aligned > sizeof(buf))
        aligned = sizeof(buf);

    if (!ESP.flashRead(_sketchFlashAddr + _backupAddr, buf, aligned))
    {
        _backupFile.close();
        _fail(LiteOTAError::BACKUP_FAIL);
        return;
    }

    if (_backupFile.write((const uint8_t *)buf, toRead) != toRead)
    {
        _backupFile.close();
        _fail(LiteOTAError::BACKUP_FAIL);
        return;
    }

    _backupAddr += toRead;

    // Progress
    if (_progressCb && _backupSize > 0)
    {
        uint8_t pct = (uint8_t)((_backupAddr * 100UL) / _backupSize);
        if (pct != _progressPercent)
        {
            _progressPercent = pct;
            _progressCb(_backupAddr, _backupSize, pct);
        }
    }

    // Done?
    if (_backupAddr >= _backupSize)
    {
        _backupFile.close();
        _backupAddr = 0;
        _progressPercent = 0;

        Serial.println(F("[LiteOTA] Backup complete."));

        // ⚡ We do NOT set the RTC flag here — it goes in _stepFinalize()
        _setState(LiteOTAState::OPEN_FIRMWARE);
    }
#else
    _setState(LiteOTAState::OPEN_FIRMWARE);
#endif
}

// ─────────────────────────────────────────────
// Config
// ─────────────────────────────────────────────
void LiteOTA::setCheckInterval(uint32_t s) { _checkInterval = s; }
void LiteOTA::setMinFreeHeap(size_t b) { _minFreeHeap = b; }
void LiteOTA::setMaxRetries(uint8_t r) { _maxRetries = r; }
void LiteOTA::setManifestUrl(const char *u) { _manifestUrl = u; }

void LiteOTA::setChunkSize(size_t s)
{
    if (s == 0)
        s = 1;
    if (s > LITEOTA_MAX_CHUNK)
        s = LITEOTA_MAX_CHUNK;
    _chunkSize = s;
}

// ─────────────────────────────────────────────
// Control
// ─────────────────────────────────────────────
void LiteOTA::requestUpdate()
{
    if (isUpdating())
    {
        Serial.println(F("[LiteOTA] Already updating."));
        return;
    }
    _updateRequested = true;
    Serial.println(F("[LiteOTA] Update requested."));
}

void LiteOTA::abort()
{
    if (_httpOpen)
    {
        _http.end();
        _httpOpen = false;
    }

#if defined(LITEOTA_USE_ROLLBACK)
    if (_backupFile)
    {
        _backupFile.close();
        _backupAddr = 0;
    }
    // ⚡ Clear stale RTC flag if it was set by a failed attempt
    if (_rollbackEnabled && _fsMounted)
    {
        if (_loadRTC())
        {
            if (_rtc.state == 1)
            {
                _rtc.state = 0;
                _rtc.bootCount = 0;
                _saveRTC();
            }
        }
    }
#endif

    Update.end(false);
    _bytesReceived = 0;
    _bytesTotal = 0;
    _progressPercent = 0;
    _invokeAfter();
    _setState(LiteOTAState::IDLE);
    Serial.println(F("[LiteOTA] Aborted."));
}

void LiteOTA::reset()
{
    abort();
    _lastError = LiteOTAError::NONE;
    _retries = 0;
}

// ─────────────────────────────────────────────
// Status
// ─────────────────────────────────────────────
bool LiteOTA::isUpdating() const
{
    return _state != LiteOTAState::IDLE &&
           _state != LiteOTAState::ERROR_STATE &&
           _state != LiteOTAState::SUCCESS_REBOOT;
}

bool LiteOTA::isError() const { return _state == LiteOTAState::ERROR_STATE; }

const char *LiteOTA::getStateName() const
{
    switch (_state)
    {
    case LiteOTAState::IDLE:
        return "IDLE";
    case LiteOTAState::FETCH_MANIFEST:
        return "FETCH_MANIFEST";
    case LiteOTAState::PARSE_MANIFEST:
        return "PARSE_MANIFEST";
    case LiteOTAState::BACKUP_FIRMWARE:
        return "BACKUP_FIRMWARE";
    case LiteOTAState::OPEN_FIRMWARE:
        return "OPEN_FIRMWARE";
    case LiteOTAState::DOWNLOADING:
        return "DOWNLOADING";
    case LiteOTAState::FINALIZING:
        return "FINALIZING";
    case LiteOTAState::ERROR_STATE:
        return "ERROR";
    case LiteOTAState::SUCCESS_REBOOT:
        return "SUCCESS";
    }
    return "UNKNOWN";
}

const char *LiteOTA::getLastErrorName() const
{
    switch (_lastError)
    {
    case LiteOTAError::NONE:
        return "NONE";
    case LiteOTAError::WIFI_DOWN:
        return "WIFI_DOWN";
    case LiteOTAError::LOW_HEAP:
        return "LOW_HEAP";
    case LiteOTAError::TLS_NOT_ENABLED:
        return "TLS_NOT_ENABLED";
    case LiteOTAError::TLS_SETUP_FAIL:
        return "TLS_SETUP_FAIL";
    case LiteOTAError::HTTP_MANIFEST_FAIL:
        return "HTTP_MANIFEST_FAIL";
    case LiteOTAError::PARSE_FAIL:
        return "PARSE_FAIL";
    case LiteOTAError::SAME_VERSION:
        return "SAME_VERSION";
    case LiteOTAError::BACKUP_FAIL:
        return "BACKUP_FAIL";
    case LiteOTAError::ROLLBACK_FAIL:
        return "ROLLBACK_FAIL";
    case LiteOTAError::NO_BACKUP:
        return "NO_BACKUP";
    case LiteOTAError::FS_MOUNT_FAIL:
        return "FS_MOUNT_FAIL";
    case LiteOTAError::HTTP_FIRMWARE_FAIL:
        return "HTTP_FIRMWARE_FAIL";
    case LiteOTAError::UPDATE_BEGIN_FAIL:
        return "UPDATE_BEGIN_FAIL";
    case LiteOTAError::UPDATE_WRITE_FAIL:
        return "UPDATE_WRITE_FAIL";
    case LiteOTAError::TIMEOUT:
        return "TIMEOUT";
    }
    return "UNKNOWN";
}

const char *LiteOTA::getManifestType() const
{
    switch (_manifestType)
    {
    case MT_JSON:
        return "JSON";
    case MT_TEXT:
        return "TEXT";
    default:
        return "UNKNOWN";
    }
}

// ─────────────────────────────────────────────
// Internals
// ─────────────────────────────────────────────
void LiteOTA::_setState(LiteOTAState s, LiteOTAError e)
{
    _state = s;
    _stateEnteredAt = millis();
    if (e != LiteOTAError::NONE)
        _lastError = e;
    if (_stateCb)
        _stateCb(_state, _lastError);
    Serial.printf_P(PSTR("[LiteOTA] State: %s\n"), getStateName());
}

void LiteOTA::_fail(LiteOTAError e)
{
    _invokeAfter();

    if (_httpOpen)
    {
        _http.end();
        _httpOpen = false;
    }

    // ⚡ Cleanup backup file (if rollback was mid-backup)
#if defined(LITEOTA_USE_ROLLBACK)
    if (_backupFile)
    {
        _backupFile.close();
    }
    _backupAddr = 0;

    // ⚡ Clear stale RTC flag (only if we hadn't reached finalize yet)
    if (_rollbackEnabled && _fsMounted)
    {
        if (_loadRTC())
        {
            if (_rtc.state == 1)
            {
                _rtc.state = 0;
                _rtc.bootCount = 0;
                _saveRTC();
            }
        }
    }
#endif

    _lastError = e;
    Serial.printf_P(PSTR("[LiteOTA] Error: %s\n"), getLastErrorName());

    _retries++;
    if (_retries >= _maxRetries)
    {
        Serial.println(F("[LiteOTA] Max retries reached."));
        _setState(LiteOTAState::ERROR_STATE, e);
        _retries = 0;
    }
    else
    {
        _updateRequested = true;
        _setState(LiteOTAState::IDLE, e);
    }
}

bool LiteOTA::_checkHeap() const
{
    size_t h = ESP.getFreeHeap();
    if (h < _minFreeHeap)
    {
        Serial.printf_P(PSTR("[LiteOTA] Heap too low: %u < %u\n"), h, _minFreeHeap);
        return false;
    }
    return true;
}

bool LiteOTA::_shouldCheck() const
{
    return (millis() - _lastCheckAt) >= (_checkInterval * 1000UL);
}

// ─────────────────────────────────────────────
// Step: Fetch Manifest
// ─────────────────────────────────────────────
void LiteOTA::_stepFetchManifest()
{
    if (WiFi.status() != WL_CONNECTED)
    {
        _fail(LiteOTAError::WIFI_DOWN);
        return;
    }

    const char *url = _manifestUrl;
    WiFiClient *client = _pickClient(url);

#if defined(LITEOTA_USE_TLS)
    if (_isHttps(url))
        _prepareSecureClient();
#endif

    _invokeBefore();

    _http.begin(*client, url);
    _http.setUserAgent(F("LiteOTA/1.0 (Kitronic)"));
    _http.setTimeout(10000);

    int code = _http.GET();
    if (code != HTTP_CODE_OK)
    {
        _http.end();
        _invokeAfter();
        _fail(LiteOTAError::HTTP_MANIFEST_FAIL);
        return;
    }

    _httpOpen = true;
    _setState(LiteOTAState::PARSE_MANIFEST);
}

// ─────────────────────────────────────────────
// Step: Parse Manifest
// ─────────────────────────────────────────────
void LiteOTA::_stepParseManifest()
{
    if (!_httpOpen)
    {
        _fail(LiteOTAError::HTTP_MANIFEST_FAIL);
        return;
    }

    WiFiClient *stream = _http.getStreamPtr();
    _manifestType = _detectType(stream);

    bool ok = false;
    switch (_manifestType)
    {
    case MT_JSON:
        ok = _parseJson(stream);
        break;
    case MT_TEXT:
        ok = _parseText(stream);
        break;
    default:
        break;
    }

    _http.end();
    _httpOpen = false;
    _invokeAfter();

    if (!ok)
    {
        _fail(LiteOTAError::PARSE_FAIL);
        return;
    }

    Serial.printf_P(PSTR("[LiteOTA] Remote:%s Current:%s Type:%s\n"),
                    _remoteVersion, _currentVersion, getManifestType());

    if (strcmp(_remoteVersion, _currentVersion) == 0)
    {
        _retries = 0;
        _setState(LiteOTAState::IDLE, LiteOTAError::SAME_VERSION);
        return;
    }

    if (!_checkHeap())
    {
        _fail(LiteOTAError::LOW_HEAP);
        return;
    }

#if defined(LITEOTA_USE_ROLLBACK)
    if (_rollbackEnabled)
    {
        _setState(LiteOTAState::BACKUP_FIRMWARE);
    }
    else
    {
        _setState(LiteOTAState::OPEN_FIRMWARE);
    }
#else
    _setState(LiteOTAState::OPEN_FIRMWARE);
#endif
}

// ─────────────────────────────────────────────
// Step: Open Firmware (start download)
// ─────────────────────────────────────────────
void LiteOTA::_stepOpenFirmware()
{
    const char *url = _firmwareUrl;
    WiFiClient *client = _pickClient(url);

#if defined(LITEOTA_USE_TLS)
    if (_isHttps(url))
        _prepareSecureClient();
#endif

    _invokeBefore();

    _http.begin(*client, url);
    _http.setUserAgent(F("LiteOTA/1.0 (Kitronic)"));
    _http.setTimeout(15000);

    int code = _http.GET();
    if (code != HTTP_CODE_OK)
    {
        _http.end();
        _invokeAfter();
        _fail(LiteOTAError::HTTP_FIRMWARE_FAIL);
        return;
    }

    int len = _http.getSize();
    if (len <= 0)
    {
        _http.end();
        _invokeAfter();
        _fail(LiteOTAError::HTTP_FIRMWARE_FAIL);
        return;
    }

    _bytesTotal = (size_t)len;
    _bytesReceived = 0;
    _progressPercent = 0;

    if (!Update.begin(_bytesTotal))
    {
        _http.end();
        _invokeAfter();
        _fail(LiteOTAError::UPDATE_BEGIN_FAIL);
        return;
    }

    _httpOpen = true;
    _lastDataAt = millis();

    Serial.printf_P(PSTR("[LiteOTA] Downloading %u bytes...\n"), _bytesTotal);
    _setState(LiteOTAState::DOWNLOADING);
}

// ─────────────────────────────────────────────
// Step: Download (one chunk per tick)
// ─────────────────────────────────────────────
void LiteOTA::_stepDownload()
{
    if (!_httpOpen)
    {
        _fail(LiteOTAError::HTTP_FIRMWARE_FAIL);
        return;
    }

    WiFiClient *stream = _http.getStreamPtr();
    size_t avail = stream->available();

    if (avail > 0)
    {
        size_t toRead = avail > _chunkSize ? _chunkSize : avail;
        uint8_t buf[LITEOTA_MAX_CHUNK];
        if (toRead > sizeof(buf))
            toRead = sizeof(buf);

        size_t got = stream->readBytes(buf, toRead);
        if (got > 0)
        {
            size_t written = Update.write(buf, got);
            if (written != got)
            {
                _http.end();
                _httpOpen = false;
                _invokeAfter();
                _fail(LiteOTAError::UPDATE_WRITE_FAIL);
                return;
            }
            _bytesReceived += got;
            _lastDataAt = millis();

            if (_bytesTotal > 0)
            {
                uint8_t pct = (uint8_t)((_bytesReceived * 100UL) / _bytesTotal);
                if (pct != _progressPercent)
                {
                    _progressPercent = pct;
                    if (_progressCb)
                        _progressCb(_bytesReceived, _bytesTotal, pct);
                }
            }
        }
    }
    else
    {
        if (millis() - _lastDataAt > 20000UL)
        {
            _http.end();
            _httpOpen = false;
            _invokeAfter();
            _fail(LiteOTAError::TIMEOUT);
            return;
        }
    }

    if (_bytesReceived >= _bytesTotal)
    {
        _http.end();
        _httpOpen = false;
        _invokeAfter();
        _setState(LiteOTAState::FINALIZING);
    }
}

// ─────────────────────────────────────────────
// Step: Finalize
// ─────────────────────────────────────────────
void LiteOTA::_stepFinalize()
{
    Serial.println(F("[LiteOTA] Finalizing..."));

    if (_progressCb)
        _progressCb(_bytesReceived, _bytesTotal, 100);

    // ⚡ Set RTC flag NOW — firmware is downloaded and about to be applied
#if defined(LITEOTA_USE_ROLLBACK)
    if (_rollbackEnabled && _fsMounted)
    {
        if (_loadRTC())
        {
            _rtc.state = 1;
            _rtc.bootCount = 0;
            _saveRTC();
            Serial.println(F("[LiteOTA] RTC: update pending"));
        }
    }
#endif

    Update.end(true);

    if (Update.hasError())
    {
        _fail(LiteOTAError::UPDATE_WRITE_FAIL);
        return;
    }

    Serial.println(F("[LiteOTA] OK. Rebooting..."));
    _setState(LiteOTAState::SUCCESS_REBOOT);
    delay(200);
    ESP.restart();
}

// ─────────────────────────────────────────────
// Manifest helpers
// ─────────────────────────────────────────────
LiteOTA::ManifestType LiteOTA::_detectType(WiFiClient *stream)
{
    unsigned long t = millis();
    auto waitData = [&]() -> bool
    {
        t = millis();
        while (!stream->available())
        {
            if (millis() - t > 3000)
                return false;
            yield();
        }
        return true;
    };

    if (!waitData())
        return MT_UNKNOWN;
    int c = stream->peek();
    while (c == ' ' || c == '\r' || c == '\n' || c == '\t')
    {
        stream->read();
        if (!waitData())
            return MT_UNKNOWN;
        c = stream->peek();
    }
    if (c < 0)
        return MT_UNKNOWN;
    return (c == '{') ? MT_JSON : MT_TEXT;
}

bool LiteOTA::_parseJson(WiFiClient *stream)
{
    StaticJsonDocument<512> doc;
    DeserializationError err = deserializeJson(doc, *stream);
    if (err)
    {
        Serial.printf_P(PSTR("[LiteOTA] JSON error: %s\n"), err.c_str());
        return false;
    }
    const char *ver = doc["version"];
    const char *url = doc["url"];
    if (!ver || !url)
        return false;

    strncpy(_remoteVersion, ver, sizeof(_remoteVersion) - 1);
    _remoteVersion[sizeof(_remoteVersion) - 1] = '\0';
    strncpy(_firmwareUrl, url, sizeof(_firmwareUrl) - 1);
    _firmwareUrl[sizeof(_firmwareUrl) - 1] = '\0';
    return true;
}

bool LiteOTA::_parseText(WiFiClient *stream)
{
    char *buffers[2] = {_remoteVersion, _firmwareUrl};
    size_t sizes[2] = {sizeof(_remoteVersion), sizeof(_firmwareUrl)};
    int line = 0;
    size_t idx = 0;
    unsigned long t = millis();

    while (line < 2)
    {
        if (!stream->available())
        {
            if (millis() - t > 3000)
                break;
            yield();
            continue;
        }
        char c = stream->read();
        t = millis();
        if (c == '\r')
            continue;
        if (c == '\n')
        {
            buffers[line][idx] = '\0';
            line++;
            idx = 0;
            if (line >= 2)
                break;
            continue;
        }
        if (idx < sizes[line] - 1)
            buffers[line][idx++] = c;
    }
    if (line < 2 && idx > 0)
    {
        buffers[line][idx] = '\0';
        line++;
    }
    return (line >= 2 && _remoteVersion[0] && _firmwareUrl[0]);
}
// ═════════════════════════════════════════════
//  Web endpoints (LITEOTA_USE_WEB)
// ═════════════════════════════════════════════
// ═════════════════════════════════════════════
//  Web endpoints (LITEOTA_USE_WEB) — Zero String
// ═════════════════════════════════════════════
#if defined(LITEOTA_USE_WEB)

// ─── HTML templates in PROGMEM (zero heap) ───
static const char HTML_HEAD[] PROGMEM =
    "<!DOCTYPE html><html><head><meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>LiteOTA Update</title>"
    "<style>body{font-family:system-ui,sans-serif;max-width:640px;"
    "margin:40px auto;padding:0 20px;background:#f4f4f7;color:#222}"
    "h1{color:#4a6cf7}h2{font-size:1rem;color:#666;margin-top:0}"
    ".card{background:#fff;padding:24px;border-radius:12px;"
    "box-shadow:0 2px 12px rgba(0,0,0,.06);margin-bottom:20px}"
    "input[type=file]{display:block;width:100%;padding:12px;"
    "border:2px dashed #cbd5e0;border-radius:8px;margin:16px 0;"
    "background:#fafafa;box-sizing:border-box}"
    "button{background:#4a6cf7;color:#fff;border:0;padding:12px 24px;"
    "border-radius:8px;font-size:1rem;cursor:pointer}"
    "button:hover{background:#3b5bdb}"
    "pre{background:#f8f9fa;padding:12px;border-radius:6px;"
    "overflow:auto;font-size:.85rem}"
    "a{color:#4a6cf7}"
    ".kitronic{font-size:.8rem;color:#888;text-align:center;margin-top:30px}"
    "</style></head><body><h1>LiteOTA</h1>";

static const char HTML_FORM_START[] PROGMEM =
    "<div class='card'><h2>Firmware Upload</h2>"
    "<form method='POST' action='";

static const char HTML_FORM_MID[] PROGMEM =
    "/update' enctype='multipart/form-data'>"
    "<input type='file' name='firmware' accept='.bin' required>"
    "<button type='submit'>Upload &amp; Flash</button></form></div>";

static const char HTML_OTA_START[] PROGMEM =
    "<div class='card'><h2>Server OTA</h2><p><a href='";

static const char HTML_LINK_OTA[] PROGMEM = "/ota'>Trigger</a> · <a href='";
static const char HTML_LINK_STATUS[] PROGMEM = "/ota/status'>Status</a> · <a href='";
static const char HTML_LINK_ABORT[] PROGMEM = "/ota/abort'>Abort</a>";

#if defined(LITEOTA_USE_ROLLBACK)
static const char HTML_LINK_RB1[] PROGMEM = " · <a href='";
static const char HTML_LINK_RB2[] PROGMEM = "/ota/rollback'>Rollback</a>";
#endif

static const char HTML_OTA_END[] PROGMEM = "</p></div>";

static const char HTML_STATUS_START[] PROGMEM =
    "<div class='card'><h2>Status</h2><pre>";

static const char HTML_FOOTER[] PROGMEM =
    "</pre></div>"
    "<div class='kitronic'>Kitronic · www.kitronic.tech</div>"
    "</body></html>";

// ─── Configuration ───
void LiteOTA::attachWebServer(ESP8266WebServer *server)
{
    if (!server)
    {
        Serial.println(F("[LiteOTA] attachWebServer: null server"));
        return;
    }
    _webServer = server;
    _webAuthOn = false;
    _webUser[0] = '\0';
    _webPass[0] = '\0';
    _webRegisterRoutes();
    Serial.println(F("[LiteOTA] Web endpoints registered (no auth)"));
}

void LiteOTA::attachWebServer(ESP8266WebServer *server,
                              const char *user, const char *pass)
{
    if (!server)
    {
        Serial.println(F("[LiteOTA] attachWebServer: null server"));
        return;
    }
    if (!user || !pass || !*user || !*pass)
    {
        Serial.println(F("[LiteOTA] attachWebServer: empty creds -> no auth"));
        attachWebServer(server);
        return;
    }
    _webServer = server;
    strncpy(_webUser, user, sizeof(_webUser) - 1);
    _webUser[sizeof(_webUser) - 1] = '\0';
    strncpy(_webPass, pass, sizeof(_webPass) - 1);
    _webPass[sizeof(_webPass) - 1] = '\0';
    _webAuthOn = true;
    _webRegisterRoutes();
    Serial.println(F("[LiteOTA] Web endpoints registered (auth ON)"));
}

void LiteOTA::setWebPrefix(const char *prefix)
{
    if (!prefix)
    {
        _webPrefix[0] = '\0';
        return;
    }
    strncpy(_webPrefix, prefix, sizeof(_webPrefix) - 1);
    _webPrefix[sizeof(_webPrefix) - 1] = '\0';
    size_t n = strlen(_webPrefix);
    if (n > 0 && _webPrefix[n - 1] == '/')
        _webPrefix[n - 1] = '\0';
}

void LiteOTA::setWebAutoReboot(bool enable)
{
    _webAutoReboot = enable;
}

bool LiteOTA::_webRequireAuth()
{
    if (!_webAuthOn)
        return true;
    if (_webServer->authenticate(_webUser, _webPass))
        return true;
    _webServer->requestAuthentication();
    return false;
}

// ─── GET /update ─── (chunked, PROGMEM, zero String)
void LiteOTA::_webHandleUpdatePage()
{
    if (!_webRequireAuth())
        return;

    // Start chunked response
    _webServer->setContentLength(CONTENT_LENGTH_UNKNOWN);
    _webServer->send_P(200, PSTR("text/html"), HTML_HEAD);

    // Upload form
    _webServer->sendContent_P(HTML_FORM_START);
    _webServer->sendContent(_webPrefix); // dynamic prefix (RAM)
    _webServer->sendContent_P(HTML_FORM_MID);

    // Server OTA links
    _webServer->sendContent_P(HTML_OTA_START);
    _webServer->sendContent(_webPrefix);
    _webServer->sendContent_P(HTML_LINK_OTA);
    _webServer->sendContent(_webPrefix);
    _webServer->sendContent_P(HTML_LINK_STATUS);
    _webServer->sendContent(_webPrefix);
    _webServer->sendContent_P(HTML_LINK_ABORT);
#if defined(LITEOTA_USE_ROLLBACK)
    _webServer->sendContent_P(HTML_LINK_RB1);
    _webServer->sendContent(_webPrefix);
    _webServer->sendContent_P(HTML_LINK_RB2);
#endif
    _webServer->sendContent_P(HTML_OTA_END);

    // Status block
    _webServer->sendContent_P(HTML_STATUS_START);

    char buf[80];
    snprintf_P(buf, sizeof(buf), PSTR("Version   : %s\n"), _currentVersion);
    _webServer->sendContent(buf);

    snprintf_P(buf, sizeof(buf), PSTR("State     : %s\n"), getStateName());
    _webServer->sendContent(buf);

    snprintf_P(buf, sizeof(buf), PSTR("LastError : %s\n"), getLastErrorName());
    _webServer->sendContent(buf);

    snprintf_P(buf, sizeof(buf), PSTR("TLS       : %s\n"),
               isTLSEnabled() ? "ON" : "OFF");
    _webServer->sendContent(buf);

    snprintf_P(buf, sizeof(buf), PSTR("Rollback  : %s\n"),
               _rollbackEnabled ? "ON" : "OFF");
    _webServer->sendContent(buf);

    snprintf_P(buf, sizeof(buf), PSTR("Free heap : %u bytes\n"),
               (unsigned)ESP.getFreeHeap());
    _webServer->sendContent(buf);

    snprintf_P(buf, sizeof(buf), PSTR("Uptime    : %lu s\n"),
               (unsigned long)(millis() / 1000));
    _webServer->sendContent(buf);

    _webServer->sendContent_P(HTML_FOOTER);
    _webServer->sendContent(""); // end chunked
}

// ─── POST /update ───
void LiteOTA::_webHandleUpdateUpload()
{
    if (!_webRequireAuth())
        return;

    HTTPUpload &up = _webServer->upload();

    switch (up.status)
    {

    case UPLOAD_FILE_START:
    {
        Serial.printf_P(PSTR("[LiteOTA] Web upload: %s (%u bytes)\n"),
                        up.filename.c_str(), up.totalSize);
        _uploadActive = true;
        _uploadAuthorized = true;
        _uploadBytes = 0;

        uint32_t maxSketch = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
        if (!Update.begin(maxSketch, U_FLASH))
        {
            _uploadAuthorized = false;
            Serial.print(F("[LiteOTA] Update.begin failed: "));
            Update.printError(Serial);
            Serial.println();
            return;
        }

#if defined(LITEOTA_USE_ROLLBACK)
        if (_rollbackEnabled && _fsMounted)
        {
            if (_loadRTC())
            {
                _rtc.state = 1;
                _rtc.bootCount = 0;
                _saveRTC();
            }
        }
#endif
        break;
    }

    case UPLOAD_FILE_WRITE:
    {
        if (!_uploadAuthorized)
            return;
        if (Update.write(up.buf, up.currentSize) != up.currentSize)
        {
            Serial.print(F("[LiteOTA] Update.write failed: "));
            Update.printError(Serial);
            Serial.println();
            _uploadAuthorized = false;
            return;
        }
        _uploadBytes += up.currentSize;

        static uint32_t lastLog = 0;
        if (millis() - lastLog > 1000)
        {
            lastLog = millis();
            Serial.printf_P(PSTR("[LiteOTA] Web upload: %u bytes\n"),
                            (unsigned)_uploadBytes);
        }
        break;
    }

    case UPLOAD_FILE_END:
    {
        _uploadActive = false;
        if (!_uploadAuthorized)
        {
            Update.end(false);
            Serial.println(F("[LiteOTA] Upload aborted"));
            return;
        }
        if (!Update.end(true))
        {
            Serial.print(F("[LiteOTA] Update.end failed: "));
            Update.printError(Serial);
            Serial.println();

            _webServer->send_P(500, PSTR("text/plain"),
                               PSTR("Update failed. Check serial."));
            return;
        }

        Serial.printf_P(PSTR("[LiteOTA] Web upload OK: %u bytes\n"),
                        (unsigned)_uploadBytes);

        _webServer->send_P(200, PSTR("text/html"),
                           PSTR("<!DOCTYPE html><html><head><meta charset='utf-8'>"
                                "<title>Rebooting</title></head>"
                                "<body style='font-family:sans-serif;max-width:640px;"
                                "margin:40px auto;padding:0 20px'>"
                                "<h1 style='color:#4a6cf7'>&#10003; Update Successful</h1>"
                                "<p>Device is rebooting with the new firmware...</p>"
                                "<p>Refresh in ~10 seconds.</p></body></html>"));

        delay(300);
        ESP.restart();
        break;
    }

    case UPLOAD_FILE_ABORTED:
    {
        _uploadActive = false;
        Update.end(false);
        Serial.println(F("[LiteOTA] Upload aborted by client"));
        break;
    }
    }
}

// ─── GET /ota ───
void LiteOTA::_webHandleOtaTrigger()
{
    if (!_webRequireAuth())
        return;
    requestUpdate();
    _webServer->send_P(200, PSTR("application/json"),
                       PSTR("{\"ok\":true,\"msg\":\"update requested\"}"));
}

// ─── GET /ota/status ───
void LiteOTA::_webHandleOtaStatus()
{
    if (!_webRequireAuth())
        return;

    char buf[192];
    snprintf_P(buf, sizeof(buf),
               PSTR("{\"state\":\"%s\",\"error\":\"%s\",\"version\":\"%s\","
                    "\"remote\":\"%s\",\"progress\":%u,\"bytesReceived\":%u,"
                    "\"bytesTotal\":%u,\"updating\":%s,\"heap\":%u}"),
               getStateName(), getLastErrorName(), _currentVersion,
               _remoteVersion, (unsigned)_progressPercent, (unsigned)_bytesReceived,
               (unsigned)_bytesTotal, isUpdating() ? "true" : "false",
               (unsigned)ESP.getFreeHeap());
    _webServer->send(200, "application/json", buf);
}

// ─── GET /ota/abort ───
void LiteOTA::_webHandleOtaAbort()
{
    if (!_webRequireAuth())
        return;
    abort();
    _webServer->send_P(200, PSTR("application/json"),
                       PSTR("{\"ok\":true,\"msg\":\"aborted\"}"));
}

// ─── GET /ota/rollback ───
#if defined(LITEOTA_USE_ROLLBACK)
void LiteOTA::_webHandleOtaRollback()
{
    if (!_webRequireAuth())
        return;
    if (!isRollbackAvailable())
    {
        _webServer->send_P(404, PSTR("application/json"),
                           PSTR("{\"ok\":false,\"msg\":\"no backup available\"}"));
        return;
    }
    _webServer->send_P(200, PSTR("application/json"),
                       PSTR("{\"ok\":true,\"msg\":\"rollback starting\"}"));
    delay(200);
    rollbackToPrevious();
}
#endif

// ─── Route registration ───
void LiteOTA::_webRegisterRoutes()
{
    if (!_webServer)
        return;

    // Build route paths in static local buffers (ESP8266WebServer copies them)
    static char pathUpdate[32];
    static char pathOta[32];
    static char pathStatus[32];
    static char pathAbort[32];
#if defined(LITEOTA_USE_ROLLBACK)
    static char pathRollback[32];
#endif

    snprintf_P(pathUpdate, sizeof(pathUpdate), PSTR("%s/update"), _webPrefix);
    snprintf_P(pathOta, sizeof(pathOta), PSTR("%s/ota"), _webPrefix);
    snprintf_P(pathStatus, sizeof(pathStatus), PSTR("%s/ota/status"), _webPrefix);
    snprintf_P(pathAbort, sizeof(pathAbort), PSTR("%s/ota/abort"), _webPrefix);
#if defined(LITEOTA_USE_ROLLBACK)
    snprintf_P(pathRollback, sizeof(pathRollback),
               PSTR("%s/ota/rollback"), _webPrefix);
#endif

    // GET /update
    _webServer->on(pathUpdate, HTTP_GET, [this]()
                   { _webHandleUpdatePage(); });

    // POST /update
    _webServer->on(pathUpdate, HTTP_POST, [this]()
                   {
                       if (!_webRequireAuth())
                           return;
                       if (!_uploadAuthorized)
                       {
                           _webServer->send_P(400, PSTR("text/plain"), PSTR("Upload failed"));
                           return;
                       }
                       // Response sent inside UPLOAD_FILE_END
                   },
                   [this]()
                   { _webHandleUpdateUpload(); });

    // GET /ota
    _webServer->on(pathOta, HTTP_GET, [this]()
                   { _webHandleOtaTrigger(); });

    // GET /ota/status
    _webServer->on(pathStatus, HTTP_GET, [this]()
                   { _webHandleOtaStatus(); });

    // GET /ota/abort
    _webServer->on(pathAbort, HTTP_GET, [this]()
                   { _webHandleOtaAbort(); });

#if defined(LITEOTA_USE_ROLLBACK)
    // GET /ota/rollback
    _webServer->on(pathRollback, HTTP_GET, [this]()
                   { _webHandleOtaRollback(); });
#endif

    Serial.printf_P(PSTR("[LiteOTA] Web endpoints registered at \"%s\"\n"),
                    _webPrefix);
}

#endif // LITEOTA_USE_WEB