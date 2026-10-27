#pragma once

#include "Stage.hpp"
#include "LogRecord.hpp"
#include "PipelineExceptions.hpp"
#include <string>
#include <vector>
#include <chrono>
#include <ctime>
#include <cctype>
#include <algorithm>
#include <sstream>

/**
 * @brief ValidationStage Bildirimsel Doğrulama Kuralları (ValidationRules).
 *
 * Artım 5 Görev 3:
 * Yapılandırma dosyasından (YAML) okunan bildirimsel kuralları temsil eder:
 * 1. Zorunlu alanlar (`requiredFields`: ör. clientIp, method, path, timestamp)
 * 2. Sayısal aralıklar (`minStatus`..`maxStatus`, `minBytes`..`maxBytes` — negatif baytları yakalar)
 * 3. Zaman damgası tutarlılığı (`minYear`..`maxYear` — 1970 ve 2099 tarihli kayıtları yakalar)
 * 4. IP biçimi (`validateIp`: IPv4 oktet doğrulaması 0-255.0-255.0-255.0-255)
 * 5. İzin verilen HTTP metotları (`allowedMethods`, isteğe bağlı)
 */
struct ValidationRules {
    std::vector<std::string> requiredFields = {"clientIp", "method", "path", "timestamp"};
    int minStatus = 100;
    int maxStatus = 599;
    long long minBytes = 0;
    long long maxBytes = 104857600LL; // 100 MB varsayılan üst sınır
    int minYear = 2000;
    int maxYear = 2035;
    bool validateIp = true;
    std::vector<std::string> allowedMethods = {};
};

/**
 * @brief Bildirimsel Doğrulama Aşaması (ValidationStage).
 *
 * Artım 5 Görev 3:
 * Yapısal olarak ayrıştırılmış `LogRecord` nesnelerini iş kurallarına ve
 * alan kısıtlamalarına göre doğrular. Geçersiz kayıtlar için `StageException`
 * fırlatarak kaydın ilgili aşama hata politikasına (`DEAD_LETTER`, `SKIP`,
 * `FAIL_FAST`, `RETRY(n)`) yönlendirilmesini sağlar.
 */
class ValidationStage : public Stage<LogRecord, LogRecord> {
private:
    ValidationRules rules_;

    /**
     * @brief IPv4 adres biçimini (A.B.C.D, her oktet 0..255) sıkı biçimde doğrular.
     */
    static bool isValidIpv4(const std::string& ip) {
        if (ip.empty()) return false;
        int dots = 0;
        size_t start = 0;
        for (size_t i = 0; i <= ip.size(); ++i) {
            if (i == ip.size() || ip[i] == '.') {
                if (i == start) return false; // boş oktet (ör. "192..1.1")
                std::string part = ip.substr(start, i - start);
                if (part.size() > 3) return false;
                for (char c : part) {
                    if (!std::isdigit(static_cast<unsigned char>(c))) return false;
                }
                if (part.size() > 1 && part[0] == '0') {
                    return false; // baştaki sıfır (leading zero) geçersiz
                }
                int val = std::stoi(part);
                if (val < 0 || val > 255) return false;
                if (i < ip.size()) dots++;
                start = i + 1;
            }
        }
        return dots == 3;
    }

    /**
     * @brief Zaman damgasının UTC yıl değerini döndürür.
     */
    static int extractUtcYear(LogRecord::Timestamp tp) {
        time_t t = std::chrono::system_clock::to_time_t(tp);
        std::tm gm{};
#if defined(_WIN32) || defined(_WIN64)
        gmtime_s(&gm, &t);
#else
        gmtime_r(&t, &gm);
#endif
        return gm.tm_year + 1900;
    }

