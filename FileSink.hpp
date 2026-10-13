#ifndef FILE_SINK_HPP
#define FILE_SINK_HPP

#include <string>
#include <fstream>
#include "LogRecord.hpp"

class FileSink {
private:
    std::string filename;
    std::ofstream out;
public:
    explicit FileSink(const std::string& fn) : filename(fn) {}

    void open() {
        out.open(filename);
    }

    void write(const LogRecord& record) {
        if (!out.is_open()) return;
        bool first = true;
        for (const auto& pair : record.fields) {
            if (!first) out << " | ";
            out << pair.first << ":" << pair.second;
            first = false;
        }
        out << "\n";
    }

    void close() {
        if (out.is_open()) {
            out.flush(); // Tampondaki kalan veriyi diske zorla yazar
            out.close(); // Dosyayý güvenli bir þekilde kapatýr
        }
    }
};

#endif // FILE_SINK_HPP
