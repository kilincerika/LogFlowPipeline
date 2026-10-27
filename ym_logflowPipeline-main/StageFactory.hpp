#pragma once

#include "Source.hpp"
#include "Stage.hpp"
#include "Sink.hpp"
#include "LogRecord.hpp"
#include "PipelineExceptions.hpp"
#include "FailedRecord.hpp"
#include "ErrorPolicy.hpp"
#include "FileLineSource.hpp"
#include "ParserStage.hpp"
#include "ValidationStage.hpp"
#include "PredicateFilterStage.hpp"
#include "ProjectionStage.hpp"
#include "FileSink.hpp"
#include "ConsoleSink.hpp"
#include "DeadLetterSink.hpp"
#include "PipelineBuilder.hpp"
#include "Pipeline.hpp"

#include <string>
#include <vector>
#include <map>
#include <functional>
#include <stdexcept>
#include <sstream>
#include <algorithm>
#include <memory>
#include <utility>

/**
 * @brief Yapılandırma Zamanı Hatası (Configuration-Time Exception).
 *
 * Artım 5 Görev 1:
 * Yapılandırma hataları işlem hattının devam edemeyeceği kurtarılamaz hatalar
 * olduğundan doğrudan `PipelineException` sınıfından türetilir.
 */
class ConfigValidationException : public PipelineException {
public:
    explicit ConfigValidationException(const std::string& message)
        : PipelineException("[Yapilandirma Hatasi] " + message, "PipelineLoader") {}
};

/**
 * @brief Tür Güvenli Yapılandırma Değeri (Typed Configuration Value).
 */
class ConfigValue {
public:
    enum class Type {
        NULL_VAL,
        STRING,
        INT,
        BOOL,
        LIST,
        MAP
    };

private:
    Type type_ = Type::NULL_VAL;
    std::string strVal_;
    long long intVal_ = 0;
    bool boolVal_ = false;
    std::vector<ConfigValue> listVal_;
    std::map<std::string, ConfigValue> mapVal_;

public:
    ConfigValue() = default;

    static ConfigValue fromString(std::string val) {
        ConfigValue cv;
        cv.type_ = Type::STRING;
        cv.strVal_ = std::move(val);
        return cv;
    }

    static ConfigValue fromInt(long long val) {
        ConfigValue cv;
        cv.type_ = Type::INT;
        cv.intVal_ = val;
        cv.strVal_ = std::to_string(val);
        return cv;
    }

    static ConfigValue fromBool(bool val) {
        ConfigValue cv;
        cv.type_ = Type::BOOL;
        cv.boolVal_ = val;
        cv.strVal_ = val ? "true" : "false";
        return cv;
    }

    static ConfigValue fromList(std::vector<ConfigValue> list) {
        ConfigValue cv;
        cv.type_ = Type::LIST;
        cv.listVal_ = std::move(list);
        return cv;
    }

    static ConfigValue fromMap(std::map<std::string, ConfigValue> map) {
        ConfigValue cv;
        cv.type_ = Type::MAP;
        cv.mapVal_ = std::move(map);
        return cv;
    }

    Type type() const { return type_; }
    bool isNull() const { return type_ == Type::NULL_VAL; }
    bool isString() const { return type_ == Type::STRING; }
    bool isInt() const { return type_ == Type::INT; }
    bool isBool() const { return type_ == Type::BOOL; }
    bool isList() const { return type_ == Type::LIST; }
    bool isMap() const { return type_ == Type::MAP; }

    std::string typeName() const {
        switch (type_) {
            case Type::NULL_VAL: return "null (bos)";
            case Type::STRING:   return "string (metin)";
            case Type::INT:      return "integer (tamsayi)";
            case Type::BOOL:     return "boolean (mantiksal)";
            case Type::LIST:     return "list (dizi/liste)";
            case Type::MAP:      return "map (nesne/sozluk)";
        }
        return "unknown";
    }

    const std::string& asString() const { return strVal_; }
    long long asInt() const { return intVal_; }
    bool asBool() const { return boolVal_; }
    const std::vector<ConfigValue>& asList() const { return listVal_; }
    const std::map<std::string, ConfigValue>& asMap() const { return mapVal_; }
};

