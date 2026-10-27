#pragma once

#include "Stage.hpp"
#include "LogRecord.hpp"
#include "PipelineExceptions.hpp"
#include <string>
#include <regex>
#include <chrono>
#include <cctype>
#include <ctime>
#include <iostream>

/**
 * @brief Common Log Format (CLF) ve Combined Log Format için Ayrıştırıcı Aşama (ParserStage).
 *
 * Artım 5 Güncellemesi (Hata Yönetimi ve Yalıtımı):
 * - "Hatalı satırları sessizce atla" yaklaşımı, tasarlanmış bir hata stratejisiyle değiştirilmiştir.
 * - Boru hattı içinde çalışırken (`throwOnError_ = true`), bozuk satırlar (kesilmiş satırlar,
 *   UTF-8 olmayan baytlar, 100 KB'lık aşırı uzun satırlar, boş satırlar, bozuk sözdizimi)
 *   tanılama bağlamıyla birlikte `StageException` fırlatır.
 * - Hata yönlendirmesi (FAIL_FAST, SKIP, DEAD_LETTER, RETRY) aşamanın içinde değil,
 *   boru hattı topolojisinde (PipelineBuilder / PipelineWiringState) kararlaştırılır.
 * - Boru hattında `ValidationStage` bulunduğunda (`deferRangeChecks_ = true`), negatif bayt
 *   sayıları (-512), aralık dışı durum kodları ve 1970/2099 tarihli zaman damgaları yapısal
 *   olarak `LogRecord` nesnesine ayrıştırılır ve bildirimsel `ValidationStage` kuralları
 *   tarafından yakalanır.
 */
class ParserStage : public Stage<std::string, LogRecord> {
public:
    static constexpr size_t DEFAULT_MAX_LINE_BYTES = 65536; // 64 KB (100 KB'lık tek satır kaos testini yakalar)

private:
    size_t malformedCount = 0;
    bool throwOnError_ = false;
    bool deferRangeChecks_ = false;
    size_t maxLineBytes_ = DEFAULT_MAX_LINE_BYTES;
    std::regex logRegex;

    /**
     * @brief RFC 3629 standardına göre dizginin geçerli UTF-8 bayt dizisi olup olmadığını denetler.
     * Ayrıca gömülü null (\0) baytlarını da reddeder.
     */
    static bool isValidUtf8(const std::string& s) {
        const unsigned char* bytes = reinterpret_cast<const unsigned char*>(s.data());
        size_t len = s.size();
        size_t i = 0;
        while (i < len) {
            unsigned char b0 = bytes[i];
            if (b0 == 0x00) {
                return false; // Gömülü null byte
            } else if (b0 <= 0x7F) {
                i += 1;
            } else if (b0 >= 0xC2 && b0 <= 0xDF) {
                if (i + 1 >= len) return false;
                unsigned char b1 = bytes[i + 1];
                if ((b1 & 0xC0) != 0x80) return false;
                i += 2;
            } else if (b0 >= 0xE0 && b0 <= 0xEF) {
                if (i + 2 >= len) return false;
                unsigned char b1 = bytes[i + 1];
                unsigned char b2 = bytes[i + 2];
                if ((b1 & 0xC0) != 0x80 || (b2 & 0xC0) != 0x80) return false;
                if (b0 == 0xE0 && b1 < 0xA0) return false; // Overlong encoding
                if (b0 == 0xED && b1 >= 0xA0) return false; // UTF-16 surrogate
                i += 3;
            } else if (b0 >= 0xF0 && b0 <= 0xF4) {
                if (i + 3 >= len) return false;
                unsigned char b1 = bytes[i + 1];
                unsigned char b2 = bytes[i + 2];
                unsigned char b3 = bytes[i + 3];
                if ((b1 & 0xC0) != 0x80 || (b2 & 0xC0) != 0x80 || (b3 & 0xC0) != 0x80) return false;
                if (b0 == 0xF0 && b1 < 0x90) return false; // Overlong
                if (b0 == 0xF4 && b1 > 0x8F) return false; // > U+10FFFF
                i += 4;
            } else {
                return false;
            }
        }
        return true;
    }

    /**
     * @brief Gün, ay, yıl değerlerinden sivil takvime göre Unix gün sayısını hesaplar.
     */
    static time_t daysFromCivil(int y, int m, int d) {
        y -= (m <= 2);
        const int era = (y >= 0 ? y : y - 399) / 400;
        const unsigned yoe = static_cast<unsigned>(y - era * 400);
        const unsigned doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;
        const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
        return static_cast<time_t>(era) * 146097 + static_cast<time_t>(doe) - 719468;
    }

