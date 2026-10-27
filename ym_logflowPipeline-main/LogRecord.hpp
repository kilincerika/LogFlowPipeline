#pragma once

#include <string>
#include <chrono>
#include <map>
#include <vector>
#include <iomanip>
#include <sstream>
#include <ctime>
#include <algorithm>

/**
 * @brief Alan Modeli (Domain Record) - Değişmez (Immutable) Log Kaydı.
 * 
 * Artım 2 Mimari Hedefi:
 * "İşlem hattı (pipeline) dizgi (string) taşır" anlayışından
 * "işlem hattı bir alan modeli (domain model) taşır" anlayışına geçiş.
 * 
 * Artım 3 Gelişimi:
 * - Projeksiyon desteği (ProjectionStage ile veri minimizasyonu ve gizlilik).
 * - Fluent lambdalar için modern erişimci takma adları (status(), path(), timestamp() vb.).
 * - Tek tip biçimlendirilmiş çıktı üretimi (toFormattedString()).
 * 
 * Sorumlulukların Ayrılması (Separation of Concerns):
 * LogRecord yalnızca ayrıştırılmış ve doğrulanmış log verisini temsil eder.
 * Dosya okuma veya ekrana yazdırma mantığından tamamen yalıtılmıştır.
 * 
 * Değişmezlik (Immutability):
 * Nesne oluşturulduktan sonra hiçbir alanı değiştirilemez (tüm alanlar const'tur).
 * 
 * Genişleme Noktası (Open-Closed Principle):
 * `attributes` eşlemi (std::map<std::string, std::string>), 5-7. haftaların
 * dayanacağı temel genişleme noktasıdır. Kayıt biçimi genişletmeye açık olmalıdır
 * (OCP), çünkü sonraki aşamalar bugünkü ayrıştırıcının (parser) hiçbir şey
 * bilmediği verileri (ör. GeoIP, kimlik bilgisi, zenginleştirilmiş metaveriler)
 * kayda ekleyecektir.
 */
class LogRecord {
public:
    using Timestamp = std::chrono::system_clock::time_point;

    // Builder sınıfı bildirimi
    class Builder;

    LogRecord(Timestamp timestamp,
              std::string clientIp,
              std::string method,
              std::string path,
              int status,
              long long bytes,
              std::string userAgent,
              std::map<std::string, std::string> attributes,
              std::string raw,
              std::vector<std::string> projectedFields = {})
        : timestamp_(timestamp),
          clientIp_(std::move(clientIp)),
          method_(std::move(method)),
          path_(std::move(path)),
          status_(status),
          bytes_(bytes),
          userAgent_(std::move(userAgent)),
          attributes_(std::move(attributes)),
          raw_(std::move(raw)),
          projectedFields_(std::move(projectedFields)) {}

    // Değişmezlik garantisi: Yalnızca const erişimciler (getters)
    Timestamp getTimestamp() const { return timestamp_; }
    const std::string& getClientIp() const { return clientIp_; }
    const std::string& getMethod() const { return method_; }
    const std::string& getPath() const { return path_; }
    int getStatus() const { return status_; }
    long long getBytes() const { return bytes_; }
    const std::string& getUserAgent() const { return userAgent_; }
    const std::map<std::string, std::string>& getAttributes() const { return attributes_; }
    const std::string& getRaw() const { return raw_; }

    // Artım 3: Fluent Lambda ve Predicate dostu kısa erişimciler (Java/C++ record stili)
    int status() const { return status_; }
    const std::string& path() const { return path_; }
    Timestamp timestamp() const { return timestamp_; }
    const std::string& clientIp() const { return clientIp_; }
    const std::string& method() const { return method_; }
    long long bytes() const { return bytes_; }
    const std::string& userAgent() const { return userAgent_; }
    const std::map<std::string, std::string>& attributes() const { return attributes_; }
    const std::string& raw() const { return raw_; }

    // Projeksiyon Durumu
    bool isProjected() const { return !projectedFields_.empty(); }
    const std::vector<std::string>& getProjectedFields() const { return projectedFields_; }