/**
 * @brief Aşama Parametreleri ve Tür Doğrulayıcı (StageParams).
 */
class StageParams {
private:
    std::map<std::string, ConfigValue> params_;

public:
    StageParams() = default;
    explicit StageParams(std::map<std::string, ConfigValue> params)
        : params_(std::move(params)) {}

    bool has(const std::string& key) const {
        auto it = params_.find(key);
        return it != params_.end() && !it->second.isNull();
    }

    const ConfigValue& get(const std::string& key) const {
        static const ConfigValue nullVal;
        auto it = params_.find(key);
        return (it != params_.end()) ? it->second : nullVal;
    }

    void set(const std::string& key, const ConfigValue& val) {
        params_[key] = val;
    }

    void erase(const std::string& key) {
        params_.erase(key);
    }

    const std::map<std::string, ConfigValue>& raw() const { return params_; }

    std::string requireString(const std::string& key, const std::string& componentContext) const {
        auto it = params_.find(key);
        if (it == params_.end() || it->second.isNull()) {
            throw ConfigValidationException(
                "Eksik parametre (Missing parameter): '" + componentContext +
                "' icinde zorunlu '" + key + "' parametresi bulunamadi. "
                "Cozum: params bloguna '" + key + ": <metin>' ekleyin.");
        }
        if (!it->second.isString()) {
            throw ConfigValidationException(
                "Yanlis turde parametre (Wrong parameter type): '" + componentContext +
                "' icindeki '" + key + "' parametresi 'string (metin)' turunde olmalidir, "
                "ancak '" + it->second.typeName() + "' verildi.");
        }
        if (it->second.asString().empty()) {
            throw ConfigValidationException(
                "Gecersiz parametre degeri: '" + componentContext +
                "' icindeki '" + key + "' parametresi bos birakilamaz.");
        }
        return it->second.asString();
    }

    long long requireInt(const std::string& key, const std::string& componentContext) const {
        auto it = params_.find(key);
        if (it == params_.end() || it->second.isNull()) {
            throw ConfigValidationException(
                "Eksik parametre (Missing parameter): '" + componentContext +
                "' icinde zorunlu '" + key + "' parametresi bulunamadi. "
                "Cozum: params bloguna '" + key + ": <tamsayi>' ekleyin.");
        }
        if (!it->second.isInt()) {
            throw ConfigValidationException(
                "Yanlis turde parametre (Wrong parameter type): '" + componentContext +
                "' icindeki '" + key + "' parametresi 'integer (tamsayi)' turunde olmalidir, "
                "ancak '" + it->second.typeName() + "' ('" + it->second.asString() + "') verildi.");
        }
        return it->second.asInt();
    }

    long long optionalInt(const std::string& key, long long defaultVal, const std::string& componentContext) const {
        if (!has(key)) return defaultVal;
        return requireInt(key, componentContext);
    }

    bool optionalBool(const std::string& key, bool defaultVal, const std::string& componentContext) const {
        auto it = params_.find(key);
        if (it == params_.end() || it->second.isNull()) return defaultVal;
        if (!it->second.isBool()) {
            throw ConfigValidationException(
                "Yanlis turde parametre (Wrong parameter type): '" + componentContext +
                "' icindeki '" + key + "' parametresi 'boolean (true/false)' turunde olmalidir, "
                "ancak '" + it->second.typeName() + "' verildi.");
        }
        return it->second.asBool();
    }

    std::pair<long long, long long> optionalIntRange(const std::string& key,
                                                     long long defaultMin,
                                                     long long defaultMax,
                                                     const std::string& componentContext) const {
        auto it = params_.find(key);
        if (it == params_.end() || it->second.isNull()) {
            return {defaultMin, defaultMax};
        }
        if (!it->second.isList() || it->second.asList().size() != 2 ||
            !it->second.asList()[0].isInt() || !it->second.asList()[1].isInt()) {
            throw ConfigValidationException(
                "Yanlis turde parametre (Wrong parameter type): '" + componentContext +
                "' icindeki '" + key + "' parametresi 2 tamsayidan olusan bir aralik listesi '[min, max]' olmalidir.");
        }
        long long lo = it->second.asList()[0].asInt();
        long long hi = it->second.asList()[1].asInt();
        if (lo > hi) {
            throw ConfigValidationException(
                "Gecersiz aralik degeri: '" + componentContext + "' icindeki '" + key +
                "' araliginda alt sinir (" + std::to_string(lo) + ") ust sinirdan (" + std::to_string(hi) + ") buyuk olamaz.");
        }
        return {lo, hi};
    }

