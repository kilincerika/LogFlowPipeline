#include <iostream>
#include <string>
#include <vector>
#include <cassert>
#include <fstream>
#include "../ParserStage.hpp"
#include "../CollectingEmitter.hpp"
#include "../LogRecord.hpp"
#include "../ConsoleSink.hpp"
#include "../Pipeline.hpp"
#include "../PipelineBuilder.hpp"
#include "../PredicateFilterStage.hpp"
#include "../ProjectionStage.hpp"
#include "../FileSink.hpp"
#include "../FileLineSource.hpp"
#include "../StageFactory.hpp"
#include "../StageRegistry.hpp"
#include "../PipelineLoader.hpp"
#include "../PipelineExceptions.hpp"
#include "../FailedRecord.hpp"
#include "../ErrorPolicy.hpp"
#include "../DeadLetterSink.hpp"
#include "../ValidationStage.hpp"
#include "../RunSummary.hpp"

// Basit ve Şık Birim Test Makroları (Zero-dependency xUnit / JUnit stili)
static int g_passCount = 0;
static int g_failCount = 0;

#define TEST_CASE(name) void name()
#define RUN_TEST(name) do { \
    std::cout << "[TEST] " << #name << " kosuluyor... "; \
    try { \
        name(); \
        std::cout << "BASARILI (PASSED)\n"; \
        g_passCount++; \
    } catch (const std::exception& e) { \
        std::cout << "BASARISIZ (FAILED): " << e.what() << "\n"; \
        g_failCount++; \
    } catch (...) { \
        std::cout << "BASARISIZ (FAILED): Bilinmeyen hata\n"; \
        g_failCount++; \
    } \
} while(0)

#define ASSERT_TRUE(cond) do { \
    if (!(cond)) throw std::runtime_error("Dogrulama basarisiz: " #cond); \
} while(0)

#define ASSERT_EQ(actual, expected) do { \
    if (!((actual) == (expected))) { \
        throw std::runtime_error("Esitlik basarisiz: Beklenen '" + std::to_string(expected) + "' fakat alinan '" + std::to_string(actual) + "'"); \
    } \
} while(0)

#define ASSERT_STR_EQ(actual, expected) do { \
    if ((actual) != (expected)) { \
        throw std::runtime_error("Dizgi esitligi basarisiz: Beklenen '" + std::string(expected) + "' fakat alinan '" + std::string(actual) + "'"); \
    } \
} while(0)

// Yardımcı dosya okuma fonksiyonu
static std::vector<std::string> readFileLines(const std::string& path) {
    std::vector<std::string> lines;
    std::ifstream file(path.c_str());
    if (!file.is_open()) {
        file.open(("../" + path).c_str());
    }
    if (!file.is_open()) {
        throw std::runtime_error("Dosya okunamadi: " + path);
    }
    std::string line;
    while (std::getline(file, line)) {
        lines.push_back(line);
    }
    return lines;
}

// ============================================================================
// ARTIM 2: ZORUNLU VE MEVCUT TESTLER (1-15)
// ============================================================================

TEST_CASE(test_1_gecerli_satir) {
    ParserStage parser;
    CollectingEmitter<LogRecord> emitter;

    std::string line = "192.168.1.10 - - [29/Sep/2026:10:00:00 +0300] \"GET /index.html HTTP/1.1\" 200 1024";
    parser.process(line, emitter);

    ASSERT_EQ(emitter.size(), 1);
    ASSERT_EQ(parser.getErrorCount(), 0);

    const LogRecord& rec = emitter[0];
    ASSERT_STR_EQ(rec.getClientIp(), "192.168.1.10");
    ASSERT_STR_EQ(rec.getMethod(), "GET");
    ASSERT_STR_EQ(rec.getPath(), "/index.html");
    ASSERT_EQ(rec.getStatus(), 200);
    ASSERT_EQ(rec.getBytes(), 1024);
    ASSERT_STR_EQ(rec.getRaw(), line);
    ASSERT_STR_EQ(rec.getTimestampIso(), "2026-09-29T07:00:00Z");
}

TEST_CASE(test_2_eksik_alan) {
    ParserStage parser;
    CollectingEmitter<LogRecord> emitter;

    std::string line = "192.168.1.10 - - [29/Sep/2026:10:00:00 +0300] \"GET /index.html HTTP/1.1\"";
    parser.process(line, emitter);

    ASSERT_EQ(emitter.size(), 0);
    ASSERT_EQ(parser.getErrorCount(), 1);
}

TEST_CASE(test_3_hatali_zaman_damgasi) {
    ParserStage parser;
    CollectingEmitter<LogRecord> emitter;

    std::string line = "192.168.1.10 - - [29/InvalidMon/2026:99:99:99 +0300] \"GET /index.html HTTP/1.1\" 200 1024";
    parser.process(line, emitter);

    ASSERT_EQ(emitter.size(), 0);
    ASSERT_EQ(parser.getErrorCount(), 1);

    std::string line2 = "192.168.1.10 - - [not-a-timestamp] \"GET /index.html HTTP/1.1\" 200 1024";
    parser.process(line2, emitter);

    ASSERT_EQ(emitter.size(), 0);
    ASSERT_EQ(parser.getErrorCount(), 2);
}

TEST_CASE(test_4_hatali_durum_kodu) {
    ParserStage parser;
    CollectingEmitter<LogRecord> emitter;

    std::string line1 = "192.168.1.10 - - [29/Sep/2026:10:00:00 +0300] \"GET /index.html HTTP/1.1\" BAD_STATUS 1024";
    parser.process(line1, emitter);

    std::string line2 = "192.168.1.10 - - [29/Sep/2026:10:00:00 +0300] \"GET /index.html HTTP/1.1\" 999 1024";
    parser.process(line2, emitter);

    ASSERT_EQ(emitter.size(), 0);
    ASSERT_EQ(parser.getErrorCount(), 2);
}

TEST_CASE(test_5_bos_satir) {
    ParserStage parser;
    CollectingEmitter<LogRecord> emitter;

    parser.process("", emitter);
    parser.process("   \t   \r\n", emitter);

    ASSERT_EQ(emitter.size(), 0);
    ASSERT_EQ(parser.getErrorCount(), 2);
}

TEST_CASE(test_6_fazladan_bosluk) {
    ParserStage parser;
    CollectingEmitter<LogRecord> emitter;

    std::string line = "   192.168.1.10    -    -    [29/Sep/2026:10:00:00 +0300]    \"GET /index.html HTTP/1.1\"    200    1024   ";
    parser.process(line, emitter);

    ASSERT_EQ(emitter.size(), 1);
    ASSERT_EQ(parser.getErrorCount(), 0);
    ASSERT_STR_EQ(emitter[0].getClientIp(), "192.168.1.10");
    ASSERT_EQ(emitter[0].getStatus(), 200);
    ASSERT_EQ(emitter[0].getBytes(), 1024);
}

TEST_CASE(test_7_bosluk_iceren_tirnakli_user_agent) {
    ParserStage parser;
    CollectingEmitter<LogRecord> emitter;

    std::string ua = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/117.0.0.0 Safari/537.36";
    std::string line = "192.168.1.10 - - [29/Sep/2026:10:00:00 +0300] \"GET /index.html HTTP/1.1\" 200 1024 \"" + ua + "\"";
    parser.process(line, emitter);

    ASSERT_EQ(emitter.size(), 1);
    ASSERT_EQ(parser.getErrorCount(), 0);
    ASSERT_STR_EQ(emitter[0].getUserAgent(), ua);
}

TEST_CASE(test_8_sorgu_dizesi_iceren_satir) {
    ParserStage parser;
    CollectingEmitter<LogRecord> emitter;

    std::string line = "192.168.1.10 - - [29/Sep/2026:10:00:00 +0300] \"GET /search?query=software+architecture&page=2&sort=desc HTTP/1.1\" 200 1024";
    parser.process(line, emitter);

    ASSERT_EQ(emitter.size(), 1);
    ASSERT_EQ(parser.getErrorCount(), 0);
    ASSERT_STR_EQ(emitter[0].getMethod(), "GET");
    ASSERT_STR_EQ(emitter[0].getPath(), "/search?query=software+architecture&page=2&sort=desc");
}

TEST_CASE(test_9_tire_boyut_alani) {
    ParserStage parser;
    CollectingEmitter<LogRecord> emitter;

    std::string line = "192.168.1.10 - - [29/Sep/2026:10:00:00 +0300] \"GET /no-content HTTP/1.1\" 204 -";
    parser.process(line, emitter);

    ASSERT_EQ(emitter.size(), 1);
    ASSERT_EQ(parser.getErrorCount(), 0);
    ASSERT_EQ(emitter[0].getStatus(), 204);
    ASSERT_EQ(emitter[0].getBytes(), 0);
}

TEST_CASE(test_10_logrecord_immutability_and_builder) {
    auto now = std::chrono::system_clock::now();
    LogRecord record = LogRecord::Builder()
        .setTimestamp(now)
        .setClientIp("10.0.0.1")
        .setMethod("POST")
        .setPath("/api/v1/data")
        .setStatus(201)
        .setBytes(512)
        .setUserAgent("TestAgent/1.0")
        .addAttribute("env", "production")
        .setRaw("raw log content")
        .build();

    ASSERT_STR_EQ(record.getClientIp(), "10.0.0.1");
    ASSERT_STR_EQ(record.getMethod(), "POST");
    ASSERT_STR_EQ(record.getPath(), "/api/v1/data");
    ASSERT_EQ(record.getStatus(), 201);
    ASSERT_EQ(record.getBytes(), 512);
    ASSERT_STR_EQ(record.getUserAgent(), "TestAgent/1.0");
    ASSERT_STR_EQ(record.getRaw(), "raw log content");
    ASSERT_STR_EQ(record.getAttributes().at("env"), "production");
}

