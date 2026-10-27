#pragma once

#include "Stage.hpp"
#include "LogRecord.hpp"
#include <functional>
#include <string>

/**
 * @brief PredicateFilterStage — bir kaydi yalnizca Predicate<LogRecord> kosulu saglaniyorsa iletir.
 */
class PredicateFilterStage : public Stage<LogRecord, LogRecord> {
public:
    using Predicate = std::function<bool(const LogRecord&)>;

private:
    Predicate predicate;

public:
    explicit PredicateFilterStage(Predicate pred) : predicate(std::move(pred)) {}

    std::string stageTypeName() const override { return "filter"; }

    void process(const LogRecord& input, Emitter<LogRecord>& out) override {
        if (predicate && predicate(input)) {
            out.emit(input);
        }
    }

    static Predicate statusAtLeast(int minStatus) {
        return [minStatus](const LogRecord& r) { return r.getStatus() >= minStatus; };
    }

    static Predicate pathStartsWith(std::string prefix) {
        return [prefix](const LogRecord& r) { return r.getPath().rfind(prefix, 0) == 0; };
    }

    static Predicate timestampInRange(LogRecord::Timestamp start, LogRecord::Timestamp end) {
        return [start, end](const LogRecord& r) { return r.getTimestamp() >= start && r.getTimestamp() <= end; };
    }
};
