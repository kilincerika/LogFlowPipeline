#pragma once

#include "ErrorPolicy.hpp"
#include <string>
#include <vector>
#include <map>
#include <chrono>
#include <iostream>
#include <sstream>
#include <iomanip>

/**
 * @brief Tek Bir Aşamanın Çalışma Zamanı Ölçüm Özeti (StageRunMetrics).
 */
struct StageRunMetrics {
    std::string stageName;
    ErrorPolicy policy;
    size_t recordsIn = 0;
    size_t recordsOut = 0;
    size_t droppedCount = 0;       // Bu aşamada ana akıştan ayrılan/atılan toplam kayıt (skipped + deadLettered + filtered)
    size_t skippedCount = 0;       // SKIP politikası ile atlanan kayıt sayısı
    size_t filteredCount = 0;      // Filtre koşulunu sağlamadığı için elenen kayıt sayısı
    size_t deadLetteredCount = 0;  // DEAD_LETTER politikası ile ikincil kanala yönlendirilen kayıt sayısı
    size_t retryCount = 0;         // RETRY(n) kapsamında yapılan yeniden deneme sayısı
};

/**
 * @brief İşlem Hattı Çalıştırma Özeti (RunSummary).
 *
 * Artım 5 Görev 4:
 * - Gelen kayıt sayısı (`recordsIn`)
 * - Çıkan kayıt sayısı (`recordsOut`)
 * - Her aşamada atılan kayıt sayısı (`droppedPerStage`)
 * - Dead-letter kanalına gönderilen kayıt sayısı (`deadLetterCount`)
 * - Gerçek geçen süre (`elapsedMs` / `elapsedUs` / `wallClockSeconds`)
 *
 * Her çalıştırmanın sonunda yazdırılır.
 * 8–9. haftalarda yapılacak başarım (throughput/latency) ölçümlerinin temelini
 * oluşturacak şekilde kronometre ve saniye başına kayıt (records/sec) metriklerini içerir.
 */
class RunSummary {
private:
    std::string pipelineName_ = "unnamed-pipeline";
    size_t recordsIn_ = 0;
    size_t recordsOut_ = 0;
    size_t totalDropped_ = 0;
    size_t totalSkipped_ = 0;
    size_t totalFiltered_ = 0;
    size_t deadLetterCount_ = 0;
    size_t totalRetries_ = 0;
    long long elapsedMicroseconds_ = 0;
    std::vector<StageRunMetrics> stageMetrics_;

public:
    RunSummary() = default;
    explicit RunSummary(std::string name) : pipelineName_(std::move(name)) {}

    void reset() {
        recordsIn_ = 0;
        recordsOut_ = 0;
        totalDropped_ = 0;
        totalSkipped_ = 0;
        totalFiltered_ = 0;
        deadLetterCount_ = 0;
        totalRetries_ = 0;
        elapsedMicroseconds_ = 0;
        for (auto& sm : stageMetrics_) {
            sm.recordsIn = 0;
            sm.recordsOut = 0;
            sm.droppedCount = 0;
            sm.skippedCount = 0;
            sm.filteredCount = 0;
            sm.deadLetteredCount = 0;
            sm.retryCount = 0;
        }
    }

    void setPipelineName(std::string name) { pipelineName_ = std::move(name); }
    const std::string& getPipelineName() const noexcept { return pipelineName_; }

    size_t registerStage(const std::string& stageName, const ErrorPolicy& policy) {
        size_t idx = stageMetrics_.size();
        StageRunMetrics m;
        m.stageName = stageName;
        m.policy = policy;
        stageMetrics_.push_back(std::move(m));
        return idx;
    }

    void incrementRecordsIn() noexcept { recordsIn_++; }
    void incrementRecordsOut() noexcept { recordsOut_++; }

    void recordStageInput(size_t stageIdx) noexcept {
        if (stageIdx < stageMetrics_.size()) {
            stageMetrics_[stageIdx].recordsIn++;
        }
    }

    void recordStageOutput(size_t stageIdx) noexcept {
        if (stageIdx < stageMetrics_.size()) {
            stageMetrics_[stageIdx].recordsOut++;
        }
    }

    void recordStageSkipped(size_t stageIdx) noexcept {
        if (stageIdx < stageMetrics_.size()) {
            stageMetrics_[stageIdx].droppedCount++;
            stageMetrics_[stageIdx].skippedCount++;
        }
        totalDropped_++;
        totalSkipped_++;
    }

    void recordStageFiltered(size_t stageIdx) noexcept {
        if (stageIdx < stageMetrics_.size()) {
            stageMetrics_[stageIdx].droppedCount++;
            stageMetrics_[stageIdx].filteredCount++;
        }
        totalDropped_++;
        totalFiltered_++;
    }

    void recordStageDeadLettered(size_t stageIdx) noexcept {
        if (stageIdx < stageMetrics_.size()) {
            stageMetrics_[stageIdx].droppedCount++;
            stageMetrics_[stageIdx].deadLetteredCount++;
        }
        totalDropped_++;
        deadLetterCount_++;
    }

    void recordStageRetry(size_t stageIdx) noexcept {
        if (stageIdx < stageMetrics_.size()) {
            stageMetrics_[stageIdx].retryCount++;
        }
        totalRetries_++;
    }

    void setElapsedMicroseconds(long long us) noexcept {
        elapsedMicroseconds_ = (us < 0) ? 0 : us;
    }

    // ========================================================================
    // ERİŞİMCİLER (GETTERS) — 8. ve 9. Hafta Ölçüm Arayüzü
    // ========================================================================