    std::vector<std::string> requireStringList(const std::string& key, const std::string& componentContext) const {
        auto it = params_.find(key);
        if (it == params_.end() || it->second.isNull()) {
            throw ConfigValidationException(
                "Eksik parametre (Missing parameter): '" + componentContext +
                "' icinde zorunlu '" + key + "' parametresi bulunamadi. "
                "Cozum: params bloguna '" + key + ": [alan1, alan2, ...]' ekleyin.");
        }
        if (!it->second.isList()) {
            throw ConfigValidationException(
                "Yanlis turde parametre (Wrong parameter type): '" + componentContext +
                "' icindeki '" + key + "' parametresi 'list (dizi/liste)' turunde olmalidir, "
                "ancak '" + it->second.typeName() + "' verildi.");
        }
        const auto& list = it->second.asList();
        if (list.empty()) {
            throw ConfigValidationException(
                "Gecersiz parametre degeri: '" + componentContext +
                "' icindeki '" + key + "' listesi bos olamaz; en az bir eleman icermelidir.");
        }
        std::vector<std::string> result;
        result.reserve(list.size());
        for (size_t i = 0; i < list.size(); ++i) {
            if (!list[i].isString() || list[i].asString().empty()) {
                throw ConfigValidationException(
                    "Yanlis turde parametre (Wrong parameter type): '" + componentContext +
                    "' icindeki '" + key + "' listesinin " + std::to_string(i + 1) +
                    ". elemani 'string (metin)' turunde olmalidir, ancak '" + list[i].typeName() + "' verildi.");
            }
            result.push_back(list[i].asString());
        }
        return result;
    }

    std::vector<std::string> optionalStringList(const std::string& key,
                                                const std::vector<std::string>& defaultList,
                                                const std::string& componentContext) const {
        if (!has(key)) return defaultList;
        return requireStringList(key, componentContext);
    }

    void rejectUnknownParams(const std::vector<std::string>& allowedKeys, const std::string& componentContext) const {
        for (const auto& kv : params_) {
            if (std::find(allowedKeys.begin(), allowedKeys.end(), kv.first) == allowedKeys.end()) {
                std::ostringstream oss;
                for (size_t i = 0; i < allowedKeys.size(); ++i) {
                    if (i > 0) oss << ", ";
                    oss << allowedKeys[i];
                }
                throw ConfigValidationException(
                    "Bilinmeyen parametre: '" + componentContext + "' icinde tanimsiz '" + kv.first +
                    "' parametresi kullanildi. Gecerli parametreler: [" + oss.str() + "].");
            }
        }
    }
};

/**
 * @brief Boru Hattı Bağlama Durumu (PipelineWiringState).
 */
class PipelineWiringState {
private:
    bool hasStringStep_ = false;
    bool hasRecordStep_ = false;
    PipelineBuilderStep<std::string> stringStep_;
    PipelineBuilderStep<LogRecord> recordStep_;

public:
    explicit PipelineWiringState(Source<std::string>* src)
        : hasStringStep_(true), hasRecordStep_(false), stringStep_(PipelineBuilder::from(src)) {}

    bool isStringStream() const { return hasStringStep_; }
    bool isRecordStream() const { return hasRecordStep_; }

    std::string currentStreamTypeName() const {
        if (hasRecordStep_) return "LogRecord";
        if (hasStringStep_) return "string";
        return "none";
    }

    void attachDeadLetterSink(Sink<FailedRecord>* dlSink, bool own = true) {
        if (hasRecordStep_) {
            recordStep_.withDeadLetterSink(dlSink, own);
        } else if (hasStringStep_) {
            stringStep_.withDeadLetterSink(dlSink, own);
        }
    }