    static bool isFieldPresent(const LogRecord& rec, const std::string& field) {
        if (field == "clientIp" || field == "ip") {
            return !rec.getClientIp().empty() && rec.getClientIp() != "-";
        }
        if (field == "method") {
            return !rec.getMethod().empty() && rec.getMethod() != "-";
        }
        if (field == "path") {
            return !rec.getPath().empty() && rec.getPath() != "-";
        }
        if (field == "userAgent") {
            return !rec.getUserAgent().empty() && rec.getUserAgent() != "-";
        }
        if (field == "timestamp") {
            return true; // Yıl tutarlılığı ayrıca kontrol edilir
        }
        if (field == "status") {
            return rec.getStatus() != 0;
        }
        if (field == "bytes") {
            return true;
        }
        if (field == "raw") {
            return !rec.getRaw().empty();
        }
        auto it = rec.getAttributes().find(field);
        return it != rec.getAttributes().end() && !it->second.empty() && it->second != "-";
    }

public:
    explicit ValidationStage(ValidationRules rules = ValidationRules{})
        : rules_(std::move(rules)) {}

    ~ValidationStage() override = default;

    std::string stageTypeName() const override { return "validate"; }
    const ValidationRules& getRules() const noexcept { return rules_; }

    void process(const LogRecord& input, Emitter<LogRecord>& out) override {
        const std::string& raw = input.getRaw();

        // 1. Zorunlu alanların varlık denetimi (Required Fields)
        for (const auto& reqField : rules_.requiredFields) {
            if (!isFieldPresent(input, reqField)) {
                throw StageException(
                    "validate",
                    raw,
                    "MISSING_REQUIRED_FIELD",
                    "Zorunlu alan eksik veya bos: '" + reqField + "'");
            }
        }

        // 2. IP biçimi doğrulaması (IP Format Validation)
        if (rules_.validateIp) {
            if (!isValidIpv4(input.getClientIp())) {
                throw StageException(
                    "validate",
                    raw,
                    "INVALID_IP_FORMAT",
                    "Gecersiz IPv4 adresi bicimi: '" + input.getClientIp() + "'");
            }
        }

        // 3. İzin verilen HTTP metotları denetimi (isteğe bağlı)
        if (!rules_.allowedMethods.empty()) {
            if (std::find(rules_.allowedMethods.begin(),
                          rules_.allowedMethods.end(),
                          input.getMethod()) == rules_.allowedMethods.end()) {
                throw StageException(
                    "validate",
                    raw,
                    "INVALID_HTTP_METHOD",
                    "Desteklenmeyen HTTP metodu: '" + input.getMethod() + "'");
            }
        }

        // 4. Sayısal aralık denetimi - HTTP durum kodu (Status Range)
        if (input.getStatus() < rules_.minStatus || input.getStatus() > rules_.maxStatus) {
            throw StageException(
                "validate",
                raw,
                "STATUS_OUT_OF_RANGE",
                "HTTP durum kodu gecerli aralik [" + std::to_string(rules_.minStatus) +
                ", " + std::to_string(rules_.maxStatus) + "] disinda: " +
                std::to_string(input.getStatus()));
        }

        // 5. Sayısal aralık denetimi - Bayt sayısı (Bytes Range / Negative Bytes)
        if (input.getBytes() < rules_.minBytes || input.getBytes() > rules_.maxBytes) {
            std::string code = (input.getBytes() < 0) ? "NEGATIVE_BYTES" : "BYTES_OUT_OF_RANGE";
            throw StageException(
                "validate",
                raw,
                code,
                "Bayt sayisi gecerli aralik [" + std::to_string(rules_.minBytes) +
                ", " + std::to_string(rules_.maxBytes) + "] disinda: " +
                std::to_string(input.getBytes()));
        }

        // 6. Zaman damgası tutarlılığı denetimi (Timestamp Consistency — ör. 1970 ve 2099 tespiti)
        int year = extractUtcYear(input.getTimestamp());
        if (year < rules_.minYear || year > rules_.maxYear) {
            throw StageException(
                "validate",
                raw,
                "TIMESTAMP_OUT_OF_RANGE",
                "Zaman damgasi yili (" + std::to_string(year) +
                ") gecerli tutarlilik araligi [" + std::to_string(rules_.minYear) +
                ", " + std::to_string(rules_.maxYear) + "] disinda: " +
                input.getTimestampIso());
        }

        // Tüm bildirimsel doğrulama kurallarından geçen kayıt sonraki aşamaya aktarılır
        out.emit(input);
    }
};