TEST_CASE(test_11_attributes_ocp_extension) {
    auto now = std::chrono::system_clock::now();
    LogRecord baseRecord = LogRecord::Builder()
        .setTimestamp(now)
        .setClientIp("10.0.0.1")
        .setMethod("GET")
        .setPath("/")
        .setStatus(200)
        .setBytes(100)
        .build();

    ASSERT_EQ(baseRecord.getAttributes().size(), 0);

    LogRecord enrichedRecord = baseRecord.withAttribute("geo_country", "TR")
                                         .withAttribute("authenticated", "true");

    ASSERT_EQ(baseRecord.getAttributes().size(), 0);
    ASSERT_EQ(enrichedRecord.getAttributes().size(), 2);
    ASSERT_STR_EQ(enrichedRecord.getAttributes().at("geo_country"), "TR");
    ASSERT_STR_EQ(enrichedRecord.getAttributes().at("authenticated"), "true");
}

TEST_CASE(test_12_combined_log_format_referer_ve_user_agent) {
    ParserStage parser;
    CollectingEmitter<LogRecord> emitter;

    std::string line = "192.168.1.10 - - [29/Sep/2026:10:00:00 +0300] \"GET /index.html HTTP/1.1\" 200 1024 \"http://example.com/home\" \"Mozilla/5.0\"";
    parser.process(line, emitter);

    ASSERT_EQ(emitter.size(), 1);
    ASSERT_EQ(parser.getErrorCount(), 0);
    ASSERT_STR_EQ(emitter[0].getUserAgent(), "Mozilla/5.0");
    ASSERT_STR_EQ(emitter[0].getAttributes().at("referer"), "http://example.com/home");
}

TEST_CASE(test_13_protokolsüz_istek_ve_hata_sifirlama) {
    ParserStage parser;
    CollectingEmitter<LogRecord> emitter;

    parser.open();
    std::string line = "192.168.1.10 - - [29/Sep/2026:10:00:00 +0300] \"GET /simple-path\" 200 500";
    parser.process(line, emitter);

    ASSERT_EQ(emitter.size(), 1);
    ASSERT_STR_EQ(emitter[0].getPath(), "/simple-path");

    parser.process("192.168.1.10 - - [29/Sep/2026:10:00:00 +0300] \"BADREQUEST\" 200 500", emitter);
    ASSERT_EQ(parser.getErrorCount(), 1);

    parser.resetErrorCount();
    ASSERT_EQ(parser.getErrorCount(), 0);
    parser.close();
}

TEST_CASE(test_14_hatali_bayt_degeri) {
    ParserStage parser;
    CollectingEmitter<LogRecord> emitter;

    parser.process("192.168.1.10 - - [29/Sep/2026:10:00:00 +0300] \"GET /test HTTP/1.1\" 200 NOT_BYTES", emitter);
    parser.process("192.168.1.10 - - [29/Sep/2026:10:00:00 +0300] \"GET /test HTTP/1.1\" 200 -50", emitter);

    ASSERT_EQ(emitter.size(), 0);
    ASSERT_EQ(parser.getErrorCount(), 2);
}

TEST_CASE(test_15_collecting_emitter_metotlari) {
    CollectingEmitter<LogRecord> emitter;
    ASSERT_TRUE(emitter.empty());

    LogRecord rec = LogRecord::Builder()
        .setClientIp("127.0.0.1")
        .setMethod("GET")
        .setPath("/")
        .build();

    emitter.emit(rec);
    ASSERT_EQ(emitter.size(), 1);
    ASSERT_TRUE(!emitter.empty());

    emitter.clear();
    ASSERT_EQ(emitter.size(), 0);
    ASSERT_TRUE(emitter.empty());
}

// ============================================================================
// ARTIM 3: YENİ AŞAMA VE MİMARİ TESTLERİ (16-31)
// ============================================================================

/**
 * @brief 16. Test: PredicateFilterStage Durum Kodu Filtresi (status >= 400)
 */
TEST_CASE(test_16_predicate_filter_status) {
    PredicateFilterStage filter(PredicateFilterStage::statusAtLeast(400));
    CollectingEmitter<LogRecord> emitter;

    LogRecord r200 = LogRecord::Builder().setStatus(200).setPath("/ok").build();
    LogRecord r401 = LogRecord::Builder().setStatus(401).setPath("/unauth").build();
    LogRecord r404 = LogRecord::Builder().setStatus(404).setPath("/notfound").build();
    LogRecord r500 = LogRecord::Builder().setStatus(500).setPath("/error").build();

    filter.process(r200, emitter);
    filter.process(r401, emitter);
    filter.process(r404, emitter);
    filter.process(r500, emitter);

    ASSERT_EQ(emitter.size(), 3);
    ASSERT_EQ(emitter[0].status(), 401);
    ASSERT_EQ(emitter[1].status(), 404);
    ASSERT_EQ(emitter[2].status(), 500);
}

/**
 * @brief 17. Test: PredicateFilterStage Yol Ön-eki Filtresi (path startsWith /api)
 */
TEST_CASE(test_17_predicate_filter_path_prefix) {
    PredicateFilterStage filter(PredicateFilterStage::pathStartsWith("/api"));
    CollectingEmitter<LogRecord> emitter;

    LogRecord r1 = LogRecord::Builder().setPath("/api/v1/users").build();
    LogRecord r2 = LogRecord::Builder().setPath("/api/login").build();
    LogRecord r3 = LogRecord::Builder().setPath("/index.html").build();
    LogRecord r4 = LogRecord::Builder().setPath("/static/style.css").build();

    filter.process(r1, emitter);
    filter.process(r2, emitter);
    filter.process(r3, emitter);
    filter.process(r4, emitter);

    ASSERT_EQ(emitter.size(), 2);
    ASSERT_STR_EQ(emitter[0].path(), "/api/v1/users");
    ASSERT_STR_EQ(emitter[1].path(), "/api/login");
}

/**
 * @brief 18. Test: PredicateFilterStage Zaman Damgası Aralığı Filtresi
 */
TEST_CASE(test_18_predicate_filter_timestamp_range) {
    using namespace std::chrono;
    auto t1 = system_clock::from_time_t(1000);
    auto t2 = system_clock::from_time_t(2000);
    auto t3 = system_clock::from_time_t(3000);

    PredicateFilterStage filter(PredicateFilterStage::timestampInRange(
        system_clock::from_time_t(1500),
        system_clock::from_time_t(2500)
    ));
    CollectingEmitter<LogRecord> emitter;

    filter.process(LogRecord::Builder().setTimestamp(t1).setPath("/t1").build(), emitter);
    filter.process(LogRecord::Builder().setTimestamp(t2).setPath("/t2").build(), emitter);
    filter.process(LogRecord::Builder().setTimestamp(t3).setPath("/t3").build(), emitter);

    ASSERT_EQ(emitter.size(), 1);
    ASSERT_STR_EQ(emitter[0].path(), "/t2");
}

/**
 * @brief 19. Test: PredicateFilterStage Özel Lambda Koşulu (r -> r.status() >= 400 && r.bytes() > 1000)
 */
TEST_CASE(test_19_predicate_filter_custom_lambda) {
    PredicateFilterStage filter([](const LogRecord& r) {
        return r.status() >= 400 && r.bytes() > 1000;
    });
    CollectingEmitter<LogRecord> emitter;

    LogRecord r1 = LogRecord::Builder().setStatus(404).setBytes(500).build();   // status uyuyor, bytes uymuyor
    LogRecord r2 = LogRecord::Builder().setStatus(200).setBytes(2000).build();  // bytes uyuyor, status uymuyor
    LogRecord r3 = LogRecord::Builder().setStatus(500).setBytes(4096).build();  // her ikisi de uyuyor

    filter.process(r1, emitter);
    filter.process(r2, emitter);
    filter.process(r3, emitter);

    ASSERT_EQ(emitter.size(), 1);
    ASSERT_EQ(emitter[0].status(), 500);
    ASSERT_EQ(emitter[0].bytes(), 4096);
}

/**
 * @brief 20. Test: PredicateFilterStage Kayıt Bütünlüğünü Korur (Değiştirmeden iletir)
 */
TEST_CASE(test_20_predicate_filter_kayit_butunlugu) {
    PredicateFilterStage filter([](const LogRecord&) { return true; });
    CollectingEmitter<LogRecord> emitter;

    LogRecord orig = LogRecord::Builder()
        .setClientIp("192.168.1.50")
        .setMethod("DELETE")
        .setPath("/api/item/42")
        .setStatus(204)
        .setBytes(0)
        .setUserAgent("CustomClient/2.0")
        .build();

    filter.process(orig, emitter);

    ASSERT_EQ(emitter.size(), 1);
    ASSERT_STR_EQ(emitter[0].clientIp(), "192.168.1.50");
    ASSERT_STR_EQ(emitter[0].method(), "DELETE");
    ASSERT_STR_EQ(emitter[0].path(), "/api/item/42");
    ASSERT_EQ(emitter[0].status(), 204);
}

/**
 * @brief 21. Test: ProjectionStage Seçilen Alanların Alt Kümesine İndirgeme
 */
TEST_CASE(test_21_projection_stage_fields_subset) {
    ProjectionStage projection({"timestamp", "clientIp", "status", "path"});
    CollectingEmitter<LogRecord> emitter;

    auto now = std::chrono::system_clock::now();
    LogRecord fullRecord = LogRecord::Builder()
        .setTimestamp(now)
        .setClientIp("10.0.0.5")
        .setMethod("POST")
        .setPath("/submit")
        .setStatus(200)
        .setBytes(1500)
        .setUserAgent("Mozilla/5.0")
        .build();

    projection.process(fullRecord, emitter);

    ASSERT_EQ(emitter.size(), 1);
    const LogRecord& proj = emitter[0];
    ASSERT_TRUE(proj.isProjected());
    ASSERT_STR_EQ(proj.clientIp(), "10.0.0.5");
    ASSERT_STR_EQ(proj.path(), "/submit");
    ASSERT_EQ(proj.status(), 200);

    // Seçilmeyen alanların projeksiyonda sıfırlandığını/varsayılan olduğunu doğrula
    ASSERT_STR_EQ(proj.method(), "");
    ASSERT_EQ(proj.bytes(), 0);
    ASSERT_STR_EQ(proj.userAgent(), "");
}

