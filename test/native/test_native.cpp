/*
 * Native (host) tests for LiteOTA — g++ compatible
 *
 * Kitronic — https://github.com/kitronic/esp-lib-lite-ota
 *
 * Build:  make -C test/native
 * Run:    ./test/native/run
 *
 * No Unity, no PlatformIO, no ESP8266 headers.
 * Pure C++17 — tests the pure-logic parts of LiteOTA.
 */

#include <cstdio>
#include <cstring>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

// ═══════════════════════════════════════════════════════════════
//  Minimal test framework
// ═══════════════════════════════════════════════════════════════

static int  g_testsRun    = 0;
static int  g_testsPassed = 0;
static int  g_testsFailed = 0;
static const char* g_currentTest = "";

#define TEST(name) \
    static void name(); \
    struct name##_reg { name##_reg() { test_register(#name, name); } } name##_reg_inst; \
    static void name()

#define ASSERT_TRUE(cond) \
    do { \
        if (!(cond)) { \
            std::printf("    [FAIL] %s:%d  ASSERT_TRUE(%s)\n", \
                        __FILE__, __LINE__, #cond); \
            g_testsFailed++; \
            return; \
        } \
    } while (0)

#define ASSERT_FALSE(cond) ASSERT_TRUE(!(cond))

#define ASSERT_EQ(a, b) \
    do { \
        auto _a = (a); auto _b = (b); \
        if (!(_a == _b)) { \
            std::printf("    [FAIL] %s:%d  ASSERT_EQ(%s, %s)\n", \
                        __FILE__, __LINE__, #a, #b); \
            g_testsFailed++; \
            return; \
        } \
    } while (0)

#define ASSERT_STREQ(a, b) \
    do { \
        const char* _a = (a); const char* _b = (b); \
        if (!_a || !_b || std::strcmp(_a, _b) != 0) { \
            std::printf("    [FAIL] %s:%d  ASSERT_STREQ(\"%s\", \"%s\")\n", \
                        __FILE__, __LINE__, _a ? _a : "(null)", _b ? _b : "(null)"); \
            g_testsFailed++; \
            return; \
        } \
    } while (0)

typedef void (*TestFn)();
static std::vector<std::pair<const char*, TestFn>> g_registry;

static void test_register(const char* name, TestFn fn) {
    g_registry.push_back({name, fn});
}

static void run_all() {
    std::printf("\n");
    std::printf("╔══════════════════════════════════════════════╗\n");
    std::printf("║  LiteOTA — Native Tests (Kitronic)          ║\n");
    std::printf("╚══════════════════════════════════════════════╝\n\n");

    for (auto& [name, fn] : g_registry) {
        g_currentTest = name;
        g_testsRun++;
        int failBefore = g_testsFailed;
        std::printf("  ▸ %s\n", name);
        fn();
        if (g_testsFailed == failBefore) {
            g_testsPassed++;
        }
    }

    std::printf("\n──────────────────────────────────────────────\n");
    std::printf("  Total:  %d\n", g_testsRun);
    std::printf("  Passed: %d\n", g_testsPassed);
    std::printf("  Failed: %d\n", g_testsFailed);
    std::printf("──────────────────────────────────────────────\n\n");

    if (g_testsFailed > 0) {
        std::printf("❌ FAILED\n\n");
        std::exit(1);
    }
    std::printf("✅ ALL PASSED\n\n");
}

// ═══════════════════════════════════════════════════════════════
//  Pure-logic reimplementations (mirror of LiteOTA internals)
// ═══════════════════════════════════════════════════════════════

enum ManifestType { MT_UNKNOWN = 0, MT_JSON, MT_TEXT };

// ─── Manifest type detection (mirrors _detectType) ───
static ManifestType detectTypeFromString(const char* s) {
    if (!s) return MT_UNKNOWN;
    while (*s == ' ' || *s == '\r' || *s == '\n' || *s == '\t') s++;
    if (*s == '\0') return MT_UNKNOWN;
    return (*s == '{') ? MT_JSON : MT_TEXT;
}

// ─── URL protocol detection (mirrors _isHttps) ───
static bool isHttps(const char* url) {
    return url && (std::strncmp(url, "https://", 8) == 0);
}

// ─── Version comparison (mirrors strcmp logic) ───
static bool isSameVersion(const char* a, const char* b) {
    if (!a || !b) return false;
    return std::strcmp(a, b) == 0;
}

// ─── JSON-like manifest parse (simplified) ───
struct ParsedManifest {
    bool ok = false;
    std::string version;
    std::string url;
};

static ParsedManifest parseJsonLike(const std::string& body) {
    ParsedManifest m;
    auto extract = [&](const std::string& key) -> std::string {
        std::string needle = "\"" + key + "\"";
        auto pos = body.find(needle);
        if (pos == std::string::npos) return "";
        pos = body.find(':', pos);
        if (pos == std::string::npos) return "";
        pos = body.find('"', pos);
        if (pos == std::string::npos) return "";
        auto end = body.find('"', pos + 1);
        if (end == std::string::npos) return "";
        return body.substr(pos + 1, end - pos - 1);
    };
    m.version = extract("version");
    m.url     = extract("url");
    m.ok = !m.version.empty() && !m.url.empty();
    return m;
}

// ─── Plain-text manifest parse (line1=version, line2=url) ───
static ParsedManifest parseTextLike(const std::string& body) {
    ParsedManifest m;
    size_t line1End = body.find('\n');
    if (line1End == std::string::npos) return m;
    size_t line2End = body.find('\n', line1End + 1);

    m.version = body.substr(0, line1End);
    if (!m.version.empty() && m.version.back() == '\r') m.version.pop_back();

    if (line2End == std::string::npos)
        m.url = body.substr(line1End + 1);
    else
        m.url = body.substr(line1End + 1, line2End - line1End - 1);

    if (!m.url.empty() && m.url.back() == '\r') m.url.pop_back();

    m.ok = !m.version.empty() && !m.url.empty();
    return m;
}

// ─── Chunk size clamping (mirrors setChunkSize) ───
#define LITEOTA_MAX_CHUNK 512
static size_t clampChunkSize(size_t s) {
    if (s == 0) s = 1;
    if (s > LITEOTA_MAX_CHUNK) s = LITEOTA_MAX_CHUNK;
    return s;
}

// ═══════════════════════════════════════════════════════════════
//  Tests — Manifest detection
// ═══════════════════════════════════════════════════════════════

TEST(manifest_detect_json_plain) {
    ASSERT_EQ(MT_JSON, detectTypeFromString("{\"version\":\"1.0\"}"));
}

TEST(manifest_detect_json_with_leading_ws) {
    ASSERT_EQ(MT_JSON, detectTypeFromString("   \n\t\r {\"v\":1}"));
}

TEST(manifest_detect_text_plain) {
    ASSERT_EQ(MT_TEXT, detectTypeFromString("1.0\nhttp://x.bin"));
}

TEST(manifest_detect_text_with_leading_ws) {
    ASSERT_EQ(MT_TEXT, detectTypeFromString("\r\n  1.0\nhttp://x.bin"));
}

TEST(manifest_detect_empty_is_unknown) {
    ASSERT_EQ(MT_UNKNOWN, detectTypeFromString(""));
}

TEST(manifest_detect_ws_only_is_unknown) {
    ASSERT_EQ(MT_UNKNOWN, detectTypeFromString("   \r\n\t  "));
}

TEST(manifest_detect_null_is_unknown) {
    ASSERT_EQ(MT_UNKNOWN, detectTypeFromString(nullptr));
}

// ═══════════════════════════════════════════════════════════════
//  Tests — URL protocol
// ═══════════════════════════════════════════════════════════════

TEST(url_is_https_true) {
    ASSERT_TRUE(isHttps("https://kitronic.tech/ota.json"));
}

TEST(url_is_https_false_http) {
    ASSERT_FALSE(isHttps("http://kitronic.tech/ota.json"));
}

TEST(url_is_https_false_empty) {
    ASSERT_FALSE(isHttps(""));
}

TEST(url_is_https_false_null) {
    ASSERT_FALSE(isHttps(nullptr));
}

// ═══════════════════════════════════════════════════════════════
//  Tests — Version comparison
// ═══════════════════════════════════════════════════════════════

TEST(version_equal) {
    ASSERT_TRUE(isSameVersion("1.0", "1.0"));
}

TEST(version_different_patch) {
    ASSERT_FALSE(isSameVersion("1.0", "1.1"));
}

TEST(version_case_sensitive) {
    ASSERT_FALSE(isSameVersion("v1.0", "V1.0"));
}

TEST(version_null_returns_false) {
    ASSERT_FALSE(isSameVersion(nullptr, "1.0"));
    ASSERT_FALSE(isSameVersion("1.0", nullptr));
}

// ═══════════════════════════════════════════════════════════════
//  Tests — JSON manifest parsing
// ═══════════════════════════════════════════════════════════════

TEST(json_parse_simple) {
    auto m = parseJsonLike(
        "{\"version\":\"1.1\",\"url\":\"http://x.bin\"}");
    ASSERT_TRUE(m.ok);
    ASSERT_STREQ("1.1", m.version.c_str());
    ASSERT_STREQ("http://x.bin", m.url.c_str());
}

TEST(json_parse_with_whitespace) {
    auto m = parseJsonLike(
        "{ \"version\" : \"2.0\" , \"url\" : \"https://y.bin\" }");
    ASSERT_TRUE(m.ok);
    ASSERT_STREQ("2.0", m.version.c_str());
    ASSERT_STREQ("https://y.bin", m.url.c_str());
}

TEST(json_parse_missing_version_fails) {
    auto m = parseJsonLike("{\"url\":\"http://x.bin\"}");
    ASSERT_FALSE(m.ok);
}

TEST(json_parse_missing_url_fails) {
    auto m = parseJsonLike("{\"version\":\"1.0\"}");
    ASSERT_FALSE(m.ok);
}

TEST(json_parse_empty_fails) {
    auto m = parseJsonLike("");
    ASSERT_FALSE(m.ok);
}

// ═══════════════════════════════════════════════════════════════
//  Tests — Plain-text manifest parsing
// ═══════════════════════════════════════════════════════════════

TEST(text_parse_simple) {
    auto m = parseTextLike("1.1\nhttp://x.bin\n");
    ASSERT_TRUE(m.ok);
    ASSERT_STREQ("1.1", m.version.c_str());
    ASSERT_STREQ("http://x.bin", m.url.c_str());
}

TEST(text_parse_crlf) {
    auto m = parseTextLike("1.1\r\nhttp://x.bin\r\n");
    ASSERT_TRUE(m.ok);
    ASSERT_STREQ("1.1", m.version.c_str());
    ASSERT_STREQ("http://x.bin", m.url.c_str());
}

TEST(text_parse_no_trailing_newline) {
    auto m = parseTextLike("1.1\nhttp://x.bin");
    ASSERT_TRUE(m.ok);
    ASSERT_STREQ("1.1", m.version.c_str());
    ASSERT_STREQ("http://x.bin", m.url.c_str());
}

TEST(text_parse_missing_url_fails) {
    auto m = parseTextLike("1.1\n");
    ASSERT_FALSE(m.ok);
}

TEST(text_parse_empty_fails) {
    auto m = parseTextLike("");
    ASSERT_FALSE(m.ok);
}

// ═══════════════════════════════════════════════════════════════
//  Tests — Chunk size clamping
// ═══════════════════════════════════════════════════════════════

TEST(chunk_zero_becomes_one) {
    ASSERT_EQ(size_t(1), clampChunkSize(0));
}

TEST(chunk_oversized_clamped) {
    ASSERT_EQ(size_t(LITEOTA_MAX_CHUNK), clampChunkSize(100000));
}

TEST(chunk_within_range_unchanged) {
    ASSERT_EQ(size_t(256), clampChunkSize(256));
}

TEST(chunk_at_max_unchanged) {
    ASSERT_EQ(size_t(LITEOTA_MAX_CHUNK), clampChunkSize(LITEOTA_MAX_CHUNK));
}

// ═══════════════════════════════════════════════════════════════
//  Main
// ═══════════════════════════════════════════════════════════════

int main() {
    run_all();
    return 0;
}