    void attachStringToRecordStage(Stage<std::string, LogRecord>* stage,
                                   const std::string& stageName,
                                   const ErrorPolicy& policy = ErrorPolicy::skip()) {
        if (!hasStringStep_) {
            delete stage;
            throw ConfigValidationException(
                "Asama siralama/tur uyumsuzlugu: '" + stageName +
                "' asamasi girdi olarak 'string' bekliyor, ancak mevcut akis turu '" +
                currentStreamTypeName() + "' (Ayristirici asama yalnizca kaynagin hemen ardindan gelebilir).");
        }
        recordStep_ = stringStep_.via(stage, policy, stageName, true);
        hasStringStep_ = false;
        hasRecordStep_ = true;
    }

    void attachRecordToRecordStage(Stage<LogRecord, LogRecord>* stage,
                                   const std::string& stageName,
                                   const ErrorPolicy& policy = ErrorPolicy::skip()) {
        if (!hasRecordStep_) {
            delete stage;
            throw ConfigValidationException(
                "Asama siralama/tur uyumsuzlugu: '" + stageName +
                "' asamasi girdi olarak 'LogRecord' bekliyor, ancak mevcut akis turu '" +
                currentStreamTypeName() + "'. Cozum: '" + stageName +
                "' asamasindan once 'parse' asamasini ekleyin.");
        }
        recordStep_ = recordStep_.via(stage, policy, stageName, true);
    }

    Pipeline<void, void> finalizeWithSink(Sink<LogRecord>* sink, const std::string& pipelineName) {
        if (!hasRecordStep_) {
            delete sink;
            throw ConfigValidationException(
                "Boru hatti sonlandirma hatasi: Hedef (sink) 'LogRecord' bekliyor, ancak boru hatti akisi hala '" +
                currentStreamTypeName() + "' turunde. Cozum: Boru hattina en az bir 'parse' asamasi ekleyin.");
        }
        Pipeline<void, void> p = recordStep_.to(sink);
        p.setName(pipelineName);
        return p;
    }
};

/**
 * @brief Aşama Fabrikası (StageFactory).
 */
class StageFactory {
public:
    using SourceCreator         = std::function<Source<std::string>*(const StageParams&)>;
    using ParserStageCreator    = std::function<Stage<std::string, LogRecord>*(const StageParams&)>;
    using RecordStageCreator    = std::function<Stage<LogRecord, LogRecord>*(const StageParams&)>;
    using SinkCreator           = std::function<Sink<LogRecord>*(const StageParams&)>;
    using DeadLetterSinkCreator = std::function<Sink<FailedRecord>*(const StageParams&)>;
    using WiringStepFn          = std::function<void(PipelineWiringState&, const StageParams&, const ErrorPolicy&)>;

private:
    std::string typeName_;
    std::string inputType_;
    std::string outputType_;
    WiringStepFn wiringFn_;
    RecordStageCreator recordCreator_;
    ParserStageCreator parserCreator_;

public:
    StageFactory() = default;

    static StageFactory forParserStage(const std::string& typeName, ParserStageCreator creator) {
        StageFactory f;
        f.typeName_ = typeName;
        f.inputType_ = "string";
        f.outputType_ = "LogRecord";
        f.parserCreator_ = creator;
        f.wiringFn_ = [typeName, creator](PipelineWiringState& state, const StageParams& params, const ErrorPolicy& policy) {
            Stage<std::string, LogRecord>* stage = creator(params);
            state.attachStringToRecordStage(stage, typeName, policy);
        };
        return f;
    }

    static StageFactory forRecordStage(const std::string& typeName, RecordStageCreator creator) {
        StageFactory f;
        f.typeName_ = typeName;
        f.inputType_ = "LogRecord";
        f.outputType_ = "LogRecord";
        f.recordCreator_ = creator;
        f.wiringFn_ = [typeName, creator](PipelineWiringState& state, const StageParams& params, const ErrorPolicy& policy) {
            Stage<LogRecord, LogRecord>* stage = creator(params);
            state.attachRecordToRecordStage(stage, typeName, policy);
        };
        return f;
    }

    const std::string& typeName() const { return typeName_; }
    const std::string& inputType() const { return inputType_; }
    const std::string& outputType() const { return outputType_; }