/**
 * @brief 22. Test: ProjectionStage Veri Minimizasyonu ve Gizlilik (Privacy)
 */
TEST_CASE(test_22_projection_stage_data_minimization) {
    // Yalnızca durum ve yol seçilir; IP ve User-Agent gibi kişisel veriler ayıklanır
    ProjectionStage projection({"status", "path"});
    CollectingEmitter<LogRecord> emitter;

    LogRecord sensitive = LogRecord::Builder()
        .setClientIp("203.0.113.195")
        .setUserAgent("SecretBrowser/99")
        .setPath("/dashboard")
        .setStatus(200)
        .build();

    projection.process(sensitive, emitter);

    ASSERT_EQ(emitter.size(), 1);
    ASSERT_STR_EQ(emitter[0].clientIp(), "");
    ASSERT_STR_EQ(emitter[0].userAgent(), "");
    ASSERT_STR_EQ(emitter[0].path(), "/dashboard");
    ASSERT_EQ(emitter[0].status(), 200);
}

/**
 * @brief 23. Test: ProjectionStage toFormattedString Çıktısı
 */
TEST_CASE(test_23_projection_stage_to_formatted_string) {
    ProjectionStage projection({"clientIp", "status", "path"});
    CollectingEmitter<LogRecord> emitter;

    LogRecord rec = LogRecord::Builder()
        .setClientIp("192.168.1.1")
        .setStatus(404)
        .setPath("/notfound")
        .build();

    projection.process(rec, emitter);

    ASSERT_EQ(emitter.size(), 1);
    ASSERT_STR_EQ(emitter[0].toFormattedString(), "192.168.1.1 404 /notfound");
}

/**
 * @brief 24. Test: ProjectionStage Çoklu Kurucu Yöntemleri (vector, init_list, variadic)
 */
TEST_CASE(test_24_projection_stage_constructors) {
    ProjectionStage p1(std::vector<std::string>{"status", "path"});
    ProjectionStage p2{"status", "path"};
    ProjectionStage p3("status", "path");

    ASSERT_EQ(p1.getFields().size(), 2);
    ASSERT_EQ(p2.getFields().size(), 2);
    ASSERT_EQ(p3.getFields().size(), 2);
    ASSERT_STR_EQ(p3.getFields()[0], "status");
    ASSERT_STR_EQ(p3.getFields()[1], "path");
}

/**
 * @brief 25. Test: FileSink Yazma ve Kapatma Döngüsü
 */
TEST_CASE(test_25_filesink_write_and_close) {
    std::string testPath = "tests/output/filesink_test.log";
    FileSink sink(testPath);

    sink.open();
    LogRecord r1 = LogRecord::Builder().setClientIp("1.1.1.1").setMethod("GET").setPath("/").setStatus(200).setBytes(100).build();
    LogRecord r2 = LogRecord::Builder().setClientIp("2.2.2.2").setMethod("POST").setPath("/data").setStatus(201).setBytes(200).build();

    sink.consume(r1);
    sink.consume(r2);
    sink.close();

    auto lines = readFileLines(testPath);
    ASSERT_EQ(lines.size(), 2);
    ASSERT_TRUE(lines[0].find("1.1.1.1") != std::string::npos);
    ASSERT_TRUE(lines[1].find("2.2.2.2") != std::string::npos);
}

/**
 * @brief 26. Test: FileSink Açılmadan Tüketim Yapılırsa Hata Fırlatır
 */
