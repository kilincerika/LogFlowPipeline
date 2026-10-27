#pragma once

#include "StageFactory.hpp"
#include "StageRegistry.hpp"
#include "Pipeline.hpp"
#include "ErrorPolicy.hpp"

#include <string>
#include <vector>
#include <map>
#include <fstream>
#include <sstream>
#include <cctype>
#include <algorithm>

/**
 * @brief Bildirimsel Yapılandırma Bileşen Tanımı (ComponentSpec).
 */
struct ComponentSpec {
    std::string type;
    StageParams params;
    ErrorPolicy policy = ErrorPolicy::skip();
    bool hasExplicitPolicy = false;
};

/**
 * @brief Bildirimsel Boru Hattı Yapılandırma Modeli (PipelineConfig).
 *
 * Artım 5 Güncellemesi:
 * - Aşamalar kendi `ErrorPolicy` (`FAIL_FAST`, `SKIP`, `DEAD_LETTER`, `RETRY(n)`) ayarlarına sahiptir.
 * - İsteğe bağlı ikincil çıktı yolu olan `deadLetter` (`Sink<FailedRecord>`) tanımını barındırır.
 */
struct PipelineConfig {
    std::string name;
    ComponentSpec source;
    std::vector<ComponentSpec> stages;
    ComponentSpec sink;
    bool hasDeadLetter = false;
    ComponentSpec deadLetter;
};

/**
 * @brief Sıfır Bağımlılıklı YAML Yapılandırma Ayrıştırıcısı (YamlConfigParser).
 */
class YamlConfigParser {
private:
    struct Line {
        int indent;
        int lineNum;
        std::string text;
    };

    static std::string trim(const std::string& s) {
        size_t first = s.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) return "";
        size_t last = s.find_last_not_of(" \t\r\n");
        return s.substr(first, last - first + 1);
    }

    static std::string stripComment(const std::string& s) {
        bool inSingle = false;
        bool inDouble = false;
        for (size_t i = 0; i < s.size(); ++i) {
            char c = s[i];
            if (c == '\'' && !inDouble) inSingle = !inSingle;
            else if (c == '"' && !inSingle) inDouble = !inDouble;
            else if (c == '#' && !inSingle && !inDouble) {
                return s.substr(0, i);
            }
        }
        return s;
    }

    static std::vector<std::string> splitFlowItems(const std::string& inner, int lineNum) {
        std::vector<std::string> items;
        std::string current;
        bool inSingle = false;
        bool inDouble = false;
        int braceDepth = 0;
        int bracketDepth = 0;

        for (size_t i = 0; i < inner.size(); ++i) {
            char c = inner[i];
            if (c == '\'' && !inDouble) {
                inSingle = !inSingle;
                current.push_back(c);
            } else if (c == '"' && !inSingle) {
                inDouble = !inDouble;
                current.push_back(c);
            } else if (!inSingle && !inDouble) {
                if (c == '{') { braceDepth++; current.push_back(c); }
                else if (c == '}') { braceDepth--; current.push_back(c); }
                else if (c == '[') { bracketDepth++; current.push_back(c); }
                else if (c == ']') { bracketDepth--; current.push_back(c); }
                else if (c == ',' && braceDepth == 0 && bracketDepth == 0) {
                    std::string t = trim(current);
                    if (!t.empty()) items.push_back(t);
                    current.clear();
                } else {
                    current.push_back(c);
                }
            } else {
                current.push_back(c);
            }
        }

        if (inSingle || inDouble || braceDepth != 0 || bracketDepth != 0) {
            throw ConfigValidationException(
                "YAML sozdizimi hatasi (satir " + std::to_string(lineNum) +
                "): Kapatilmamis tirnak veya parantez: '" + inner + "'.");
        }

        std::string last = trim(current);
        if (!last.empty()) {
            items.push_back(last);
        }
        return items;
    }

    static size_t findTopLevelColon(const std::string& s) {
        bool inSingle = false;
        bool inDouble = false;
        int braceDepth = 0;
        int bracketDepth = 0;

        for (size_t i = 0; i < s.size(); ++i) {
            char c = s[i];
            if (c == '\'' && !inDouble) inSingle = !inSingle;
            else if (c == '"' && !inSingle) inDouble = !inDouble;
            else if (!inSingle && !inDouble) {
                if (c == '{') braceDepth++;
                else if (c == '}') braceDepth--;
                else if (c == '[') bracketDepth++;
                else if (c == ']') bracketDepth--;
                else if (c == ':' && braceDepth == 0 && bracketDepth == 0) {
                    if (i + 1 == s.size() || s[i + 1] == ' ' || s[i + 1] == '\t') {
                        return i;
                    }
                }
            }
        }
        return std::string::npos;
    }

    static bool isIntegerLiteral(const std::string& s, long long& outVal) {
        if (s.empty()) return false;
        size_t start = 0;
        if (s[0] == '-' || s[0] == '+') {
            if (s.size() == 1) return false;
            start = 1;
        }
        for (size_t i = start; i < s.size(); ++i) {
            if (!std::isdigit(static_cast<unsigned char>(s[i]))) return false;
        }
        try {
            size_t idx = 0;
            outVal = std::stoll(s, &idx);
            return idx == s.size();
        } catch (...) {
            return false;
        }
    }