    void wire(PipelineWiringState& state,
              const StageParams& params,
              const ErrorPolicy& policy = ErrorPolicy::skip()) const {
        if (!wiringFn_) {
            throw ConfigValidationException("Fabrika fonksiyonu tanimsiz: '" + typeName_ + "'");
        }
        wiringFn_(state, params, policy);
    }

    Stage<LogRecord, LogRecord>* createRecordStage(const StageParams& params) const {
        if (!recordCreator_) {
            throw ConfigValidationException(
                "'" + typeName_ + "' asamasi bir Stage<LogRecord, LogRecord> degildir (" +
                inputType_ + " -> " + outputType_ + ").");
        }
        return recordCreator_(params);
    }

    Stage<std::string, LogRecord>* createParserStage(const StageParams& params) const {
        if (!parserCreator_) {
            throw ConfigValidationException(
                "'" + typeName_ + "' asamasi bir Stage<string, LogRecord> degildir (" +
                inputType_ + " -> " + outputType_ + ").");
        }
        return parserCreator_(params);
    }

    // ========================================================================
    // YERLEŞİK (BUILT-IN) BİLEŞEN FABRİKA FONKSİYONLARI
    // ========================================================================

    static Source<std::string>* createFileSource(const StageParams& params) {
        const std::string ctx = "source (type: file)";
        params.rejectUnknownParams({"path"}, ctx);
        std::string path = params.requireString("path", ctx);
        return new FileLineSource(path);
    }

    static Stage<std::string, LogRecord>* createParseStage(const StageParams& params) {
        const std::string ctx = "stage (type: parse)";
        params.rejectUnknownParams({"format", "maxLineBytes", "deferRangeChecks"}, ctx);
        std::string format = params.requireString("format", ctx);
        if (format != "clf" && format != "combined") {
            throw ConfigValidationException(
                "Gecersiz parametre degeri: '" + ctx + "' icinde desteklenmeyen format '" + format +
                "'. Desteklenen formatlar: [clf, combined].");
        }
        long long maxBytes = params.optionalInt("maxLineBytes", static_cast<long long>(ParserStage::DEFAULT_MAX_LINE_BYTES), ctx);
        if (maxBytes <= 0) {
            throw ConfigValidationException(
                "Gecersiz parametre degeri: '" + ctx + "' icinde 'maxLineBytes' pozitif bir tamsayi olmalidir.");
        }
        bool deferRanges = params.optionalBool("deferRangeChecks", false, ctx);
        return new ParserStage(true, deferRanges, static_cast<size_t>(maxBytes));
    }