    size_t getRecordsIn() const noexcept { return recordsIn_; }
    size_t getRecordsOut() const noexcept { return recordsOut_; }
    size_t getTotalDropped() const noexcept { return totalDropped_; }
    size_t getTotalSkipped() const noexcept { return totalSkipped_; }
    size_t getTotalFiltered() const noexcept { return totalFiltered_; }
    size_t getDeadLetterCount() const noexcept { return deadLetterCount_; }
    size_t getTotalRetries() const noexcept { return totalRetries_; }

    // Kısa erişimciler
    size_t recordsIn() const noexcept { return recordsIn_; }
    size_t recordsOut() const noexcept { return recordsOut_; }
    size_t deadLetterCount() const noexcept { return deadLetterCount_; }
    size_t totalDropped() const noexcept { return totalDropped_; }

    long long getElapsedMicroseconds() const noexcept { return elapsedMicroseconds_; }
    double getElapsedMilliseconds() const noexcept { return static_cast<double>(elapsedMicroseconds_) / 1000.0; }
    double getElapsedSeconds() const noexcept { return static_cast<double>(elapsedMicroseconds_) / 1000000.0; }

    /**
     * @brief Saniyede işlenen kayıt hızı (Throughput - records/sec).
     * 8–9. haftalarda yapılacak performans analizleri için temel ölçüt.
     */
    double getThroughputRecordsPerSec() const noexcept {
        if (elapsedMicroseconds_ <= 0) return static_cast<double>(recordsIn_);
        return (static_cast<double>(recordsIn_) * 1000000.0) / static_cast<double>(elapsedMicroseconds_);
    }

    const std::vector<StageRunMetrics>& getStageMetrics() const noexcept { return stageMetrics_; }

    /**
     * @brief Aşama adına göre o aşamada atılan (dropped) toplam kayıt sayısını döndürür.
     */
    size_t getDroppedAtStage(const std::string& stageName) const {
        size_t sum = 0;
        for (const auto& sm : stageMetrics_) {
            if (sm.stageName == stageName) {
                sum += sm.droppedCount;
            }
        }
        return sum;
    }

    /**
     * @brief Aşama adına göre atılan kayıt sayılarını std::map olarak döndürür.
     */
    std::map<std::string, size_t> getDroppedPerStageMap() const {
        std::map<std::string, size_t> result;
        for (const auto& sm : stageMetrics_) {
            result[sm.stageName] += sm.droppedCount;
        }
        return result;
    }

    /**
     * @brief Muhasebe mutabakatı denetimi:
     * Gelen kayıt sayısı == Çıkan kayıt sayısı + Tüm aşamalarda atılan/dead-letter'a giden kayıt sayısı
     */
    bool isAccountingConsistent() const noexcept {
        size_t stageDropSum = 0;
        for (const auto& sm : stageMetrics_) {
            stageDropSum += sm.droppedCount;
        }
        return (recordsIn_ == recordsOut_ + totalDropped_) &&
               (totalDropped_ == stageDropSum) &&
               (totalDropped_ == totalSkipped_ + totalFiltered_ + deadLetterCount_);
    }

    std::string toFormattedString() const {
        std::ostringstream oss;
        oss << "----------------------------------------------------------------------\n";
        oss << " CALISTIRMA OZETI (RunSummary) - [" << pipelineName_ << "]\n";
        oss << "----------------------------------------------------------------------\n";
        oss << " Gelen Kayit Sayisi (recordsIn)        : " << recordsIn_ << "\n";
        oss << " Cikan Kayit Sayisi (recordsOut)       : " << recordsOut_ << "\n";
        oss << " Dead-Letter Kanalina Giden Kayit      : " << deadLetterCount_ << "\n";
        oss << " Atlanan / Elenen Kayit (SKIP/Filter)  : " << (totalSkipped_ + totalFiltered_)
            << " (SKIP: " << totalSkipped_ << ", Filter: " << totalFiltered_ << ")\n";
        if (totalRetries_ > 0) {
            oss << " Toplam Yeniden Deneme (RETRY)         : " << totalRetries_ << "\n";
        }
        oss << " Asama Bazinda Atilan Kayit Sayisi     :\n";
        for (size_t i = 0; i < stageMetrics_.size(); ++i) {
            const auto& sm = stageMetrics_[i];
            oss << "   - [" << (i + 1) << "] " << std::left << std::setw(10) << sm.stageName
                << " (Politika: " << std::setw(11) << sm.policy.toString() << ") : "
                << sm.droppedCount << " atildi"
                << " [DEAD_LETTER: " << sm.deadLetteredCount
                << ", SKIP: " << sm.skippedCount;
            if (sm.filteredCount > 0) {
                oss << ", FILTER: " << sm.filteredCount;
            }
            if (sm.retryCount > 0) {
                oss << ", RETRY: " << sm.retryCount;
            }
            oss << "]\n";
        }
        oss << " Sayisal Mutabakat (In == Out + Drop)  : "
            << (isAccountingConsistent() ? "DOGRULANDI (TAM TUTARLI)" : "TUTARSIZ!")
            << " [In(" << recordsIn_ << ") == Out(" << recordsOut_
            << ") + Skipped(" << (totalSkipped_ + totalFiltered_)
            << ") + DeadLetter(" << deadLetterCount_ << ")]\n";
        oss << std::fixed << std::setprecision(3)
            << " Gercek Gecen Sure (Wall-Clock Time)   : " << getElapsedMilliseconds() << " ms ("
            << elapsedMicroseconds_ << " us)\n";
        oss << std::fixed << std::setprecision(1)
            << " Isleme Hizi (Throughput - 8/9. Hafta) : " << getThroughputRecordsPerSec() << " kayit/sn\n";
        oss << "----------------------------------------------------------------------\n";
        return oss.str();
    }

    void print(std::ostream& os = std::cout) const {
        os << toFormattedString();
    }
};