public:
    static ConfigValue parseInlineValue(const std::string& raw, int lineNum = 0) {
        std::string s = trim(raw);
        if (s.empty() || s == "~" || s == "null") {
            return ConfigValue();
        }

        if (s.front() == '{') {
            if (s.back() != '}') {
                throw ConfigValidationException(
                    "YAML sozdizimi hatasi (satir " + std::to_string(lineNum) +
                    "): '{' ile baslayan eslem '}' ile bitmelidir: '" + s + "'.");
            }
            std::string inner = trim(s.substr(1, s.size() - 2));
            std::map<std::string, ConfigValue> mapResult;
            if (!inner.empty()) {
                auto pairs = splitFlowItems(inner, lineNum);
                for (const auto& pairStr : pairs) {
                    size_t colonPos = findTopLevelColon(pairStr);
                    if (colonPos == std::string::npos) {
                        throw ConfigValidationException(
                            "YAML sozdizimi hatasi (satir " + std::to_string(lineNum) +
                            "): Satir ici eslemde gecersiz 'anahtar: deger' cifti: '" + pairStr + "'.");
                    }
                    std::string key = trim(pairStr.substr(0, colonPos));
                    std::string valStr = trim(pairStr.substr(colonPos + 1));
                    if (key.size() >= 2 && ((key.front() == '"' && key.back() == '"') || (key.front() == '\'' && key.back() == '\''))) {
                        key = key.substr(1, key.size() - 2);
                    }
                    mapResult[key] = parseInlineValue(valStr, lineNum);
                }
            }
            return ConfigValue::fromMap(std::move(mapResult));
        }

        if (s.front() == '[') {
            if (s.back() != ']') {
                throw ConfigValidationException(
                    "YAML sozdizimi hatasi (satir " + std::to_string(lineNum) +
                    "): '[' ile baslayan liste ']' ile bitmelidir: '" + s + "'.");
            }
            std::string inner = trim(s.substr(1, s.size() - 2));
            std::vector<ConfigValue> listResult;
            if (!inner.empty()) {
                auto elems = splitFlowItems(inner, lineNum);
                for (const auto& elemStr : elems) {
                    listResult.push_back(parseInlineValue(elemStr, lineNum));
                }
            }
            return ConfigValue::fromList(std::move(listResult));
        }

        if (s.size() >= 2 && ((s.front() == '"' && s.back() == '"') || (s.front() == '\'' && s.back() == '\''))) {
            return ConfigValue::fromString(s.substr(1, s.size() - 2));
        }

        if (s == "true") return ConfigValue::fromBool(true);
        if (s == "false") return ConfigValue::fromBool(false);

        long long intVal = 0;
        if (isIntegerLiteral(s, intVal)) {
            return ConfigValue::fromInt(intVal);
        }

        return ConfigValue::fromString(s);
    }