    /**
     * @brief 'validate' aşaması (ValidationStage) fabrikası (Artım 5 Görev 3).
     *
     * Desteklenen bildirimsel parametreler:
     * - requiredFields / required : [clientIp, method, path, timestamp, ...]
     * - statusRange               : [100, 599] (veya minStatus / maxStatus)
     * - bytesRange                : [0, 104857600] (veya minBytes / maxBytes)
     * - timestampYearRange        : [2000, 2035] (veya yearRange / minYear / maxYear)
     * - validateIp                : true | false (veya validateIpFormat)
     * - allowedMethods            : [GET, POST, PUT, DELETE, ...]
     */
    static Stage<LogRecord, LogRecord>* createValidateStage(const StageParams& params) {
        const std::string ctx = "stage (type: validate)";
        params.rejectUnknownParams({
            "requiredFields", "required",
            "statusRange", "minStatus", "maxStatus",
            "bytesRange", "minBytes", "maxBytes",
            "timestampYearRange", "yearRange", "minYear", "maxYear",
            "validateIp", "validateIpFormat",
            "allowedMethods"
        }, ctx);

        ValidationRules rules;

        if (params.has("requiredFields")) {
            rules.requiredFields = params.requireStringList("requiredFields", ctx);
        } else if (params.has("required")) {
            rules.requiredFields = params.requireStringList("required", ctx);
        }

        if (params.has("statusRange")) {
            auto r = params.optionalIntRange("statusRange", rules.minStatus, rules.maxStatus, ctx);
            rules.minStatus = static_cast<int>(r.first);
            rules.maxStatus = static_cast<int>(r.second);
        }
        rules.minStatus = static_cast<int>(params.optionalInt("minStatus", rules.minStatus, ctx));
        rules.maxStatus = static_cast<int>(params.optionalInt("maxStatus", rules.maxStatus, ctx));

        if (params.has("bytesRange")) {
            auto r = params.optionalIntRange("bytesRange", rules.minBytes, rules.maxBytes, ctx);
            rules.minBytes = r.first;
            rules.maxBytes = r.second;
        }
        rules.minBytes = params.optionalInt("minBytes", rules.minBytes, ctx);
        rules.maxBytes = params.optionalInt("maxBytes", rules.maxBytes, ctx);

        if (params.has("timestampYearRange")) {
            auto r = params.optionalIntRange("timestampYearRange", rules.minYear, rules.maxYear, ctx);
            rules.minYear = static_cast<int>(r.first);
            rules.maxYear = static_cast<int>(r.second);
        } else if (params.has("yearRange")) {
            auto r = params.optionalIntRange("yearRange", rules.minYear, rules.maxYear, ctx);
            rules.minYear = static_cast<int>(r.first);
            rules.maxYear = static_cast<int>(r.second);
        }
        rules.minYear = static_cast<int>(params.optionalInt("minYear", rules.minYear, ctx));
        rules.maxYear = static_cast<int>(params.optionalInt("maxYear", rules.maxYear, ctx));

        if (params.has("validateIpFormat")) {
            rules.validateIp = params.optionalBool("validateIpFormat", rules.validateIp, ctx);
        } else {
            rules.validateIp = params.optionalBool("validateIp", rules.validateIp, ctx);
        }

        if (params.has("allowedMethods")) {
            rules.allowedMethods = params.requireStringList("allowedMethods", ctx);
        }

        return new ValidationStage(rules);
    }

    static Stage<LogRecord, LogRecord>* createFilterStage(const StageParams& params) {
        const std::string ctx = "stage (type: filter)";
        params.rejectUnknownParams({"field", "op", "value"}, ctx);

        std::string field = params.requireString("field", ctx);
        std::string op    = params.requireString("op", ctx);
        if (!params.has("value")) {
            throw ConfigValidationException(
                "Eksik parametre (Missing parameter): '" + ctx +
                "' icinde zorunlu 'value' parametresi bulunamadi. "
                "Cozum: params bloguna 'value: <deger>' ekleyin.");
        }

        static const std::map<std::string, std::function<long long(const LogRecord&)>> numericExtractors = {
            {"status", [](const LogRecord& r) -> long long { return r.status(); }},
            {"bytes",  [](const LogRecord& r) -> long long { return r.bytes(); }}
        };

        static const std::map<std::string, std::function<std::string(const LogRecord&)>> stringExtractors = {
            {"clientIp",  [](const LogRecord& r) { return r.clientIp(); }},
            {"ip",        [](const LogRecord& r) { return r.clientIp(); }},
            {"method",    [](const LogRecord& r) { return r.method(); }},
            {"path",      [](const LogRecord& r) { return r.path(); }},
            {"userAgent", [](const LogRecord& r) { return r.userAgent(); }},
            {"timestamp", [](const LogRecord& r) { return r.getTimestampIso(); }}
        };

        static const std::map<std::string, std::function<bool(long long, long long)>> numericOps = {
            {"==", [](long long a, long long b) { return a == b; }},
            {"!=", [](long long a, long long b) { return a != b; }},
            {">",  [](long long a, long long b) { return a >  b; }},
            {">=", [](long long a, long long b) { return a >= b; }},
            {"<",  [](long long a, long long b) { return a <  b; }},
            {"<=", [](long long a, long long b) { return a <= b; }}
        };

        static const std::map<std::string, std::function<bool(const std::string&, const std::string&)>> stringOps = {
            {"==",         [](const std::string& a, const std::string& b) { return a == b; }},
            {"!=",         [](const std::string& a, const std::string& b) { return a != b; }},
            {">",          [](const std::string& a, const std::string& b) { return a >  b; }},
            {">=",         [](const std::string& a, const std::string& b) { return a >= b; }},
            {"<",          [](const std::string& a, const std::string& b) { return a <  b; }},
            {"<=",         [](const std::string& a, const std::string& b) { return a <= b; }},
            {"startsWith", [](const std::string& a, const std::string& b) { return a.rfind(b, 0) == 0; }},
            {"contains",   [](const std::string& a, const std::string& b) { return a.find(b) != std::string::npos; }},
            {"endsWith",   [](const std::string& a, const std::string& b) {
                return a.size() >= b.size() && a.compare(a.size() - b.size(), b.size(), b) == 0;
            }}
        };

        auto numFieldIt = numericExtractors.find(field);
        if (numFieldIt != numericExtractors.end()) {
            auto opIt = numericOps.find(op);
            if (opIt == numericOps.end()) {
                throw ConfigValidationException(
                    "Gecersiz operator: Sayisal '" + field + "' alani icin '" + op +
                    "' operatoru desteklenmiyor. Gecerli sayisal operatorler: [==, !=, >, >=, <, <=].");
            }
            long long targetVal = params.requireInt("value", ctx + " [field=" + field + "]");
            auto extractor = numFieldIt->second;
            auto comparator = opIt->second;
            return new PredicateFilterStage([extractor, comparator, targetVal](const LogRecord& r) {
                return comparator(extractor(r), targetVal);
            });
        }

        auto strFieldIt = stringExtractors.find(field);
        if (strFieldIt != stringExtractors.end()) {
            auto opIt = stringOps.find(op);
            if (opIt == stringOps.end()) {
                throw ConfigValidationException(
                    "Gecersiz operator: Metin tabanli '" + field + "' alani icin '" + op +
                    "' operatoru desteklenmiyor. Gecerli metin operatorleri: [==, !=, startsWith, contains, endsWith, >, >=, <, <=].");
            }
            std::string targetVal = params.requireString("value", ctx + " [field=" + field + "]");
            auto extractor = strFieldIt->second;
            auto comparator = opIt->second;
            return new PredicateFilterStage([extractor, comparator, targetVal](const LogRecord& r) {
                return comparator(extractor(r), targetVal);
            });
        }

        throw ConfigValidationException(
            "Bilinmeyen filtre alani: '" + field + "'. "
            "Desteklenen alanlar: [status, bytes, clientIp, method, path, userAgent, timestamp].");
    }

