#ifndef FILTER_STAGES_HPP
#define FILTER_STAGES_HPP

#include <vector>
#include <sstream>
#include <functional>
#include <initializer_list>
#include "PipelineStage.hpp"

// 1. ParserStage: Ham satýrý LogRecord nesnesine çevirir
class ParserStage : public PipelineStage {
public:
    bool parse(const std::string& line, LogRecord& record) {
        std::stringstream ss(line);
        if (ss >> record.timestamp >> record.clientIp >> record.status >> record.path) {
            record.syncMap();
            return true;
        }
        return false;
    }

    bool process(LogRecord& record) override {
        return true;
    }
};

// 2. PredicateFilterStage: Yalnýzca koþulu saðlayan kayýtlarý iletir (< 40 satýr)
class PredicateFilterStage : public PipelineStage {
private:
    std::function<bool(const LogRecord&)> predicate;
public:
    explicit PredicateFilterStage(std::function<bool(const LogRecord&)> pred)
        : predicate(pred) {}

    bool process(LogRecord& record) override {
        return predicate(record);
    }
};

// 3. ProjectionStage: Belirli alanlarý seçer, diðerlerini atar (< 40 satýr)
class ProjectionStage : public PipelineStage {
private:
    std::vector<std::string> allowedFields;
public:
    ProjectionStage(std::initializer_list<std::string> fields) : allowedFields(fields) {}
    explicit ProjectionStage(const std::vector<std::string>& fields) : allowedFields(fields) {}

    bool process(LogRecord& record) override {
        std::map<std::string, std::string> projected;
        for (const auto& field : allowedFields) {
            auto it = record.fields.find(field);
            if (it != record.fields.end()) {
                projected[field] = it->second;
            }
        }
        record.fields = std::move(projected);
        return true;
    }
};

#endif // FILTER_STAGES_HPP