private:
    static ConfigValue parseBlock(const std::vector<Line>& lines, size_t& idx, int minIndent) {
        if (idx >= lines.size() || lines[idx].indent < minIndent) {
            return ConfigValue();
        }

        int blockIndent = lines[idx].indent;
        bool isSequence = (lines[idx].text == "-" || lines[idx].text.rfind("- ", 0) == 0);

        if (isSequence) {
            std::vector<ConfigValue> listResult;
            while (idx < lines.size() && lines[idx].indent == blockIndent) {
                const std::string& t = lines[idx].text;
                int lineNum = lines[idx].lineNum;
                if (t != "-" && t.rfind("- ", 0) != 0) {
                    break;
                }
                std::string afterDash = (t == "-") ? "" : trim(t.substr(2));
                idx++;

                if (afterDash.empty()) {
                    if (idx < lines.size() && lines[idx].indent > blockIndent) {
                        listResult.push_back(parseBlock(lines, idx, blockIndent + 1));
                    } else {
                        listResult.push_back(ConfigValue());
                    }
                } else {
                    size_t colonPos = findTopLevelColon(afterDash);
                    if (colonPos != std::string::npos && afterDash.front() != '{' && afterDash.front() != '[') {
                        std::map<std::string, ConfigValue> itemMap;
                        std::string k = trim(afterDash.substr(0, colonPos));
                        std::string vStr = trim(afterDash.substr(colonPos + 1));
                        if (!vStr.empty()) {
                            itemMap[k] = parseInlineValue(vStr, lineNum);
                        } else if (idx < lines.size() && lines[idx].indent > blockIndent) {
                            itemMap[k] = parseBlock(lines, idx, blockIndent + 1);
                        } else {
                            itemMap[k] = ConfigValue();
                        }

                        if (idx < lines.size() && lines[idx].indent > blockIndent) {
                            ConfigValue subBlock = parseBlock(lines, idx, blockIndent + 1);
                            if (subBlock.isMap()) {
                                for (const auto& kv : subBlock.asMap()) {
                                    itemMap[kv.first] = kv.second;
                                }
                            }
                        }
                        listResult.push_back(ConfigValue::fromMap(std::move(itemMap)));
                    } else {
                        listResult.push_back(parseInlineValue(afterDash, lineNum));
                    }
                }
            }
            return ConfigValue::fromList(std::move(listResult));
        }

        std::map<std::string, ConfigValue> mapResult;
        while (idx < lines.size() && lines[idx].indent == blockIndent) {
            const std::string& t = lines[idx].text;
            int lineNum = lines[idx].lineNum;
            if (t == "-" || t.rfind("- ", 0) == 0) {
                break;
            }

            size_t colonPos = findTopLevelColon(t);
            if (colonPos == std::string::npos) {
                throw ConfigValidationException(
                    "YAML sozdizimi hatasi (satir " + std::to_string(lineNum) +
                    "): Gecersiz 'anahtar: deger' ifadesi: '" + t + "'.");
            }

            std::string key = trim(t.substr(0, colonPos));
            std::string valStr = trim(t.substr(colonPos + 1));
            idx++;

            if (!valStr.empty()) {
                mapResult[key] = parseInlineValue(valStr, lineNum);
            } else {
                if (idx < lines.size() && lines[idx].indent > blockIndent) {
                    mapResult[key] = parseBlock(lines, idx, blockIndent + 1);
                } else {
                    mapResult[key] = ConfigValue();
                }
            }
        }
        return ConfigValue::fromMap(std::move(mapResult));
    }

public:
    static ConfigValue parse(const std::string& yamlContent) {
        std::vector<Line> lines;
        std::istringstream iss(yamlContent);
        std::string rawLine;
        int lineNum = 0;

        while (std::getline(iss, rawLine)) {
            lineNum++;
            std::string cleaned = stripComment(rawLine);
            if (trim(cleaned).empty()) continue;

            int indent = 0;
            for (char c : cleaned) {
                if (c == ' ') indent++;
                else if (c == '\t') indent += 2;
                else break;
            }
            lines.push_back({indent, lineNum, trim(cleaned)});
        }

        if (lines.empty()) {
            return ConfigValue();
        }

        if (lines.size() == 1 && lines[0].text.front() == '{') {
            return parseInlineValue(lines[0].text, lines[0].lineNum);
        }

        size_t idx = 0;
        ConfigValue root = parseBlock(lines, idx, 0);
        if (idx < lines.size()) {
            throw ConfigValidationException(
                "YAML girinti (indentation) hatasi (satir " + std::to_string(lines[idx].lineNum) +
                "): Beklenmeyen satir '" + lines[idx].text + "'.");
        }
        return root;
    }
};