    static Stage<LogRecord, LogRecord>* createProjectStage(const StageParams& params) {
        const std::string ctx = "stage (type: project)";
        params.rejectUnknownParams({"fields"}, ctx);
        std::vector<std::string> fields = params.requireStringList("fields", ctx);

        static const std::vector<std::string> validFields = {
            "timestamp", "clientIp", "ip", "method", "path",
            "status", "bytes", "userAgent", "raw", "referer"
        };

        for (const auto& f : fields) {
            if (std::find(validFields.begin(), validFields.end(), f) == validFields.end()) {
                throw ConfigValidationException(
                    "Bilinmeyen projeksiyon alani: '" + f + "'. "
                    "Gecerli alanlar: [timestamp, clientIp, method, path, status, bytes, userAgent, raw, referer].");
            }
        }

        return new ProjectionStage(fields);
    }

    static Sink<LogRecord>* createFileSink(const StageParams& params) {
        const std::string ctx = "sink (type: file)";
        params.rejectUnknownParams({"path"}, ctx);
        std::string path = params.requireString("path", ctx);
        return new FileSink(path);
    }

    static Sink<LogRecord>* createConsoleSink(const StageParams& params) {
        const std::string ctx = "sink (type: console)";
        params.rejectUnknownParams({}, ctx);
        return new ConsoleSink();
    }

    static Sink<FailedRecord>* createDeadLetterFileSink(const StageParams& params) {
        const std::string ctx = "deadLetter (type: file)";
        params.rejectUnknownParams({"path"}, ctx);
        std::string path = params.requireString("path", ctx);
        return new DeadLetterFileSink(path);
    }

    static Sink<FailedRecord>* createDeadLetterConsoleSink(const StageParams& params) {
        const std::string ctx = "deadLetter (type: console)";
        params.rejectUnknownParams({}, ctx);
        return new DeadLetterConsoleSink();
    }
};
