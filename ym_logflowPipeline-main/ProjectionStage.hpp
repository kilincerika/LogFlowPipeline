#pragma once

#include "Stage.hpp"
#include "LogRecord.hpp"
#include <vector>
#include <string>

/**
 * @brief ProjectionStage — her kaydi secilen alanlarin bir alt kumesine indirger.
 */
class ProjectionStage : public Stage<LogRecord, LogRecord> {
private:
    std::vector<std::string> fields;

public:
    explicit ProjectionStage(std::vector<std::string> f) : fields(std::move(f)) {}
    ProjectionStage(std::initializer_list<std::string> f) : fields(f) {}

    template <typename... Args>
    ProjectionStage(const std::string& first, Args&&... rest)
        : fields{first, std::string(std::forward<Args>(rest))...} {}

    void process(const LogRecord& input, Emitter<LogRecord>& out) override {
        out.emit(LogRecord::Builder::project(input, fields));
    }

    std::string stageTypeName() const override { return "project"; }

    const std::vector<std::string>& getFields() const { return fields; }
};