/**
 * @brief Yapılandırma Yükleyicisi (PipelineLoader).
 */
class PipelineLoader {
private:
    const StageRegistry& registry_;

    static bool isPolicyKey(const std::string& k) {
        return k == "policy" || k == "onError" || k == "errorPolicy" || k == "error_policy" || k == "on_error";
    }

    static bool isDeadLetterKey(const std::string& k) {
        return k == "deadLetter" || k == "dead_letter" || k == "deadLetterSink" || k == "dead_letter_sink";
    }

    static ComponentSpec parseComponentSpec(const ConfigValue& node,
                                            const std::string& sectionName,
                                            bool allowErrorPolicy = false) {
        if (node.isNull() || !node.isMap()) {
            throw ConfigValidationException(
                "Eksik veya gecersiz bolum: '" + sectionName +
                "' bolumu bir nesne (map) olmalidir ve 'type' alani icermelidir.");
        }

        const auto& m = node.asMap();
        for (const auto& kv : m) {
            if (kv.first != "type" && kv.first != "params" && !(allowErrorPolicy && isPolicyKey(kv.first))) {
                std::string allowed = allowErrorPolicy ? "[type, params, policy, onError, errorPolicy]" : "[type, params]";
                throw ConfigValidationException(
                    "Bilinmeyen yapilandirma anahtari: '" + sectionName +
                    "' icinde '" + kv.first + "' gecersizdir. Izin verilen alanlar: " + allowed + ".");
            }
        }

        auto typeIt = m.find("type");
        if (typeIt == m.end() || typeIt->second.isNull()) {
            throw ConfigValidationException(
                "Eksik parametre (Missing parameter): '" + sectionName +
                "' icinde zorunlu 'type' alani belirtilmedi.");
        }
        if (!typeIt->second.isString() || typeIt->second.asString().empty()) {
            throw ConfigValidationException(
                "Yanlis turde parametre (Wrong parameter type): '" + sectionName +
                "' icindeki 'type' alani bos olmayan bir 'string (metin)' olmalidir, ancak '" +
                typeIt->second.typeName() + "' verildi.");
        }

        ComponentSpec spec;
        spec.type = typeIt->second.asString();

        // Aşama düzeyinde policy / onError / errorPolicy okuma
        if (allowErrorPolicy) {
            for (const auto& kv : m) {
                if (isPolicyKey(kv.first) && !kv.second.isNull()) {
                    if (!kv.second.isString()) {
                        throw ConfigValidationException(
                            "Yanlis turde parametre (Wrong parameter type): '" + sectionName +
                            "' icindeki '" + kv.first + "' alani 'string' olmalidir.");
                    }
                    spec.policy = ErrorPolicy::parse(kv.second.asString(), sectionName + " (" + spec.type + ")");
                    spec.hasExplicitPolicy = true;
                }
            }
        }

        auto paramsIt = m.find("params");
        if (paramsIt != m.end() && !paramsIt->second.isNull()) {
            if (!paramsIt->second.isMap()) {
                throw ConfigValidationException(
                    "Yanlis turde parametre (Wrong parameter type): '" + sectionName +
                    " (type: " + spec.type + ")' icindeki 'params' alani bir nesne/sozluk '{ ... }' olmalidir, ancak '" +
                    paramsIt->second.typeName() + "' verildi.");
            }
            auto paramMap = paramsIt->second.asMap();

            // params içinde policy / onError tanımlanmışsa onu da destekle
            if (allowErrorPolicy) {
                for (auto it = paramMap.begin(); it != paramMap.end(); ) {
                    if (isPolicyKey(it->first)) {
                        if (!it->second.isString()) {
                            throw ConfigValidationException(
                                "Yanlis turde parametre (Wrong parameter type): '" + sectionName +
                                "' icindeki '" + it->first + "' alani 'string' olmalidir.");
                        }
                        spec.policy = ErrorPolicy::parse(it->second.asString(), sectionName + " (" + spec.type + ")");
                        spec.hasExplicitPolicy = true;
                        it = paramMap.erase(it);
                    } else {
                        ++it;
                    }
                }
            }

            spec.params = StageParams(std::move(paramMap));
        }

        return spec;
    }

public:
    explicit PipelineLoader(const StageRegistry& registry = StageRegistry::instance())
        : registry_(registry) {}

