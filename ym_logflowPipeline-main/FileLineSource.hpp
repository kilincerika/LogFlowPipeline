#pragma once
#include "Source.hpp"
#include "PipelineExceptions.hpp"
#include <string>
#include <fstream>
#include <stdexcept>

class FileLineSource : public Source<std::string> {
private:
    std::string filePath;
public:
    explicit FileLineSource(std::string path) : filePath(std::move(path)) {}

    void produce(Emitter<std::string>& out) override {
        std::ifstream file(filePath.c_str(), std::ios::in | std::ios::binary);
        if (!file.is_open()) {
            // VS Code veya IDE'nin binary'yi output/ klasöründen çalıştırması durumunda üst dizini kontrol et
            file.open(("../" + filePath).c_str(), std::ios::in | std::ios::binary);
        }
        if (!file.is_open()) {
            throw PipelineException("Could not open file: " + filePath, "FileLineSource");
        }

        std::string line;
        while (std::getline(file, line)) {
            // CRLF (\r\n) satır sonu uyumluluğu: yalnızca satırda başka karakterler varsa sondaki \r'yi temizle
            if (!line.empty() && line.back() == '\r' && line.find_first_not_of(" \t\r\n") != std::string::npos) {
                line.pop_back();
            }
            out.emit(line);
        }
    }

    const std::string& getFilePath() const noexcept { return filePath; }
};