    /**
     * @brief Zaman damgasını ISO-8601 formatında (YYYY-MM-DDTHH:MM:SSZ) döndürür.
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
     * @brief Kaydı tek satırlık standart veya projeksiyonlu dizgiye dönüştürür.
     */
    std::string toFormattedString() const {
        if (!isProjected()) {
            std::ostringstream oss;
            oss << "[" << getTimestampIso() << "] "
                << clientIp_ << " "
                << "\"" << method_ << " " << path_ << "\" "
                << status_ << " "
                << bytes_ << "B";
            if (!userAgent_.empty() && userAgent_ != "-") {
                oss << " \"" << userAgent_ << "\"";
            }
            return oss.str();
        }

        std::ostringstream oss;
        for (size_t i = 0; i < projectedFields_.size(); ++i) {
            if (i > 0) oss << " ";
            const std::string& f = projectedFields_[i];
            if (f == "timestamp") {
                oss << getTimestampIso();
            } else if (f == "clientIp" || f == "ip") {
                oss << clientIp_;
            } else if (f == "method") {
                oss << method_;
            } else if (f == "path") {
                oss << path_;
            } else if (f == "status") {
                oss << status_;
            } else if (f == "bytes") {
                oss << bytes_;
            } else if (f == "userAgent") {
                oss << (userAgent_.empty() ? "-" : userAgent_);
            } else if (f == "raw") {
                oss << raw_;
            } else {
                auto it = attributes_.find(f);
                if (it != attributes_.end()) {
                    oss << it->second;
                } else {
                    oss << "-";
                }
            }
        }
        return oss.str();
    }

    /**
     * @brief OCP Gereği: Yeni bir öznitelik eklenmiş YENİ bir LogRecord kopyası üretir.
     * Mevcut nesnenin değişmezliğini bozmaz (Functional Immutability).
     */
    LogRecord withAttribute(const std::string& key, const std::string& value) const {
        auto newAttrs = attributes_;
        newAttrs[key] = value;
        return LogRecord(timestamp_, clientIp_, method_, path_, status_, bytes_, userAgent_, newAttrs, raw_, projectedFields_);
    }

private:
    const Timestamp timestamp_;
    const std::string clientIp_;
    const std::string method_;
    const std::string path_;
    const int status_;
    const long long bytes_;
    const std::string userAgent_;
    const std::map<std::string, std::string> attributes_;
    const std::string raw_;
    const std::vector<std::string> projectedFields_;
};

/**
 * @brief LogRecord için Builder Tasarım Deseni.
 * Esnek, okunabilir ve güvenli nesne inşası sağlar.
 */
class LogRecord::Builder {
private:
    LogRecord::Timestamp timestamp_{};
    std::string clientIp_;
    std::string method_;
    std::string path_;
    int status_ = 0;
    long long bytes_ = 0;
    std::string userAgent_;
    std::map<std::string, std::string> attributes_;
    std::string raw_;

public:
    Builder() = default;

    Builder& setTimestamp(LogRecord::Timestamp ts) {
        timestamp_ = ts;
        return *this;
    }

    Builder& setClientIp(std::string ip) {
        clientIp_ = std::move(ip);
        return *this;
    }

    Builder& setMethod(std::string method) {
        method_ = std::move(method);
        return *this;
    }

    Builder& setPath(std::string path) {
        path_ = std::move(path);
        return *this;
    }

    Builder& setStatus(int status) {
        status_ = status;
        return *this;
    }

    Builder& setBytes(long long bytes) {
        bytes_ = bytes;
        return *this;
    }

    Builder& setUserAgent(std::string ua) {
        userAgent_ = std::move(ua);
        return *this;
    }

    Builder& setAttributes(std::map<std::string, std::string> attrs) {
        attributes_ = std::move(attrs);
        return *this;
    }

    Builder& addAttribute(const std::string& key, const std::string& value) {
        attributes_[key] = value;
        return *this;
    }

    Builder& setRaw(std::string raw) {
        raw_ = std::move(raw);
        return *this;
    }

    Builder& setProjectedFields(std::vector<std::string> fields) {
        projectedFields_ = std::move(fields);
        return *this;
    }

    LogRecord build() const {
        return LogRecord(timestamp_, clientIp_, method_, path_, status_, bytes_, userAgent_, attributes_, raw_, projectedFields_);
    }

    /**
     * @brief Bir LogRecord nesnesini yalnizca belirtilen alanlarla filtreleyerek yeni bir kayit olusturur.
     */
    static LogRecord project(const LogRecord& rec, const std::vector<std::string>& fields) {
        Builder b;
        b.setProjectedFields(fields);
        for (const auto& f : fields) {
            if (f == "timestamp") b.setTimestamp(rec.getTimestamp());
            else if (f == "clientIp" || f == "ip") b.setClientIp(rec.getClientIp());
            else if (f == "method") b.setMethod(rec.getMethod());
            else if (f == "path") b.setPath(rec.getPath());
            else if (f == "status") b.setStatus(rec.getStatus());
            else if (f == "bytes") b.setBytes(rec.getBytes());
            else if (f == "userAgent") b.setUserAgent(rec.getUserAgent());
            else if (f == "raw") b.setRaw(rec.getRaw());
            else {
                auto it = rec.getAttributes().find(f);
                if (it != rec.getAttributes().end()) {
                    b.addAttribute(it->first, it->second);
                }
            }
        }
        return b.build();
    }
private:
    std::vector<std::string> projectedFields_;
};
