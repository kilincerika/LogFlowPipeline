#ifndef PIPELINE_STAGE_HPP
#define PIPELINE_STAGE_HPP

#include <string>
#include <fstream>
#include "LogRecord.hpp"

// Tüm ara aþamalar için temel sýnýf
class PipelineStage {
public:
    virtual ~PipelineStage() = default;
    virtual void open() {}
    // true dönerse kayýt sonraki aþamaya geçer, false ise elenir (drop)
    virtual bool process(LogRecord& record) = 0;
    virtual void close() {}
};

// Satýr satýr okuyan veri kaynaðý
class FileLineSource {
private:
    std::string filename;
    std::ifstream file;
public:
    explicit FileLineSource(const std::string& fn) : filename(fn) {}

    void open() {
        file.open(filename);
    }

    bool nextLine(std::string& line) {
        return static_cast<bool>(std::getline(file, line));
    }

    void close() {
        if (file.is_open()) {
            file.close();
        }
    }
};

#endif // PIPELINE_STAGE_HPP