    /**
     * @brief CLF zaman damgasını (örn: "29/Sep/2026:10:00:00 +0300") UTC time_point'e çevirir.
     */
    static bool parseClfTimestamp(const std::string& tsStr, std::chrono::system_clock::time_point& outTp) {
        std::regex tsRegex(R"(^(\d{1,2})/([A-Za-z]{3})/(\d{4}):(\d{2}):(\d{2}):(\d{2})\s+([+-]\d{4})$)");
        std::smatch m;
        if (!std::regex_match(tsStr, m, tsRegex)) {
            return false;
        }

        int day = std::stoi(m[1].str());
        std::string monStr = m[2].str();
        int year = std::stoi(m[3].str());
        int hour = std::stoi(m[4].str());
        int min = std::stoi(m[5].str());
        int sec = std::stoi(m[6].str());
        std::string tz = m[7].str();

        if (day < 1 || day > 31 || hour < 0 || hour > 23 || min < 0 || min > 59 || sec < 0 || sec > 60) {
            return false;
        }

        const char* months[] = {
            "Jan", "Feb", "Mar", "Apr", "May", "Jun",
            "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
        };
        int month = -1;
        for (int i = 0; i < 12; ++i) {
            if (monStr == months[i]) {
                month = i + 1;
                break;
            }
        }
        if (month == -1) {
            return false;
        }

        time_t days = daysFromCivil(year, month, day);
        time_t localSecs = days * 86400 + hour * 3600 + min * 60 + sec;

        int tzSign = (tz[0] == '-') ? -1 : 1;
        int tzHours = std::stoi(tz.substr(1, 2));
        int tzMins = std::stoi(tz.substr(3, 2));
        int totalOffsetSec = tzSign * (tzHours * 3600 + tzMins * 60);

        time_t utcSecs = localSecs - totalOffsetSec;
        outTp = std::chrono::system_clock::from_time_t(utcSecs);
        return true;
    }

