#pragma once

#include "PipelineExceptions.hpp"
#include <string>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

/**
 * @brief Başarısız Kayıt Tanılama Modeli (FailedRecord).
 *
 * Artım 5 Görev 3:
 * Dead-letter kanalında taşınan, başarısız olmuş bir kaydın ham girdisini
 * (`rawInput`), hatanın oluştuğu aşama adını (`stageName`), yakalanan
 * istisnayı (`exception`) ve zaman damgasını (`timestamp`) barındıran
 * değişmez (immutable) veri yapısı.
 */
class FailedRecord {
public:
    using Timestamp = std::chrono::system_clock::time_point;

private:
    std::string rawInput_;
    std::string stageName_;
    StageException exception_;
    Timestamp timestamp_;

    /**
     * @brief UTF-8 olmayan baytları \xHH biçiminde kaçışlayarak ve aşırı uzun
     * satırları (ör. 100 KB) özetleyerek güvenli tek satırlık metin üretir.
     */
    static std::string sanitizeRawPreview(const std::string& raw, size_t maxPreviewLen = 160) {
        std::ostringstream oss;
        size_t limit = (raw.size() > maxPreviewLen) ? maxPreviewLen : raw.size();
        for (size_t i = 0; i < limit; ++i) {
            unsigned char c = static_cast<unsigned char>(raw[i]);
            if (c == '\r') {
                oss << "\\r";
            } else if (c == '\n') {
                oss << "\\n";
            } else if (c == '\t') {
                oss << "\\t";
            } else if (c >= 0x20 && c <= 0x7E) {
                oss << static_cast<char>(c);
            } else {
                oss << "\\x" << std::uppercase << std::hex << std::setw(2) << std::setfill('0')
                    << static_cast<int>(c) << std::nouppercase << std::dec;
            }
        }
        if (raw.size() > maxPreviewLen) {
            oss << "... [kesildi/truncated, toplam " << raw.size() << " bayt]";
        }
        return oss.str();
    }

public:
    FailedRecord(std::string rawInput,
                 std::string stageName,
                 StageException exception,
                 Timestamp timestamp = std::chrono::system_clock::now())
        : rawInput_(std::move(rawInput)),
          stageName_(std::move(stageName)),
          exception_(std::move(exception)),
          timestamp_(timestamp) {}

    FailedRecord(std::string rawInput,
                 std::string stageName,
                 const std::string& exceptionMessage,
                 Timestamp timestamp = std::chrono::system_clock::now())
        : rawInput_(rawInput),
          stageName_(stageName),
          exception_(stageName, rawInput, "STAGE_ERROR", exceptionMessage),
          timestamp_(timestamp) {}

    const std::string& getRawInput() const noexcept { return rawInput_; }
    const std::string& getStageName() const noexcept { return stageName_; }
    const StageException& getException() const noexcept { return exception_; }
    std::string getExceptionMessage() const { return exception_.what(); }
    const std::string& getErrorCode() const noexcept { return exception_.getErrorCode(); }
    Timestamp getTimestamp() const noexcept { return timestamp_; }

    // Kısa erişimciler (record stili)
    const std::string& rawInput() const noexcept { return rawInput_; }
    const std::string& stageName() const noexcept { return stageName_; }
    const StageException& exception() const noexcept { return exception_; }
    Timestamp timestamp() const noexcept { return timestamp_; }

    /**
     * @brief Zaman damgasını ISO-8601 UTC formatında döndürür.
     */
    std::string getTimestampIso() const {
        time_t t = std::chrono::system_clock::to_time_t(timestamp_);
        std::tm gm{};
#if defined(_WIN32) || defined(_WIN64)
        gmtime_s(&gm, &t);
#else
        gmtime_r(&t, &gm);
#endif
        std::ostringstream oss;
        oss << std::setfill('0')
            << std::setw(4) << (gm.tm_year + 1900) << "-"
            << std::setw(2) << (gm.tm_mon + 1) << "-"
            << std::setw(2) << gm.tm_mday << "T"
            << std::setw(2) << gm.tm_hour << ":"
            << std::setw(2) << gm.tm_min << ":"
            << std::setw(2) << gm.tm_sec << "Z";
        return oss.str();
    }

    /**
     * @brief Dead-letter hedefi için tanılama bağlamını içeren tek satırlık biçimlendirilmiş çıktı.
     */
    std::string toFormattedString() const {
        std::ostringstream oss;
        oss << "[" << getTimestampIso() << "] "
            << "[DEAD_LETTER] "
            << "stage=" << stageName_ << " | "
            << "code=" << exception_.getErrorCode() << " | "
            << "error=\"" << exception_.what() << "\" | "
            << "rawBytes=" << rawInput_.size() << " | "
            << "raw=\"" << sanitizeRawPreview(rawInput_) << "\"";
        return oss.str();
    }
};