    PipelineConfig parseConfigFromString(const std::string& yamlContent) const {
        ConfigValue root = YamlConfigParser::parse(yamlContent);
        if (root.isNull() || !root.isMap() || root.asMap().empty()) {
            throw ConfigValidationException(
                "Bos islem hatti (Empty pipeline): Yapilandirma dosyasi bos veya gecersiz.");
        }

        const auto& rootMap = root.asMap();
        auto pipeIt = rootMap.find("pipeline");
        if (pipeIt == rootMap.end() || pipeIt->second.isNull() || !pipeIt->second.isMap() || pipeIt->second.asMap().empty()) {
            throw ConfigValidationException(
                "Bos islem hatti (Empty pipeline): En ust duzeyde 'pipeline:' blogu tanimlanmalidir.");
        }

        const auto& pipeMap = pipeIt->second.asMap();
        for (const auto& kv : pipeMap) {
            if (kv.first != "name" && kv.first != "source" && kv.first != "stages" &&
                kv.first != "sink" && !isDeadLetterKey(kv.first)) {
                throw ConfigValidationException(
                    "Bilinmeyen pipeline anahtari: '" + kv.first +
                    "'. Gecerli anahtarlar: [name, source, stages, sink, deadLetter].");
            }
        }

        PipelineConfig cfg;

        // 1. İsim
        auto nameIt = pipeMap.find("name");
        if (nameIt != pipeMap.end() && !nameIt->second.isNull()) {
            if (!nameIt->second.isString()) {
                throw ConfigValidationException(
                    "Yanlis turde parametre (Wrong parameter type): 'pipeline.name' alani 'string' olmalidir, ancak '" +
                    nameIt->second.typeName() + "' verildi.");
            }
            cfg.name = nameIt->second.asString();
        } else {
            cfg.name = "unnamed-pipeline";
        }

        // 2. Kaynak (Source)
        auto srcIt = pipeMap.find("source");
        if (srcIt == pipeMap.end() || srcIt->second.isNull()) {
            throw ConfigValidationException(
                "Bos islem hatti (Empty pipeline): 'pipeline.source' (kaynak) bolumu tanimlanmamis.");
        }
        cfg.source = parseComponentSpec(srcIt->second, "pipeline.source", false);

        // 3. Aşamalar (Stages)
        auto stagesIt = pipeMap.find("stages");
        if (stagesIt == pipeMap.end() || stagesIt->second.isNull()) {
            throw ConfigValidationException(
                "Bos islem hatti (Empty pipeline): 'pipeline.stages' bolumu eksik; en az bir asama tanimlanmalidir.");
        }
        if (!stagesIt->second.isList()) {
            throw ConfigValidationException(
                "Yanlis turde parametre (Wrong parameter type): 'pipeline.stages' bir liste (dizi) olmalidir, ancak '" +
                stagesIt->second.typeName() + "' verildi.");
        }
        const auto& stageList = stagesIt->second.asList();
        if (stageList.empty()) {
            throw ConfigValidationException(
                "Bos islem hatti (Empty pipeline): 'pipeline.stages' listesi bos olamaz; en az bir asama (or. parse) icermelidir.");
        }

        for (size_t i = 0; i < stageList.size(); ++i) {
            cfg.stages.push_back(parseComponentSpec(
                stageList[i], "pipeline.stages[" + std::to_string(i) + "]", true));
        }

        // 4. Ana Hedef (Sink)
        auto sinkIt = pipeMap.find("sink");
        if (sinkIt == pipeMap.end() || sinkIt->second.isNull()) {
            throw ConfigValidationException(
                "Bos islem hatti (Empty pipeline): 'pipeline.sink' (hedef) bolumu tanimlanmamis.");
        }
        cfg.sink = parseComponentSpec(sinkIt->second, "pipeline.sink", false);

        // 5. İkincil Dead-Letter Hedefi (İsteğe bağlı, Artım 5 Görev 3)
        for (const auto& kv : pipeMap) {
            if (isDeadLetterKey(kv.first) && !kv.second.isNull()) {
                cfg.hasDeadLetter = true;
                cfg.deadLetter = parseComponentSpec(kv.second, "pipeline." + kv.first, false);
                break;
            }
        }

        return cfg;
    }