TEST_CASE(test_26_filesink_lifecycle_unopened_throws) {
    FileSink sink("tests/output/unopened.log");
    LogRecord r = LogRecord::Builder().setStatus(200).build();

    bool threw = false;
    try {
        sink.consume(r);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    ASSERT_TRUE(threw);
}

/**
 * @brief 27. Test: FileSink close() Arabelleği Temizler ve Dosyayı Kapatır
 */
TEST_CASE(test_27_filesink_close_flushes_buffer) {
    std::string path = "tests/output/flush_test.log";
    {
        FileSink sink(path);
        sink.open();
        sink.consume(LogRecord::Builder().setClientIp("10.0.0.1").setMethod("GET").setPath("/flush").setStatus(200).setBytes(50).build());
        sink.close();
    }

    auto lines = readFileLines(path);
    ASSERT_EQ(lines.size(), 1);
    ASSERT_TRUE(lines[0].find("/flush") != std::string::npos);
}

/**
 * @brief 28. Test: PipelineBuilder Akıcı Çok Aşamalı Boru Hattı
 */
TEST_CASE(test_28_pipeline_builder_fluent_chain) {
    class DummyTestSource : public Source<std::string> {
    public:
        void produce(Emitter<std::string>& out) override {
            out.emit("192.168.1.1 - - [29/Sep/2026:10:00:00 +0300] \"GET /api/v1 HTTP/1.1\" 200 100");
            out.emit("192.168.1.2 - - [29/Sep/2026:10:01:00 +0300] \"GET /api/v2 HTTP/1.1\" 500 500");
        }
    };

    class DummyMemorySink : public Sink<LogRecord> {
    public:
        std::vector<LogRecord> items;
        void consume(const LogRecord& item) override {
            items.push_back(item);
        }
    };

    DummyMemorySink* memSink = new DummyMemorySink();
    Pipeline p = PipelineBuilder::from(new DummyTestSource())
        .via(new ParserStage())
        .via(new PredicateFilterStage(PredicateFilterStage::statusAtLeast(400)))
        .via(new ProjectionStage({"status", "path"}))
        .to(memSink, false);

    p.run();

    ASSERT_EQ(memSink->items.size(), 1);
    ASSERT_EQ(memSink->items[0].status(), 500);
    ASSERT_STR_EQ(memSink->items[0].path(), "/api/v2");
    delete memSink;
}

/**
 * @brief 29. Test: PipelineBuilder Yaşam Döngüsü Kancalarını (open/close) Sırayla Çalıştırır
 */
TEST_CASE(test_29_pipeline_builder_lifecycle_invocations) {
    static std::vector<std::string> lifecycleEvents;
    lifecycleEvents.clear();

    class LifecycleStage : public Stage<LogRecord, LogRecord> {
    private:
        std::string name;
    public:
        LifecycleStage(std::string n) : name(n) {}
        void open() override { lifecycleEvents.push_back(name + "_OPEN"); }
        void process(const LogRecord& in, Emitter<LogRecord>& out) override { out.emit(in); }
        void close() override { lifecycleEvents.push_back(name + "_CLOSE"); }
    };

    class LifecycleSink : public Sink<LogRecord> {
    public:
        void open() override { lifecycleEvents.push_back("SINK_OPEN"); }
        void consume(const LogRecord&) override {}
        void close() override { lifecycleEvents.push_back("SINK_CLOSE"); }
    };

    class DummySingleSource : public Source<LogRecord> {
    public:
        void produce(Emitter<LogRecord>& out) override {
            out.emit(LogRecord::Builder().setStatus(200).build());
        }
    };

    Pipeline p = PipelineBuilder::from(new DummySingleSource())
        .via(new LifecycleStage("STAGE1"))
        .via(new LifecycleStage("STAGE2"))
        .to(new LifecycleSink());

    p.run();

    // open() çağrıları: STAGE1 -> STAGE2 -> SINK
    ASSERT_TRUE(lifecycleEvents.size() == 6);
    ASSERT_STR_EQ(lifecycleEvents[0], "STAGE1_OPEN");
    ASSERT_STR_EQ(lifecycleEvents[1], "STAGE2_OPEN");
    ASSERT_STR_EQ(lifecycleEvents[2], "SINK_OPEN");

    // close() çağrıları: SINK -> STAGE2 -> STAGE1 (ters sıra kaynak yönetimi)
    ASSERT_STR_EQ(lifecycleEvents[3], "SINK_CLOSE");
    ASSERT_STR_EQ(lifecycleEvents[4], "STAGE2_CLOSE");
    ASSERT_STR_EQ(lifecycleEvents[5], "STAGE1_CLOSE");
}

/**
 * @brief 30. Test: Entegrasyon Testi - Test Fixture Üzerinde 3 Aşamalı Pipeline Çıktı Doğrulaması
 * 
 * Gereksinim:
 * "küçük bir test verisi (fixture) üzerinde 3 aşamalı bir pipeline çalıştırıp
 * çıktı dosyasının içeriğini doğrulayan (assert eden) bir entegrasyon testi."
 */
TEST_CASE(test_30_integration_3_stage_pipeline_fixture) {
    std::string fixturePath = "tests/fixtures/test_access.log";
    std::string outputPath = "tests/output/result_3stage.log";

    // 3 Aşamalı Pipeline: FileLineSource -> ParserStage -> PredicateFilterStage -> ProjectionStage -> FileSink
    Pipeline p = PipelineBuilder::from(new FileLineSource(fixturePath))
        .via(new ParserStage())
        .via(new PredicateFilterStage([](const LogRecord& r) { return r.status() >= 400; }))
        .via(new ProjectionStage({"timestamp", "clientIp", "status", "path"}))
        .to(new FileSink(outputPath));

    p.run();

    auto lines = readFileLines(outputPath);

    // Fixture içinde status >= 400 olan 4 geçerli satır vardır:
    // 192.168.1.15 (401), 192.168.1.25 (403), 192.168.1.30 (404), 192.168.1.35 (500)
    // Hatalı satır (MALFORMED_...) atlanmıştır.
    ASSERT_EQ(lines.size(), 4);

    ASSERT_STR_EQ(lines[0], "2026-09-29T07:01:05Z 192.168.1.15 401 /api/login");
    ASSERT_STR_EQ(lines[1], "2026-09-29T07:03:15Z 192.168.1.25 403 /api/secrets");
    ASSERT_STR_EQ(lines[2], "2026-09-29T07:04:20Z 192.168.1.30 404 /api/missing");
    ASSERT_STR_EQ(lines[3], "2026-09-29T07:05:25Z 192.168.1.35 500 /api/crash");
}

/**
 * @brief 31. Test: Entegrasyon Testi - Bileşilebilirlik ve Sıralama Bağımsızlığı (Order Invariance)
 * 
 * Gereksinim:
 * "Bileşilebilirliği deneysel olarak doğrulayın: aynı veriyi, filtre aşamaları
 * her iki sırada olacak şekilde pipeline'dan geçirin ve sonuçların aynı olduğunu belgeleyin."
 */
TEST_CASE(test_31_integration_composability_and_order_invariance) {
    std::string fixturePath = "tests/fixtures/test_access.log";
    std::string outOrderA = "tests/output/out_order_filter_first.log";
    std::string outOrderB = "tests/output/out_order_project_first.log";

    // Sıra A: Parser -> Filter -> Projection -> FileSink
    Pipeline pA = PipelineBuilder::from(new FileLineSource(fixturePath))
        .via(new ParserStage())
        .via(new PredicateFilterStage([](const LogRecord& r) { return r.status() >= 400; }))
        .via(new ProjectionStage({"timestamp", "clientIp", "status", "path"}))
        .to(new FileSink(outOrderA));
    pA.run();

    // Sıra B: Parser -> Projection -> Filter -> FileSink
    Pipeline pB = PipelineBuilder::from(new FileLineSource(fixturePath))
        .via(new ParserStage())
        .via(new ProjectionStage({"timestamp", "clientIp", "status", "path"}))
        .via(new PredicateFilterStage([](const LogRecord& r) { return r.status() >= 400; }))
        .to(new FileSink(outOrderB));
    pB.run();

    auto linesA = readFileLines(outOrderA);
    auto linesB = readFileLines(outOrderB);

    ASSERT_EQ(linesA.size(), linesB.size());
    for (size_t i = 0; i < linesA.size(); ++i) {
        ASSERT_STR_EQ(linesA[i], linesB[i]);
    }
}

/**
 * @brief 32. Test: Coklu Filtre Asamalarinin Arka Arkaya Zincirlenmesi (Multiple Filters in Series)
 * 
 * Gereksinim:
 * "Aşamaların (stage) herhangi bir sırada ve herhangi bir sayıda bir araya getirilebildiğini göstermek."
 * 
 * Bu test, boru hattında BİRDEN FAZLA PredicateFilterStage aşamasının (status >= 400 VE path startsWith /api VE bytes > 300)
 * arka arkaya eklenebildiğini ve her birinin tek sorumlulukla çalıştığını doğrular.
 */
TEST_CASE(test_32_multiple_predicate_filters_chained) {
    std::string fixturePath = "tests/fixtures/test_access.log";
    std::string outMulti = "tests/output/out_multi_filters.log";

    // 1. ParserStage
    // 2. Filtre 1: status >= 400
    // 3. Filtre 2: path startsWith /api
    // 4. Filtre 3: bytes > 300
    // 5. ProjectionStage: status, path, bytes
    // 6. FileSink
    Pipeline p = PipelineBuilder::from(new FileLineSource(fixturePath))
        .via(new ParserStage())
        .via(new PredicateFilterStage(PredicateFilterStage::statusAtLeast(400)))
        .via(new PredicateFilterStage(PredicateFilterStage::pathStartsWith("/api")))
        .via(new PredicateFilterStage([](const LogRecord& r) { return r.bytes() > 300; }))
        .via(new ProjectionStage({"status", "path", "bytes"}))
        .to(new FileSink(outMulti));
    p.run();

    auto lines = readFileLines(outMulti);
    // Fixture içinde status >= 400 ve /api olanlar:
    // 192.168.1.15 -> 401, /api/login, 512B (>300 -> KALIR)
    // 192.168.1.25 -> 403, /api/secrets, 256B (<=300 -> ELENIR)
    // 192.168.1.30 -> 404, /api/missing, 128B (<=300 -> ELENIR)
    // 192.168.1.35 -> 500, /api/crash, 4096B (>300 -> KALIR)
    ASSERT_EQ(lines.size(), 2);
    ASSERT_STR_EQ(lines[0], "401 /api/login 512");
    ASSERT_STR_EQ(lines[1], "500 /api/crash 4096");
}

// ============================================================================
// ARTIM 4: YAPILANDIRMA YÜKLEYİCİ (PIPELINELOADER) VE KAYIT DEFTERİ TESTLERİ (33-42)
// ============================================================================

/**
 * @brief 33. Test (Görev 6.1): Geçerli Bir Yapılandırma (Valid Configuration)
 */
TEST_CASE(test_33_loader_valid_configuration) {
    std::string yaml =
        "pipeline:\n"
        "  name: test-error-report\n"
        "  source:\n"
        "    type: file\n"
        "    params: { path: tests/fixtures/test_access.log }\n"
        "  stages:\n"
        "    - type: parse\n"
        "      params: { format: clf }\n"
        "    - type: filter\n"
        "      params: { field: status, op: \">=\", value: 400 }\n"
        "    - type: project\n"
        "      params: { fields: [timestamp, clientIp, status, path] }\n"
        "  sink:\n"
        "    type: file\n"
        "    params: { path: tests/output/loader_valid_out.txt }\n";

    PipelineLoader loader;
    Pipeline<> p = loader.loadFromString(yaml);
    ASSERT_STR_EQ(p.getName(), "test-error-report");
    p.run();

    auto lines = readFileLines("tests/output/loader_valid_out.txt");
    ASSERT_EQ(lines.size(), 4);
    ASSERT_STR_EQ(lines[0], "2026-09-29T07:01:05Z 192.168.1.15 401 /api/login");
    ASSERT_STR_EQ(lines[3], "2026-09-29T07:05:25Z 192.168.1.35 500 /api/crash");
}

/**
 * @brief 34. Test (Görev 6.2): Bilinmeyen Bir Aşama Türü (Unknown Stage Type)
 */
TEST_CASE(test_34_loader_unknown_stage_type) {
    std::string yaml =
        "pipeline:\n"
        "  name: unknown-stage-test\n"
        "  source:\n"
        "    type: file\n"
        "    params: { path: data/access.log }\n"
        "  stages:\n"
        "    - type: parse\n"
        "      params: { format: clf }\n"
        "    - type: non_existent_stage\n"
        "      params: { foo: bar }\n"
        "  sink:\n"
        "    type: file\n"
        "    params: { path: tests/output/should_not_exist.txt }\n";

    PipelineLoader loader;
    bool caughtConfigError = false;
    std::string errMsg;
    try {
        Pipeline<> p = loader.loadFromString(yaml);
        p.run();
    } catch (const ConfigValidationException& e) {
        caughtConfigError = true;
        errMsg = e.what();
    }

    ASSERT_TRUE(caughtConfigError);
    ASSERT_TRUE(errMsg.find("non_existent_stage") != std::string::npos);
    ASSERT_TRUE(errMsg.find("Bilinmeyen asama turu") != std::string::npos);
}

/**
 * @brief 35. Test (Görev 6.3): Eksik Bir Parametre (Missing Parameter)
 */
TEST_CASE(test_35_loader_missing_parameter) {
    // 'filter' aşamasında zorunlu 'value' parametresi eksik
    std::string yamlMissingFilterValue =
        "pipeline:\n"
        "  name: missing-param-test\n"
        "  source:\n"
        "    type: file\n"
        "    params: { path: data/access.log }\n"
        "  stages:\n"
        "    - type: parse\n"
        "      params: { format: clf }\n"
        "    - type: filter\n"
        "      params: { field: status, op: \">=\" }\n"
        "  sink:\n"
        "    type: file\n"
        "    params: { path: tests/output/missing_param.txt }\n";

    PipelineLoader loader;
    bool caughtMissingValue = false;
    std::string errMsg1;
    try {
        loader.loadFromString(yamlMissingFilterValue);
    } catch (const ConfigValidationException& e) {
        caughtMissingValue = true;
        errMsg1 = e.what();
    }
    ASSERT_TRUE(caughtMissingValue);
    ASSERT_TRUE(errMsg1.find("Eksik parametre") != std::string::npos);
    ASSERT_TRUE(errMsg1.find("value") != std::string::npos);

    // 'source (type: file)' içinde zorunlu 'path' parametresi eksik
    std::string yamlMissingSourcePath =
        "pipeline:\n"
        "  name: missing-source-path\n"
        "  source:\n"
        "    type: file\n"
        "    params: {}\n"
        "  stages:\n"
        "    - type: parse\n"
        "      params: { format: clf }\n"
        "  sink:\n"
        "    type: console\n";

    bool caughtMissingPath = false;
    try {
        loader.loadFromString(yamlMissingSourcePath);
    } catch (const ConfigValidationException&) {
        caughtMissingPath = true;
    }
    ASSERT_TRUE(caughtMissingPath);
}

/**
 * @brief 36. Test (Görev 6.4): Yanlış Türde Bir Parametre (Wrong Parameter Type)
 */
TEST_CASE(test_36_loader_wrong_parameter_type) {
    PipelineLoader loader;

    // Durum 1: Sayısal 'status' alanı için tamsayı yerine metin ("not_a_number") verilmesi
    std::string yamlWrongInt =
        "pipeline:\n"
        "  name: wrong-type-int\n"
        "  source:\n"
        "    type: file\n"
        "    params: { path: data/access.log }\n"
        "  stages:\n"
        "    - type: parse\n"
        "      params: { format: clf }\n"
        "    - type: filter\n"
        "      params: { field: status, op: \">=\", value: \"not_a_number\" }\n"
        "  sink:\n"
        "    type: console\n";

    bool caughtWrongInt = false;
    std::string errMsg1;
    try {
        loader.loadFromString(yamlWrongInt);
    } catch (const ConfigValidationException& e) {
        caughtWrongInt = true;
        errMsg1 = e.what();
    }
    ASSERT_TRUE(caughtWrongInt);
    ASSERT_TRUE(errMsg1.find("Yanlis turde parametre") != std::string::npos);
    ASSERT_TRUE(errMsg1.find("integer") != std::string::npos);

    // Durum 2: 'project' aşamasında 'fields' için liste ([...]) yerine skaler tamsayı (400) verilmesi
    std::string yamlWrongList =
        "pipeline:\n"
        "  name: wrong-type-list\n"
        "  source:\n"
        "    type: file\n"
        "    params: { path: data/access.log }\n"
        "  stages:\n"
        "    - type: parse\n"
        "      params: { format: clf }\n"
        "    - type: project\n"
        "      params: { fields: 400 }\n"
        "  sink:\n"
        "    type: console\n";

    bool caughtWrongList = false;
    try {
        loader.loadFromString(yamlWrongList);
    } catch (const ConfigValidationException&) {
        caughtWrongList = true;
    }
    ASSERT_TRUE(caughtWrongList);

    // Durum 3: Metin alanı 'path' için string yerine integer (404) verilmesi
    std::string yamlWrongString =
        "pipeline:\n"
        "  name: wrong-type-string\n"
        "  source:\n"
        "    type: file\n"
        "    params: { path: data/access.log }\n"
        "  stages:\n"
        "    - type: parse\n"
        "      params: { format: clf }\n"
        "    - type: filter\n"
        "      params: { field: path, op: \"startsWith\", value: 404 }\n"
        "  sink:\n"
        "    type: console\n";

    bool caughtWrongStr = false;
    try {
        loader.loadFromString(yamlWrongString);
    } catch (const ConfigValidationException&) {
        caughtWrongStr = true;
    }
    ASSERT_TRUE(caughtWrongStr);
}

/**
 * @brief 37. Test (Görev 6.5): Boş Bir İşlem Hattı (Empty Pipeline)
 */
TEST_CASE(test_37_loader_empty_pipeline) {
    PipelineLoader loader;

    // Durum 1: Tamamen boş YAML içeriği
    bool caughtEmptyDoc = false;
    try {
        loader.loadFromString("   \n  # sadece yorum satiri \n");
    } catch (const ConfigValidationException& e) {
        caughtEmptyDoc = true;
        ASSERT_TRUE(std::string(e.what()).find("Bos islem hatti") != std::string::npos);
    }
    ASSERT_TRUE(caughtEmptyDoc);

    // Durum 2: 'stages: []' (hiç aşama içermeyen işlem hattı)
    std::string yamlEmptyStages =
        "pipeline:\n"
        "  name: empty-stages\n"
        "  source:\n"
        "    type: file\n"
        "    params: { path: data/access.log }\n"
        "  stages: []\n"
        "  sink:\n"
        "    type: file\n"
        "    params: { path: out/errors.txt }\n";

    bool caughtEmptyStages = false;
    try {
        loader.loadFromString(yamlEmptyStages);
    } catch (const ConfigValidationException& e) {
        caughtEmptyStages = true;
        ASSERT_TRUE(std::string(e.what()).find("Bos islem hatti") != std::string::npos);
    }
    ASSERT_TRUE(caughtEmptyStages);

    // Durum 3: 'source' veya 'sink' eksik olan işlem hattı
    std::string yamlNoSink =
        "pipeline:\n"
        "  name: no-sink\n"
        "  source:\n"
        "    type: file\n"
        "    params: { path: data/access.log }\n"
        "  stages:\n"
        "    - type: parse\n"
        "      params: { format: clf }\n";

    bool caughtNoSink = false;
    try {
        loader.loadFromString(yamlNoSink);
    } catch (const ConfigValidationException&) {
        caughtNoSink = true;
    }
    ASSERT_TRUE(caughtNoSink);
}

/**
 * @brief 38. Test (Görev 4): Üç Ayrı Yapılandırma Dosyasının Aynı Çalıştırılabilir Kodla
 * Gerçekten Farklı Davranışlar ve Çıktılar Üretmesi
 */
TEST_CASE(test_38_three_config_files_produce_different_outputs) {
    PipelineLoader loader;

    // 1. Hata Raporu (configs/error-report.yaml -> out/errors.txt)
    Pipeline<> pError = loader.loadFromFile("configs/error-report.yaml");
    ASSERT_STR_EQ(pError.getName(), "error-report");
    pError.run();

    // 2. API Trafiği Veri Çıkarımı (configs/api-traffic.yaml -> out/api-traffic.txt)
    Pipeline<> pApi = loader.loadFromFile("configs/api-traffic.yaml");
    ASSERT_STR_EQ(pApi.getName(), "api-traffic");
    pApi.run();

    // 3. İstemci Bazında Bant Genişliği (configs/bandwidth-by-client.yaml -> out/bandwidth-by-client.txt)
    Pipeline<> pBw = loader.loadFromFile("configs/bandwidth-by-client.yaml");
    ASSERT_STR_EQ(pBw.getName(), "bandwidth-by-client");
    pBw.run();

    auto errLines = readFileLines("out/errors.txt");
    auto apiLines = readFileLines("out/api-traffic.txt");
    auto bwLines  = readFileLines("out/bandwidth-by-client.txt");

    // Her üç çıktı da farklı satır sayısına ve farklı alan projeksiyonuna sahiptir
    ASSERT_EQ(errLines.size(), 3); // 401, 404, 500
    ASSERT_EQ(apiLines.size(), 5); // /api/login, /api/users, /api/orders, /api/pay, /api/session
    ASSERT_EQ(bwLines.size(), 4);  // bytes >= 1024 olan istemciler

    ASSERT_STR_EQ(errLines[0], "2026-09-29T07:01:05Z 192.168.1.15 401 /api/login");
    ASSERT_STR_EQ(apiLines[0], "2026-09-29T07:01:05Z POST /api/login 401 512");
    ASSERT_STR_EQ(bwLines[0],  "192.168.1.10 1024 /index.html 200");
}

/**
 * @brief 39. Test (Görev 5): StageRegistry Veri Odaklı Dinamik Eklenti Kaydı (Plugin Extensibility)
 */
TEST_CASE(test_39_stage_registry_dynamic_plugin_registration) {
    // Mevcut kodu değiştirmeden çalışma zamanında yeni bir aşama türü ('anonymize_ip') kaydediyoruz
    class AnonymizeIpStage : public Stage<LogRecord, LogRecord> {
    public:
        void process(const LogRecord& in, Emitter<LogRecord>& out) override {
            LogRecord masked = LogRecord::Builder()
                .setTimestamp(in.timestamp())
                .setClientIp("0.0.0.0")
                .setMethod(in.method())
                .setPath(in.path())
                .setStatus(in.status())
                .setBytes(in.bytes())
                .setUserAgent(in.userAgent())
                .setAttributes(in.attributes())
                .setRaw(in.raw())
                .build();
            out.emit(masked);
        }
    };

    StageRegistry customRegistry;
    customRegistry.registerStage("anonymize_ip", [](const StageParams& params) -> Stage<LogRecord, LogRecord>* {
        params.rejectUnknownParams({}, "stage (type: anonymize_ip)");
        return new AnonymizeIpStage();
    });

    ASSERT_TRUE(customRegistry.hasStage("anonymize_ip"));

    std::string yaml =
        "pipeline:\n"
        "  name: plugin-pipeline\n"
        "  source:\n"
        "    type: file\n"
        "    params: { path: tests/fixtures/test_access.log }\n"
        "  stages:\n"
        "    - type: parse\n"
        "      params: { format: clf }\n"
        "    - type: anonymize_ip\n"
        "    - type: project\n"
        "      params: { fields: [clientIp, status, path] }\n"
        "  sink:\n"
        "    type: file\n"
        "    params: { path: tests/output/plugin_out.txt }\n";

    PipelineLoader loader(customRegistry);
    Pipeline<> p = loader.loadFromString(yaml);
    p.run();

    auto lines = readFileLines("tests/output/plugin_out.txt");
    ASSERT_EQ(lines.size(), 6);
    ASSERT_STR_EQ(lines[0], "0.0.0.0 200 /index.html");
}

/**
 * @brief 40. Test: Aşama Akış Türü Uyumsuzluğu (Parse Öncesi Filter Kullanımı Hatası)
 */
TEST_CASE(test_40_loader_stage_stream_type_mismatch) {
    std::string yaml =
        "pipeline:\n"
        "  name: filter-before-parse\n"
        "  source:\n"
        "    type: file\n"
        "    params: { path: data/access.log }\n"
        "  stages:\n"
        "    - type: filter\n"
        "      params: { field: status, op: \">=\", value: 400 }\n"
        "    - type: parse\n"
        "      params: { format: clf }\n"
        "  sink:\n"
        "    type: console\n";

    PipelineLoader loader;
    bool caughtMismatch = false;
    try {
        loader.loadFromString(yaml);
    } catch (const ConfigValidationException& e) {
        caughtMismatch = true;
        ASSERT_TRUE(std::string(e.what()).find("LogRecord") != std::string::npos);
    }
    ASSERT_TRUE(caughtMismatch);
}

/**
 * @brief 41. Test: Bilinmeyen Source/Sink Türü ve Tanımsız Parametre Denetimi
 */
TEST_CASE(test_41_loader_unknown_source_sink_and_extra_params) {
    PipelineLoader loader;

    // Bilinmeyen source türü
    bool caughtUnknownSource = false;
    try {
        loader.loadFromString(
            "pipeline:\n"
            "  name: bad-src\n"
            "  source:\n"
            "    type: kafka\n"
            "  stages:\n"
            "    - type: parse\n"
            "      params: { format: clf }\n"
            "  sink:\n"
            "    type: console\n");
    } catch (const ConfigValidationException&) {
        caughtUnknownSource = true;
    }
    ASSERT_TRUE(caughtUnknownSource);

    // Bilinmeyen sink türü
    bool caughtUnknownSink = false;
    try {
        loader.loadFromString(
            "pipeline:\n"
            "  name: bad-sink\n"
            "  source:\n"
            "    type: file\n"
            "    params: { path: data/access.log }\n"
            "  stages:\n"
            "    - type: parse\n"
            "      params: { format: clf }\n"
            "  sink:\n"
            "    type: elasticsearch\n");
    } catch (const ConfigValidationException&) {
        caughtUnknownSink = true;
    }
    ASSERT_TRUE(caughtUnknownSink);

    // Fazladan tanımsız parametre
    bool caughtExtraParam = false;
    try {
        loader.loadFromString(
            "pipeline:\n"
            "  name: extra-param\n"
            "  source:\n"
            "    type: file\n"
            "    params: { path: data/access.log, encoding: utf8 }\n"
            "  stages:\n"
            "    - type: parse\n"
            "      params: { format: clf }\n"
            "  sink:\n"
            "    type: console\n");
    } catch (const ConfigValidationException&) {
        caughtExtraParam = true;
    }
    ASSERT_TRUE(caughtExtraParam);
}

/**
 * @brief 42. Test: StageFactory Filter Operatörleri ve Alanlarının Kapsamlı Doğrulaması
 */
TEST_CASE(test_42_filter_factory_all_operators_and_fields) {
    LogRecord rec = LogRecord::Builder()
        .setClientIp("192.168.1.10")
        .setMethod("POST")
        .setPath("/api/v1/orders.json")
        .setStatus(201)
        .setBytes(2048)
        .setUserAgent("Mozilla/5.0")
        .build();

    auto runSingleFilter = [&](const std::string& inlineParams) -> size_t {
        ConfigValue cv = YamlConfigParser::parseInlineValue(inlineParams);
        StageParams sp(cv.asMap());
        std::unique_ptr<Stage<LogRecord, LogRecord>> stg(StageFactory::createFilterStage(sp));
        CollectingEmitter<LogRecord> em;
        stg->process(rec, em);
        return em.size();
    };

    ASSERT_EQ(runSingleFilter("{ field: status, op: \"==\", value: 201 }"), 1);
    ASSERT_EQ(runSingleFilter("{ field: status, op: \"!=\", value: 500 }"), 1);
    ASSERT_EQ(runSingleFilter("{ field: bytes, op: \">\", value: 1000 }"), 1);
    ASSERT_EQ(runSingleFilter("{ field: bytes, op: \"<\", value: 4096 }"), 1);
    ASSERT_EQ(runSingleFilter("{ field: bytes, op: \"<=\", value: 2048 }"), 1);
    ASSERT_EQ(runSingleFilter("{ field: path, op: \"startsWith\", value: \"/api\" }"), 1);
    ASSERT_EQ(runSingleFilter("{ field: path, op: \"contains\", value: \"orders\" }"), 1);
    ASSERT_EQ(runSingleFilter("{ field: path, op: \"endsWith\", value: \".json\" }"), 1);
    ASSERT_EQ(runSingleFilter("{ field: method, op: \"==\", value: \"POST\" }"), 1);
    ASSERT_EQ(runSingleFilter("{ field: clientIp, op: \"!=\", value: \"10.0.0.1\" }"), 1);
}

// ============================================================================
// ARTIM 5: HATA YÖNETİMİ, DOĞRULAMA, DEAD-LETTER VE KAOS TESTLERİ (43-52)
// ============================================================================

/**
 * @brief 43. Test (Görev 1): StageException (Kurtarılabilir) vs PipelineException (Kurtarılamaz)
 */
TEST_CASE(test_43_error_model_stage_vs_pipeline_exception) {
    // 1. StageException: Tek bir kayıt işlenirken oluşan kurtarılabilir aşama hatası
    StageException stgEx("parse", "bozuk_ham_satir", "TRUNCATED_LINE", "Kesilmis satir");
    ASSERT_STR_EQ(stgEx.getStageName(), "parse");
    ASSERT_STR_EQ(stgEx.getRawInput(), "bozuk_ham_satir");
    ASSERT_STR_EQ(stgEx.getErrorCode(), "TRUNCATED_LINE");
    ASSERT_TRUE(std::string(stgEx.what()).find("TRUNCATED_LINE") != std::string::npos);

    // 2. PipelineException: İşlem hattının devam edemeyeceği kurtarılamaz hata (ör. okunamayan dosya)
    FileLineSource missingSource("data/non_existent_file_12345.log");
    CollectingEmitter<std::string> emitter;
    bool caughtPipelineEx = false;
    try {
        missingSource.produce(emitter);
    } catch (const PipelineException& e) {
        caughtPipelineEx = true;
        ASSERT_STR_EQ(e.getComponentName(), "FileLineSource");
    }
    ASSERT_TRUE(caughtPipelineEx);
}

/**
 * @brief 44. Test (Görev 2): ErrorPolicy Ayrıştırma ve Mod Doğrulaması
 */
TEST_CASE(test_44_error_policy_parsing_and_modes) {
    ErrorPolicy pFail = ErrorPolicy::parse("FAIL_FAST");
    ASSERT_TRUE(pFail.isFailFast());
    ASSERT_STR_EQ(pFail.toString(), "FAIL_FAST");

    ErrorPolicy pSkip = ErrorPolicy::parse("SKIP");
    ASSERT_TRUE(pSkip.isSkip());
    ASSERT_STR_EQ(pSkip.toString(), "SKIP");

    ErrorPolicy pDl = ErrorPolicy::parse("DEAD_LETTER");
    ASSERT_TRUE(pDl.isDeadLetter());
    ASSERT_STR_EQ(pDl.toString(), "DEAD_LETTER");

    ErrorPolicy pRetry = ErrorPolicy::parse("RETRY(3)");
    ASSERT_TRUE(pRetry.isRetry());
    ASSERT_EQ(pRetry.maxRetries(), 3);
    ASSERT_STR_EQ(pRetry.toString(), "RETRY(3)");

    bool caughtInvalidPolicy = false;
    try {
        ErrorPolicy::parse("IGNORE_SILENTLY");
    } catch (const PipelineException&) {
        caughtInvalidPolicy = true;
    }
    ASSERT_TRUE(caughtInvalidPolicy);
}

/**
 * @brief 45. Test (Görev 2): FAIL_FAST Politikası İlk Hatada Boru Hattını Durdurur
 */
TEST_CASE(test_45_policy_fail_fast_stops_pipeline) {
    class MemoryLineSource : public Source<std::string> {
    public:
        void produce(Emitter<std::string>& out) override {
            out.emit("192.168.1.10 - - [29/Sep/2026:10:00:00 +0300] \"GET /ok1 HTTP/1.1\" 200 100");
            out.emit("KESILMIS_BOZUK_SATIR_FAIL_FAST");
            out.emit("192.168.1.11 - - [29/Sep/2026:10:01:00 +0300] \"GET /ok2 HTTP/1.1\" 200 200");
        }
    };

    class MemoryRecordSink : public Sink<LogRecord> {
    public:
        std::vector<LogRecord> records;
        void consume(const LogRecord& r) override { records.push_back(r); }
    };

    MemoryRecordSink* sink = new MemoryRecordSink();
    Pipeline<> p = PipelineBuilder::from(new MemoryLineSource())
        .via(new ParserStage(true), ErrorPolicy::failFast(), "parse")
        .to(sink, false);
    p.setPrintSummary(false);

    bool abortedWithPipelineException = false;
    try {
        p.run();
    } catch (const PipelineException& e) {
        abortedWithPipelineException = true;
        ASSERT_TRUE(std::string(e.what()).find("FAIL_FAST") != std::string::npos);
    }

    ASSERT_TRUE(abortedWithPipelineException);
    // İlk geçerli kayıt işlenmiş, 2. kayıtta FAIL_FAST tetiklenmiş, 3. kayıt hiç işlenmemiştir
    ASSERT_EQ(sink->records.size(), 1);
    delete sink;
}

/**
 * @brief 46. Test (Görev 2 & 4): SKIP Politikası Hatalı Kayıtları Atlar ve RunSummary'de Sayar
 */
TEST_CASE(test_46_policy_skip_counts_and_continues) {
    std::string yaml =
        "pipeline:\n"
        "  name: skip-policy-test\n"
        "  source:\n"
        "    type: file\n"
        "    params: { path: data/access-dirty.log }\n"
        "  stages:\n"
        "    - type: parse\n"
        "      policy: SKIP\n"
        "      params: { format: combined }\n"
        "    - type: validate\n"
        "      policy: SKIP\n"
        "      params:\n"
        "        requiredFields: [clientIp, method, path, timestamp]\n"
        "        statusRange: [100, 599]\n"
        "        bytesRange: [0, 104857600]\n"
        "        timestampYearRange: [2000, 2035]\n"
        "        validateIp: true\n"
        "  sink:\n"
        "    type: file\n"
        "    params: { path: tests/output/skip_policy_out.txt }\n";

    PipelineLoader loader;
    Pipeline<> p = loader.loadFromString(yaml);
    p.setPrintSummary(false);
    RunSummary summary = p.run();

    ASSERT_EQ(summary.getRecordsIn(), 11);
    ASSERT_EQ(summary.getRecordsOut(), 3);
    ASSERT_EQ(summary.getTotalSkipped(), 8);
    ASSERT_EQ(summary.getDeadLetterCount(), 0);
    ASSERT_EQ(summary.getDroppedAtStage("parse"), 5);
    ASSERT_EQ(summary.getDroppedAtStage("validate"), 3);
    ASSERT_TRUE(summary.isAccountingConsistent());
}

/**
 * @brief 47. Test (Görev 2 & 3): DEAD_LETTER Politikası ve FailedRecord Tanılama Bağlamı
 */
TEST_CASE(test_47_policy_dead_letter_routes_failed_records) {
    class DirtyMiniSource : public Source<std::string> {
    public:
        void produce(Emitter<std::string>& out) override {
            out.emit("192.168.1.10 - - [29/Sep/2026:10:00:00 +0300] \"GET /valid HTTP/1.1\" 200 512");
            out.emit("192.168.1.20 - - [29/Sep/2026:10:01:00 +0300] \"GET /truncated");
            out.emit("192.168.1.30 - - [29/Sep/2026:10:02:00 +0300] \"GET /neg HTTP/1.1\" 200 -100");
        }
    };

    class VecSink : public Sink<LogRecord> {
    public:
        std::vector<LogRecord> items;
        void consume(const LogRecord& r) override { items.push_back(r); }
    };

    VecSink* mainSink = new VecSink();
    DeadLetterMemorySink* dlSink = new DeadLetterMemorySink();

    Pipeline<> p = PipelineBuilder::from(new DirtyMiniSource())
        .withDeadLetterSink(dlSink, false)
        .via(new ParserStage(true, true), ErrorPolicy::deadLetter(), "parse")
        .via(new ValidationStage(), ErrorPolicy::deadLetter(), "validate")
        .to(mainSink, false);
    p.setPrintSummary(false);

    RunSummary summary = p.run();

    ASSERT_EQ(summary.getRecordsIn(), 3);
    ASSERT_EQ(summary.getRecordsOut(), 1);
    ASSERT_EQ(summary.getDeadLetterCount(), 2);
    ASSERT_EQ(mainSink->items.size(), 1);
    ASSERT_EQ(dlSink->size(), 2);

    // 1. Başarısız kayıt: 'parse' aşamasından TRUNCATED_LINE
    ASSERT_STR_EQ((*dlSink)[0].getStageName(), "parse");
    ASSERT_STR_EQ((*dlSink)[0].getErrorCode(), "TRUNCATED_LINE");
    ASSERT_TRUE((*dlSink)[0].getRawInput().find("/truncated") != std::string::npos);
    ASSERT_TRUE(!(*dlSink)[0].getTimestampIso().empty());

    // 2. Başarısız kayıt: 'validate' aşamasından NEGATIVE_BYTES
    ASSERT_STR_EQ((*dlSink)[1].getStageName(), "validate");
    ASSERT_STR_EQ((*dlSink)[1].getErrorCode(), "NEGATIVE_BYTES");
    ASSERT_TRUE((*dlSink)[1].getRawInput().find("-100") != std::string::npos);

    delete mainSink;
    delete dlSink;
}

/**
 * @brief 48. Test (Görev 2): RETRY(n) Politikası Geçici Hataları Kurtarır, Kalıcı Hataları Yönlendirir
 */
TEST_CASE(test_48_policy_retry_n_recovers_or_falls_back) {
    // 1. kayıtta 2 kez başarısız olup 3. denemede (2. retry) başarılı olan,
    // 2. kayıtta ise sürekli başarısız olan simüle edilmiş I/O zenginleştirme aşaması
    class FlakyIoStage : public Stage<LogRecord, LogRecord> {
    private:
        int attemptsForFirst_ = 0;
    public:
        std::string stageTypeName() const override { return "io_enrich"; }
        void process(const LogRecord& in, Emitter<LogRecord>& out) override {
            if (in.path() == "/transient-error") {
                attemptsForFirst_++;
                if (attemptsForFirst_ < 3) {
                    throw StageException("io_enrich", in.raw(), "IO_TIMEOUT", "Gecici baglanti zaman asimi");
                }
                out.emit(in);
            } else {
                throw StageException("io_enrich", in.raw(), "IO_PERMANENT", "Kalici sunucu hatasi");
            }
        }
    };

    class TwoRecordSource : public Source<LogRecord> {
    public:
        void produce(Emitter<LogRecord>& out) override {
            out.emit(LogRecord::Builder().setPath("/transient-error").setRaw("raw_transient").setStatus(200).build());
            out.emit(LogRecord::Builder().setPath("/permanent-error").setRaw("raw_permanent").setStatus(200).build());
        }
    };

    class MemSink : public Sink<LogRecord> {
    public:
        std::vector<LogRecord> out;
        void consume(const LogRecord& r) override { out.push_back(r); }
    };

    MemSink* mainSink = new MemSink();
    DeadLetterMemorySink* dlSink = new DeadLetterMemorySink();

    Pipeline<> p = PipelineBuilder::from(new TwoRecordSource())
        .withDeadLetterSink(dlSink, false)
        .via(new FlakyIoStage(), ErrorPolicy::retry(3, ErrorPolicyType::DEAD_LETTER), "io_enrich")
        .to(mainSink, false);
    p.setPrintSummary(false);

    RunSummary summary = p.run();

    // 1. kayıt 2 retry sonunda kurtarıldı -> mainSink'e ulaştı
    // 2. kayıt 3 retry sonunda hala başarısız oldu -> dlSink'e yönlendirildi
    // Toplam retry sayısı = 2 (ilk kayıt) + 3 (ikinci kayıt) = 5
    ASSERT_EQ(summary.getRecordsIn(), 2);
    ASSERT_EQ(summary.getRecordsOut(), 1);
    ASSERT_EQ(summary.getDeadLetterCount(), 1);
    ASSERT_EQ(summary.getTotalRetries(), 5);
    ASSERT_EQ(dlSink->size(), 1);
    ASSERT_STR_EQ((*dlSink)[0].getErrorCode(), "IO_PERMANENT");

    delete mainSink;
    delete dlSink;
}

/**
 * @brief 49. Test (Görev 3): ValidationStage Tüm Bildirimsel Doğrulama Kuralları
 */
TEST_CASE(test_49_validation_stage_declarative_rules) {
    ValidationRules rules;
    rules.requiredFields = {"clientIp", "method", "path", "userAgent"};
    rules.minStatus = 100;
    rules.maxStatus = 599;
    rules.minBytes = 0;
    rules.maxBytes = 10000;
    rules.minYear = 2000;
    rules.maxYear = 2035;
    rules.validateIp = true;
    rules.allowedMethods = {"GET", "POST"};

    ValidationStage validator(rules);
    CollectingEmitter<LogRecord> emitter;

    // 2026 yılına ait geçerli zaman damgası (1790665200 = 2026-09-29T07:00:00Z)
    auto validTp = std::chrono::system_clock::from_time_t(1790665200);

    // 1. Geçerli kayıt
    LogRecord validRec = LogRecord::Builder()
        .setTimestamp(validTp)
        .setClientIp("192.168.1.10")
        .setMethod("GET")
        .setPath("/index.html")
        .setStatus(200)
        .setBytes(512)
        .setUserAgent("Mozilla/5.0")
        .setRaw("valid_raw")
        .build();
    validator.process(validRec, emitter);
    ASSERT_EQ(emitter.size(), 1);

    // 2. Eksik zorunlu alan (userAgent boş)
    bool caughtMissingField = false;
    try {
        LogRecord r = LogRecord::Builder()
            .setTimestamp(validTp).setClientIp("192.168.1.10").setMethod("GET")
            .setPath("/").setStatus(200).setBytes(100).setUserAgent("").build();
        validator.process(r, emitter);
    } catch (const StageException& e) {
        caughtMissingField = (e.getErrorCode() == "MISSING_REQUIRED_FIELD");
    }
    ASSERT_TRUE(caughtMissingField);

    // 3. Geçersiz IP biçimi (999.999.999.999)
    bool caughtBadIp = false;
    try {
        LogRecord r = LogRecord::Builder()
            .setTimestamp(validTp).setClientIp("999.999.999.999").setMethod("GET")
            .setPath("/").setStatus(200).setBytes(100).setUserAgent("UA").build();
        validator.process(r, emitter);
    } catch (const StageException& e) {
        caughtBadIp = (e.getErrorCode() == "INVALID_IP_FORMAT");
    }
    ASSERT_TRUE(caughtBadIp);

    // 4. Negatif bayt sayısı (-512)
    bool caughtNegBytes = false;
    try {
        LogRecord r = LogRecord::Builder()
            .setTimestamp(validTp).setClientIp("10.0.0.1").setMethod("GET")
            .setPath("/").setStatus(200).setBytes(-512).setUserAgent("UA").build();
        validator.process(r, emitter);
    } catch (const StageException& e) {
        caughtNegBytes = (e.getErrorCode() == "NEGATIVE_BYTES");
    }
    ASSERT_TRUE(caughtNegBytes);

    // 5. 1970 tarihli zaman damgası (tutarsız yıl)
    bool caught1970 = false;
    try {
        auto tp1970 = std::chrono::system_clock::from_time_t(5);
        LogRecord r = LogRecord::Builder()
            .setTimestamp(tp1970).setClientIp("10.0.0.1").setMethod("GET")
            .setPath("/").setStatus(200).setBytes(100).setUserAgent("UA").build();
        validator.process(r, emitter);
    } catch (const StageException& e) {
        caught1970 = (e.getErrorCode() == "TIMESTAMP_OUT_OF_RANGE");
    }
    ASSERT_TRUE(caught1970);

    // 6. 2099 tarihli zaman damgası (tutarsız yıl)
    bool caught2099 = false;
    try {
        // 2099-01-01 ~ 4070908800
        auto tp2099 = std::chrono::system_clock::from_time_t(static_cast<time_t>(4070908800LL));
        LogRecord r = LogRecord::Builder()
            .setTimestamp(tp2099).setClientIp("10.0.0.1").setMethod("GET")
            .setPath("/").setStatus(200).setBytes(100).setUserAgent("UA").build();
        validator.process(r, emitter);
    } catch (const StageException& e) {
        caught2099 = (e.getErrorCode() == "TIMESTAMP_OUT_OF_RANGE");
    }
    ASSERT_TRUE(caught2099);
}

/**
 * @brief 50. Test (Görev 5): ParserStage UTF-8 Olmayan Veri ve 100 KB'lık Tek Satır Tespiti
 */
TEST_CASE(test_50_parser_stage_utf8_and_100kb_oversized_line) {
    ParserStage parser(true, true, 65536);
    CollectingEmitter<LogRecord> emitter;

    // 1. UTF-8 olmayan bayt dizisi (\xFF\xFE\xC0\xAF)
    std::string badUtf8 = "192.168.1.10 - - [29/Sep/2026:10:00:00 +0300] \"GET /bad-\xff\xfe\xc0\xaf HTTP/1.1\" 200 100";
    bool caughtNonUtf8 = false;
    try {
        parser.process(badUtf8, emitter);
    } catch (const StageException& e) {
        caughtNonUtf8 = (e.getErrorCode() == "NON_UTF8_BYTES");
    }
    ASSERT_TRUE(caughtNonUtf8);

    // 2. 100 KB'lık tek satır (102,400 bayt)
    std::string oversizedLine = "192.168.1.10 - - [29/Sep/2026:10:00:00 +0300] \"GET /" + std::string(102400, 'A') + " HTTP/1.1\" 200 102400";
    bool caughtOversized = false;
    try {
        parser.process(oversizedLine, emitter);
    } catch (const StageException& e) {
        caughtOversized = (e.getErrorCode() == "OVERSIZED_LINE");
    }
    ASSERT_TRUE(caughtOversized);
    ASSERT_EQ(emitter.size(), 0);
}

/**
 * @brief 51. Test (Görev 5): Kaos Testi — data/access-dirty.log Uçtan Uca İşleme ve Sayısal Mutabakat
 *
 * Gereksinim:
 * "Bilerek bozulmuş bir test dosyası sağlayın (data/access-dirty.log: kesilmiş satırlar,
 * UTF-8 olmayan veri(byte), negatif veri(byte) sayıları, 1970 ve 2099 tarihli zaman damgaları,
 * 100 KB'lık tek satır ve boş dosya). İşlem hattı tamamlanmalı, geçerli kayıtlar için doğru
 * çıktıyı üretmeli ve tüm hatalı kayıtları hesaba katmalıdır."
 */
TEST_CASE(test_51_chaos_test_dirty_log_end_to_end) {
    PipelineLoader loader;
    Pipeline<> p = loader.loadFromFile("configs/chaos-test.yaml");
    p.setPrintSummary(false);

    RunSummary summary = p.run();

    // 1. Özet sayılarının tam mutabakatı (Hocanın access-dirty.log fixture dosyası: 11 satır)
    // - 3 geçerli kayıt -> ana hedefe (out/clean-access.log)
    // - 5 kayıt 'parse' aşamasında reddedilir (1 yorum satırı, 2 kesilmiş satır, 1 UTF-8 olmayan bayt, 1 100 KiB satır)
    // - 3 kayıt 'validate' aşamasında reddedilir (1 negatif bayt -45, 1 yıl 1970, 1 yıl 2099)
    // Toplam Dead-Letter = 8 kayıt (out/dead-letter.log)
    ASSERT_EQ(summary.getRecordsIn(), 11);
    ASSERT_EQ(summary.getRecordsOut(), 3);
    ASSERT_EQ(summary.getDeadLetterCount(), 8);
    ASSERT_EQ(summary.getTotalDropped(), 8);
    ASSERT_EQ(summary.getDroppedAtStage("parse"), 5);
    ASSERT_EQ(summary.getDroppedAtStage("validate"), 3);
    ASSERT_EQ(summary.getDroppedAtStage("project"), 0);
    ASSERT_TRUE(summary.isAccountingConsistent());
    ASSERT_TRUE(summary.getElapsedMicroseconds() >= 0);

    // 2. Ana hedef (out/clean-access.log) ve Dead-Letter hedefi (out/dead-letter.log) dosya doğrulaması
    auto cleanLines = readFileLines("out/clean-access.log");
    auto dlLines    = readFileLines("out/dead-letter.log");

    ASSERT_EQ(cleanLines.size(), summary.getRecordsOut());
    ASSERT_EQ(dlLines.size(), summary.getDeadLetterCount());

    ASSERT_STR_EQ(cleanLines[0], "2026-10-15T10:15:32Z 192.0.2.10 GET /index.html 200 1234");
    ASSERT_STR_EQ(cleanLines[1], "2026-10-15T10:16:05Z 198.51.100.22 POST /api/login 401 287");
    ASSERT_STR_EQ(cleanLines[2], "2026-10-15T10:17:41Z 203.0.113.7 GET /images/logo.png 304 0");

    // Dead-letter çıktısında tüm hata kodlarının yer aldığını doğrula
    std::string allDlText;
    for (const auto& l : dlLines) allDlText += l + "\n";

    ASSERT_TRUE(allDlText.find("code=COMMENT_OR_HEADER_LINE") != std::string::npos);
    ASSERT_TRUE(allDlText.find("code=TRUNCATED_LINE") != std::string::npos);
    ASSERT_TRUE(allDlText.find("code=NON_UTF8_BYTES") != std::string::npos);
    ASSERT_TRUE(allDlText.find("code=OVERSIZED_LINE") != std::string::npos);
    ASSERT_TRUE(allDlText.find("code=NEGATIVE_BYTES") != std::string::npos);
    ASSERT_TRUE(allDlText.find("code=TIMESTAMP_OUT_OF_RANGE") != std::string::npos);
}

/**
 * @brief 52. Test (Görev 5): Kaos Testi — Boş Dosya (data/access-empty.log) Sınır Durumu
 */
TEST_CASE(test_52_chaos_test_empty_file_edge_case) {
    PipelineLoader loader;
    Pipeline<> p = loader.loadFromFile("configs/empty-file-test.yaml");
    p.setPrintSummary(false);

    RunSummary summary = p.run();

    ASSERT_EQ(summary.getRecordsIn(), 0);
    ASSERT_EQ(summary.getRecordsOut(), 0);
    ASSERT_EQ(summary.getDeadLetterCount(), 0);
    ASSERT_EQ(summary.getTotalDropped(), 0);
    ASSERT_TRUE(summary.isAccountingConsistent());

    auto cleanLines = readFileLines("out/empty-clean.log");
    auto dlLines    = readFileLines("out/empty-dead-letter.log");
    ASSERT_EQ(cleanLines.size(), 0);
    ASSERT_EQ(dlLines.size(), 0);
    std::remove("out/empty-clean.log");
    std::remove("out/empty-dead-letter.log");
}

// ============================================================================
// ANA TEST KOŞUCU (MAIN)
// ============================================================================

int main() {
    std::cout << "============================================================\n";
    std::cout << " LogFlow Artim 5 - Birim ve Entegrasyon Test Paketi\n";
    std::cout << "============================================================\n";

    // Artım 2 Mevcut Testleri (1-15)
    RUN_TEST(test_1_gecerli_satir);
    RUN_TEST(test_2_eksik_alan);
    RUN_TEST(test_3_hatali_zaman_damgasi);
    RUN_TEST(test_4_hatali_durum_kodu);
    RUN_TEST(test_5_bos_satir);
    RUN_TEST(test_6_fazladan_bosluk);
    RUN_TEST(test_7_bosluk_iceren_tirnakli_user_agent);
    RUN_TEST(test_8_sorgu_dizesi_iceren_satir);
    RUN_TEST(test_9_tire_boyut_alani);
    RUN_TEST(test_10_logrecord_immutability_and_builder);
    RUN_TEST(test_11_attributes_ocp_extension);
    RUN_TEST(test_12_combined_log_format_referer_ve_user_agent);
    RUN_TEST(test_13_protokolsüz_istek_ve_hata_sifirlama);
    RUN_TEST(test_14_hatali_bayt_degeri);
    RUN_TEST(test_15_collecting_emitter_metotlari);

    // Artım 3 Testleri (16-32)
    std::cout << "------------------------------------------------------------\n";
    std::cout << " Artim 3: Asama ve Bilesilebilirlik Testleri\n";
    std::cout << "------------------------------------------------------------\n";
    RUN_TEST(test_16_predicate_filter_status);
    RUN_TEST(test_17_predicate_filter_path_prefix);
    RUN_TEST(test_18_predicate_filter_timestamp_range);
    RUN_TEST(test_19_predicate_filter_custom_lambda);
    RUN_TEST(test_20_predicate_filter_kayit_butunlugu);

    RUN_TEST(test_21_projection_stage_fields_subset);
    RUN_TEST(test_22_projection_stage_data_minimization);
    RUN_TEST(test_23_projection_stage_to_formatted_string);
    RUN_TEST(test_24_projection_stage_constructors);

    RUN_TEST(test_25_filesink_write_and_close);
    RUN_TEST(test_26_filesink_lifecycle_unopened_throws);
    RUN_TEST(test_27_filesink_close_flushes_buffer);

    RUN_TEST(test_28_pipeline_builder_fluent_chain);
    RUN_TEST(test_29_pipeline_builder_lifecycle_invocations);

    RUN_TEST(test_30_integration_3_stage_pipeline_fixture);
    RUN_TEST(test_31_integration_composability_and_order_invariance);
    RUN_TEST(test_32_multiple_predicate_filters_chained);

    // Artım 4 Yeni Testleri (33-42)
    std::cout << "------------------------------------------------------------\n";
    std::cout << " Artim 4: Yapilandirma Yukleyici ve Kayit Defteri Testleri\n";
    std::cout << "------------------------------------------------------------\n";
    RUN_TEST(test_33_loader_valid_configuration);
    RUN_TEST(test_34_loader_unknown_stage_type);
    RUN_TEST(test_35_loader_missing_parameter);
    RUN_TEST(test_36_loader_wrong_parameter_type);
    RUN_TEST(test_37_loader_empty_pipeline);
    RUN_TEST(test_38_three_config_files_produce_different_outputs);
    RUN_TEST(test_39_stage_registry_dynamic_plugin_registration);
    RUN_TEST(test_40_loader_stage_stream_type_mismatch);
    RUN_TEST(test_41_loader_unknown_source_sink_and_extra_params);
    RUN_TEST(test_42_filter_factory_all_operators_and_fields);

    // Artım 5 Yeni Testleri (43-52)
    std::cout << "------------------------------------------------------------\n";
    std::cout << " Artim 5: Hata Yonetimi, Dogrulama, Dead-Letter ve Kaos Testleri\n";
    std::cout << "------------------------------------------------------------\n";
    RUN_TEST(test_43_error_model_stage_vs_pipeline_exception);
    RUN_TEST(test_44_error_policy_parsing_and_modes);
    RUN_TEST(test_45_policy_fail_fast_stops_pipeline);
    RUN_TEST(test_46_policy_skip_counts_and_continues);
    RUN_TEST(test_47_policy_dead_letter_routes_failed_records);
    RUN_TEST(test_48_policy_retry_n_recovers_or_falls_back);
    RUN_TEST(test_49_validation_stage_declarative_rules);
    RUN_TEST(test_50_parser_stage_utf8_and_100kb_oversized_line);
    RUN_TEST(test_51_chaos_test_dirty_log_end_to_end);
    RUN_TEST(test_52_chaos_test_empty_file_edge_case);

    std::cout << "============================================================\n";
    std::cout << "Test Sonuclari: " << g_passCount << " Basarili, " << g_failCount << " Basarisiz\n";
    std::cout << "============================================================\n";

    return (g_failCount == 0) ? 0 : 1;
}

