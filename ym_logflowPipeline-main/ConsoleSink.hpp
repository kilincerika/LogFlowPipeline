#pragma once

#include "Sink.hpp"
#include "LogRecord.hpp"
#include <iostream>

/**
 * @brief Konsol Hedefi (Console Sink).
 * 
 * Artım 2 Güncellemesi:
 * Önceki sürümde ham dizgileri (std::string) ekrana yazdırırken,
 * Artım 2'de artık doğrudan alan modelini (LogRecord) tüketir (`Sink<LogRecord>`).
 * 
 * Her bir LogRecord nesnesini okunabilir, temiz ve tek satırlık bir biçimde
 * standart çıktıya (stdout) yazar.
 */
class ConsoleSink : public Sink<LogRecord> {
public:
    ConsoleSink() = default;
    ~ConsoleSink() override = default;

    /**
     * @brief LogRecord nesnesini tek satırda biçimlendirip konsola yazdırır.
     */
    void consume(const LogRecord& record) override {
        std::cout << record.toFormattedString() << "\n";
    }
};