    void handleParseError(const std::string& input,
                          const std::string& errorCode,
                          const std::string& message) {
        malformedCount++;
        if (throwOnError_) {
            throw StageException("parse", input, errorCode, message);
        }
    }

public:
    explicit ParserStage(bool throwOnError = false,
                         bool deferRangeChecks = false,
                         size_t maxLineBytes = DEFAULT_MAX_LINE_BYTES)
        : throwOnError_(throwOnError),
          deferRangeChecks_(deferRangeChecks),
          maxLineBytes_(maxLineBytes),
          logRegex(R"DELIM(^\s*(\S+)\s+(\S+)\s+(\S+)\s+\[([^\]]+)\]\s+"([^"]*)"\s+(\S+)\s+(\S+)(?:\s+"([^"]*)")?(?:\s+"([^"]*)")?\s*$)DELIM") {}

    ~ParserStage() override = default;

    void setThrowOnError(bool enabled) noexcept { throwOnError_ = enabled; }
    bool isThrowOnError() const noexcept { return throwOnError_; }

    void setDeferRangeChecks(bool defer) noexcept { deferRangeChecks_ = defer; }
    bool isDeferRangeChecks() const noexcept { return deferRangeChecks_; }

    void setMaxLineBytes(size_t maxBytes) noexcept { maxLineBytes_ = maxBytes; }
    size_t getMaxLineBytes() const noexcept { return maxLineBytes_; }

    void open() override {}
    void close() override {}
    std::string stageTypeName() const override { return "parse"; }
    void onAttachedToPipeline() override { throwOnError_ = true; }

    /**
     * @brief Gelen ham metin satırını ayrıştırır.
     * Geçerliyse out.emit(LogRecord) çağrılır; hatalıysa StageException fırlatır (veya sayacı artırır).
     */
    void process(const std::string& input, Emitter<LogRecord>& out) override {
        // 1. Aşırı uzun satır denetimi (örn. 100 KB'lık tek satır kaos testi)
        // Regex çökmesini (stack overflow) önlemek için en başta kontrol edilir.
        if (input.size() > maxLineBytes_) {
            handleParseError(
                input,
                "OVERSIZED_LINE",
                "Satir boyutu izin verilen azami siniri (" + std::to_string(maxLineBytes_) +
                " bayt) asiyor: " + std::to_string(input.size()) + " bayt");
            return;
        }

        // 2. UTF-8 bayt geçerliliği denetimi
        if (!isValidUtf8(input)) {
            handleParseError(
                input,
                "NON_UTF8_BYTES",
                "Gecersiz UTF-8 bayt dizisi veya bozuk ikili (binary) veri tespit edildi");
            return;
        }

        // 3. Boş satır veya yalnızca boşluk içeren satır kontrolü
        size_t firstNonSpace = input.find_first_not_of(" \t\r\n");
        if (firstNonSpace == std::string::npos) {
            handleParseError(
                input,
                "EMPTY_LINE",
                "Bos veya yalnizca bosluk karakterlerinden olusan satir");
            return;
        }

        // 3b. Yorum / başlık satırı kontrolü ('#' ile başlayan satırlar)
        if (input[firstNonSpace] == '#') {
            handleParseError(
                input,
                "COMMENT_OR_HEADER_LINE",
                "Yorum veya baslik satiri (# ile baslayan kayit disi satir)");
            return;
        }

        // 4. Regex ile CLF/Combined şablonuna uygunluk kontrolü (Kesilmiş satırları yakalar)
        std::smatch match;
        if (!std::regex_match(input, match, logRegex)) {
            handleParseError(
                input,
                "TRUNCATED_LINE",
                "Kesilmis (truncated) veya eksik alanli CLF log satiri");
            return;
        }

        std::string ip = match[1].str();
        std::string timestampStr = match[4].str();
        std::string requestStr = match[5].str();
        std::string statusStr = match[6].str();
        std::string bytesStr = match[7].str();
        std::string opt1 = match[8].matched ? match[8].str() : "";
        std::string opt2 = match[9].matched ? match[9].str() : "";

        // 5. HTTP Durum Kodu Ayrıştırma
        int status = 0;
        try {
            size_t idx = 0;
            status = std::stoi(statusStr, &idx);
            if (idx != statusStr.length()) {
                handleParseError(input, "INVALID_STATUS", "HTTP durum kodu tamsayi degil: '" + statusStr + "'");
                return;
            }
            if (!deferRangeChecks_ && (status < 100 || status > 599)) {
                handleParseError(input, "STATUS_OUT_OF_RANGE", "HTTP durum kodu [100, 599] disinda: " + statusStr);
                return;
            }
        } catch (const StageException&) {
            throw;
        } catch (...) {
            handleParseError(input, "INVALID_STATUS", "HTTP durum kodu ayristirilamadi: '" + statusStr + "'");
            return;
        }

        // 6. Gönderilen Bayt Ayrıştırma ('-' ise 0 kabul edilir)
        long long bytes = 0;
        if (bytesStr == "-") {
            bytes = 0;
        } else {
            try {
                size_t idx = 0;
                bytes = std::stoll(bytesStr, &idx);
                if (idx != bytesStr.length()) {
                    handleParseError(input, "INVALID_BYTES", "Bayt alani tamsayi degil: '" + bytesStr + "'");
                    return;
                }
                if (!deferRangeChecks_ && bytes < 0) {
                    handleParseError(input, "NEGATIVE_BYTES", "Negatif bayt sayisi: " + bytesStr);
                    return;
                }
            } catch (const StageException&) {
                throw;
            } catch (...) {
                handleParseError(input, "INVALID_BYTES", "Bayt alani ayristirilamadi: '" + bytesStr + "'");
                return;
            }
        }

        // 7. Zaman Damgası Ayrıştırma
        std::chrono::system_clock::time_point timestamp;
        if (!parseClfTimestamp(timestampStr, timestamp)) {
            handleParseError(
                input,
                "INVALID_TIMESTAMP",
                "Gecersiz CLF zaman damgasi bicimi: '" + timestampStr + "'");
            return;
        }

        // 8. İstek Satırını Ayrıştırma (Method ve Path)
        size_t firstSpace = requestStr.find(' ');
        if (firstSpace == std::string::npos || firstSpace == 0) {
            handleParseError(
                input,
                "INVALID_REQUEST",
                "Istek satirinda HTTP metodu veya yol eksik: '" + requestStr + "'");
            return;
        }

        std::string method = requestStr.substr(0, firstSpace);
        std::string rest = requestStr.substr(firstSpace + 1);
        while (!rest.empty() && rest.front() == ' ') {
            rest.erase(0, 1);
        }

        std::string path;
        size_t lastSpace = rest.rfind(' ');
        if (lastSpace != std::string::npos && rest.substr(lastSpace + 1).rfind("HTTP/", 0) == 0) {
            path = rest.substr(0, lastSpace);
        } else {
            path = rest;
        }

        if (path.empty()) {
            handleParseError(
                input,
                "INVALID_REQUEST",
                "Istek satirinda hedef yol (path) bos: '" + requestStr + "'");
            return;
        }

        // 9. User-Agent ve İsteğe Bağlı Öznitelikler (Attributes)
        std::string userAgent = "";
        std::map<std::string, std::string> attributes;

        if (!opt2.empty()) {
            attributes["referer"] = opt1;
            userAgent = opt2;
        } else if (!opt1.empty()) {
            userAgent = opt1;
        }

        // 10. Alan Modeli Nesnesini Oluştur ve Sonraki Aşamaya Yay (Emit)
        LogRecord record(timestamp, ip, method, path, status, bytes, userAgent, attributes, input);
        out.emit(record);
    }

    size_t getErrorCount() const {
        return malformedCount;
    }

    void resetErrorCount() {
        malformedCount = 0;
    }
};
