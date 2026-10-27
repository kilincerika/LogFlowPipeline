#pragma once

#include "Sink.hpp"
#include "LogRecord.hpp"
#include "PipelineExceptions.hpp"
#include <fstream>
#include <stdexcept>
#include <filesystem>

/**
 * @brief FileSink — cikti dosyasina yazar; open()/close() yasam dongusunu uygular.
 * Dosya acilamazsa veya yazma sirasinda disk/G-C hatasi olusursa PipelineException firlatir.
 */
class FileSink : public Sink<LogRecord> {
private:
    std::string filePath;
    std::ofstream outFile;

public:
    explicit FileSink(std::string path) : filePath(std::move(path)) {}
    ~FileSink() override { close(); }

    void open() override {
        auto p = std::filesystem::u8path(filePath);
        if (p.has_parent_path()) {
            std::error_code ec;
            std::filesystem::create_directories(p.parent_path(), ec);
        }
        outFile.open(filePath.c_str());
        if (!outFile.is_open()) {
            throw PipelineException("Could not open output file: " + filePath, "FileSink");
        }
    }

    void consume(const LogRecord& record) override {
        if (!outFile.is_open()) {
            throw PipelineException("FileSink is not open: " + filePath, "FileSink");
        }
        outFile << record.toFormattedString() << "\n";
        if (!outFile.good()) {
            throw PipelineException("Failed to write to output file (disk full or I/O error): " + filePath, "FileSink");
        }
    }

    void close() override {
        if (outFile.is_open()) {
            outFile.flush();
            outFile.close();
        }
    }

    const std::string& getFilePath() const noexcept { return filePath; }
};
