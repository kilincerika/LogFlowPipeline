#pragma once

#include "PipelineExceptions.hpp"
#include <string>
#include <algorithm>
#include <cctype>
#include <sstream>

/**
 * @brief Aşama Başına Ayarlanabilir Hata Politikası Türleri (ErrorPolicyType).
 *
 * Artım 5 Görev 2:
 * - FAIL_FAST   : İlk kayıt hatasında çalıştırmayı derhal durdurur (PipelineException fırlatır).
 * - SKIP        : Hatalı kaydı atlar ve aşama bazlı atlanan kayıt sayısını tutar.
 * - DEAD_LETTER : Başarısız kaydı tanılama bağlamıyla (FailedRecord) ikincil hedefe yönlendirir.
 * - RETRY       : Hatalı işlemi n kez yeniden dener (RETRY(n), 6. haftadaki I/O aşamaları için uygundur).
 */
enum class ErrorPolicyType {
    FAIL_FAST,
    SKIP,
    DEAD_LETTER,
    RETRY
};

class ErrorPolicy {
private:
    ErrorPolicyType type_ = ErrorPolicyType::SKIP;
    int maxRetries_ = 0;
    ErrorPolicyType retryFallback_ = ErrorPolicyType::DEAD_LETTER;

    static std::string trimAndUpper(const std::string& s) {
        size_t first = s.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) return "";
        size_t last = s.find_last_not_of(" \t\r\n");
        std::string sub = s.substr(first, last - first + 1);
        for (char& c : sub) {
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        }
        return sub;
    }

public:
    ErrorPolicy() = default;

    explicit ErrorPolicy(ErrorPolicyType type,
                         int maxRetries = 0,
                         ErrorPolicyType retryFallback = ErrorPolicyType::DEAD_LETTER)
        : type_(type), maxRetries_(maxRetries), retryFallback_(retryFallback) {}

    static ErrorPolicy failFast() {
        return ErrorPolicy(ErrorPolicyType::FAIL_FAST, 0);
    }

    static ErrorPolicy skip() {
        return ErrorPolicy(ErrorPolicyType::SKIP, 0);
    }

    static ErrorPolicy deadLetter() {
        return ErrorPolicy(ErrorPolicyType::DEAD_LETTER, 0);
    }

    static ErrorPolicy retry(int n, ErrorPolicyType fallback = ErrorPolicyType::DEAD_LETTER) {
        if (n < 0) {
            throw PipelineException("[Yapilandirma Hatasi] RETRY(n) politikasinda tekrar sayisi (n) negatif olamaz.", "ErrorPolicy");
        }
        return ErrorPolicy(ErrorPolicyType::RETRY, n, fallback);
    }

    ErrorPolicyType type() const noexcept { return type_; }
    int maxRetries() const noexcept { return maxRetries_; }
    ErrorPolicyType retryFallback() const noexcept { return retryFallback_; }

    bool isFailFast() const noexcept { return type_ == ErrorPolicyType::FAIL_FAST; }
    bool isSkip() const noexcept { return type_ == ErrorPolicyType::SKIP; }
    bool isDeadLetter() const noexcept { return type_ == ErrorPolicyType::DEAD_LETTER; }
    bool isRetry() const noexcept { return type_ == ErrorPolicyType::RETRY; }

    std::string toString() const {
        switch (type_) {
            case ErrorPolicyType::FAIL_FAST:   return "FAIL_FAST";
            case ErrorPolicyType::SKIP:        return "SKIP";
            case ErrorPolicyType::DEAD_LETTER: return "DEAD_LETTER";
            case ErrorPolicyType::RETRY:
                return "RETRY(" + std::to_string(maxRetries_) + ")";
        }
        return "SKIP";
    }

    /**
     * @brief Yapılandırma dosyasındaki metin ifadesinden ("FAIL_FAST", "SKIP",
     * "DEAD_LETTER", "RETRY(3)") ErrorPolicy nesnesi üretir.
     */
    static ErrorPolicy parse(const std::string& rawPolicy, const std::string& context = "stage") {
        std::string up = trimAndUpper(rawPolicy);
        if (up == "FAIL_FAST" || up == "FAILFAST" || up == "FAIL-FAST") {
            return failFast();
        }
        if (up == "SKIP") {
            return skip();
        }
        if (up == "DEAD_LETTER" || up == "DEADLETTER" || up == "DEAD-LETTER") {
            return deadLetter();
        }

        // RETRY(n) veya RETRY:n biçimi
        if (up.rfind("RETRY", 0) == 0) {
            std::string rest = up.substr(5);
            size_t f = rest.find_first_not_of(" \t");
            rest = (f == std::string::npos) ? "" : rest.substr(f);

            if (rest.empty()) {
                return retry(3);
            }
            if (rest.front() == '(' && rest.back() == ')') {
                std::string numStr = rest.substr(1, rest.size() - 2);
                try {
                    size_t idx = 0;
                    int n = std::stoi(numStr, &idx);
                    if (idx == numStr.size() && n >= 0) {
                        return retry(n);
                    }
                } catch (...) {}
            }
            throw PipelineException(
                "[Yapilandirma Hatasi] '" + context + "' icinde gecersiz RETRY politikasi sozdizimi: '" +
                rawPolicy + "'. Ornek kullanim: RETRY(3).", context);
        }

        throw PipelineException(
            "[Yapilandirma Hatasi] '" + context + "' icinde bilinmeyen hata politikasi: '" + rawPolicy +
            "'. Gecerli politikalar: [FAIL_FAST, SKIP, DEAD_LETTER, RETRY(n)].", context);
    }
};