    PipelineConfig parseConfigFromFile(const std::string& filePath) const {
        std::ifstream file(filePath.c_str());
        if (!file.is_open()) {
            file.open(("../" + filePath).c_str());
        }
        if (!file.is_open()) {
            throw ConfigValidationException(
                "Yapilandirma dosyasi acilamadi: '" + filePath +
                "'. Dosya yolunun dogru oldugundan emin olun.");
        }
        std::ostringstream ss;
        ss << file.rdbuf();
        return parseConfigFromString(ss.str());
    }

    Pipeline<void, void> loadFromConfig(const PipelineConfig& cfg) const {
        if (cfg.source.type.empty() || cfg.stages.empty() || cfg.sink.type.empty()) {
            throw ConfigValidationException(
                "Bos islem hatti (Empty pipeline): Source, en az bir Stage ve Sink zorunludur.");
        }

        // 1. Tüm aşama türlerinin kayıt defterinde var olduğunu doğrula
        bool hasValidationStage = false;
        for (const auto& stgSpec : cfg.stages) {
            (void)registry_.getStageFactory(stgSpec.type);
            if (stgSpec.type == "validate") {
                hasValidationStage = true;
            }
        }

        // 2. Kaynağı (Source) oluştur
        Source<std::string>* src = registry_.createSource(cfg.source.type, cfg.source.params);
        PipelineWiringState wiringState(src);

        // 3. Aşamaları (Stages) sırayla oluştur, hata politikalarıyla birlikte bağla
        try {
            for (const auto& stgSpec : cfg.stages) {
                const StageFactory& factory = registry_.getStageFactory(stgSpec.type);
                StageParams effectiveParams = stgSpec.params;
                // Boru hattında 'validate' aşaması varsa, negatif bayt / aralık denetimlerini
                // ValidationStage'in bildirimsel kurallarına devret
                if (stgSpec.type == "parse" && hasValidationStage && !effectiveParams.has("deferRangeChecks")) {
                    effectiveParams.set("deferRangeChecks", ConfigValue::fromBool(true));
                }
                factory.wire(wiringState, effectiveParams, stgSpec.policy);
            }

            // 4. İkincil Dead-Letter hedefini oluştur ve bağla (varsa)
            if (cfg.hasDeadLetter) {
                Sink<FailedRecord>* dlSink = registry_.createDeadLetterSink(cfg.deadLetter.type, cfg.deadLetter.params);
                wiringState.attachDeadLetterSink(dlSink, true);
            }

            // 5. Ana Hedefi (Sink) oluştur ve boru hattını tamamla
            Sink<LogRecord>* snk = registry_.createSink(cfg.sink.type, cfg.sink.params);
            return wiringState.finalizeWithSink(snk, cfg.name);
        } catch (...) {
            throw;
        }
    }

    Pipeline<void, void> loadFromString(const std::string& yamlContent) const {
        PipelineConfig cfg = parseConfigFromString(yamlContent);
        return loadFromConfig(cfg);
    }

    Pipeline<void, void> loadFromFile(const std::string& filePath) const {
        PipelineConfig cfg = parseConfigFromFile(filePath);
        return loadFromConfig(cfg);
    }

    Pipeline<void, void> load(const std::string& filePath) const {
        return loadFromFile(filePath);
    }
};
