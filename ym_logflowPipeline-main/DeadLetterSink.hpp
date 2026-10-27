#pragma once

#include "Sink.hpp"
#include "FailedRecord.hpp"
#include "PipelineExceptions.hpp"
#include <fstream>
#include <iostream>
#include <vector>
#include <filesystem>

/**
 * @brief Dead-Letter Dosya Hedefi (DeadLetterFileSink).
 *
 * Artım 5 Görev 3:
 * DEAD_LETTER hata politikası tarafından yönlendirilen `FailedRecord`
 * nesnelerini ana boru hattından bağımsız ikincil bir çıktı dosyasına yazar.
 */
class DeadLetterFileSink : public Sink<FailedRecord> {
private:
    std::string filePath_;
    std::ofstream outFile_;
    size_t writtenCount_ = 0;

public:
    explicit DeadLetterFileSink(std::string path) : filePath_(std::move(path)) {}
    ~DeadLetterFileSink() override { close(); }

    void open() override {
        writtenCount_ = 0;
        auto p = std::filesystem::u8path(filePath_);
        if (p.has_parent_path()) {
            std::error_code ec;
            std::filesystem::create_directories(p.parent_path(), ec);
        }
        outFile_.open(filePath_.c_str());
        if (!outFile_.is_open()) {
            throw PipelineException(
                "Dead-letter hedef dosyasi acilamadi: " + filePath_, "DeadLetterFileSink");
        }
    }

    void consume(const FailedRecord& record) override {
        if (!outFile_.is_open()) {
            throw PipelineException(
                "DeadLetterFileSink acik degil: " + filePath_, "DeadLetterFileSink");
        }
        outFile_ << record.toFormattedString() << "\n";
        if (!outFile_.good()) {
            throw PipelineException(
                "Dead-letter dosyasina yazma hatasi (disk dolu veya G/C hatasi): " + filePath_,
                "DeadLetterFileSink");
        }
        writtenCount_++;
    }

    void close() override {
        if (outFile_.is_open()) {
            outFile_.flush();
            outFile_.close();
        }
    }

    const std::string& getFilePath() const noexcept { return filePath_; }
    size_t getWrittenCount() const noexcept { return writtenCount_; }
};

/**
 * @brief Dead-Letter Konsol Hedefi (DeadLetterConsoleSink).
 *
 * Başarısız kayıtları tanılama bağlamıyla birlikte standart çıktıya/hata akışına yazar.
 */
class DeadLetterConsoleSink : public Sink<FailedRecord> {
private:
    size_t writtenCount_ = 0;

public:
    DeadLetterConsoleSink() = default;
    ~DeadLetterConsoleSink() override = default;

    void open() override { writtenCount_ = 0; }

    void consume(const FailedRecord& record) override {
        std::cout << record.toFormattedString() << "\n";
        writtenCount_++;
    }

    void close() override {}

    size_t getWrittenCount() const noexcept { return writtenCount_; }
};

/**
 * @brief Dead-Letter Bellek İçi Test Hedefi (DeadLetterMemorySink).
 *
 * Birim ve entegrasyon testlerinde ikincil kanala yönlendirilen FailedRecord
 * nesnelerini dosya sistemine dokunmadan doğrulamak için kullanılır.
 */
class DeadLetterMemorySink : public Sink<FailedRecord> {
private:
    std::vector<FailedRecord> records_;

public:
    DeadLetterMemorySink() = default;
    ~DeadLetterMemorySink() override = default;

    void open() override { records_.clear(); }

    void consume(const FailedRecord& record) override {
        records_.push_back(record);
    }

    void close() override {}

    const std::vector<FailedRecord>& getRecords() const noexcept { return records_; }
    size_t size() const noexcept { return records_.size(); }
    bool empty() const noexcept { return records_.empty(); }
    const FailedRecord& operator[](size_t i) const { return records_[i]; }
    void clear() { records_.clear(); }
};
