#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <fstream>
#include "PipelineExceptions.hpp"
#include "FailedRecord.hpp"
#include "ErrorPolicy.hpp"
#include "DeadLetterSink.hpp"
#include "ValidationStage.hpp"
#include "RunSummary.hpp"
#include "FileLineSource.hpp"
#include "ParserStage.hpp"
#include "PredicateFilterStage.hpp"
#include "ProjectionStage.hpp"
#include "FileSink.hpp"
#include "ConsoleSink.hpp"
#include "Pipeline.hpp"
#include "StageFactory.hpp"
#include "StageRegistry.hpp"
#include "PipelineLoader.hpp"

static std::vector<std::string> split(const std::string& str, char delimiter) {
    std::vector<std::string> tokens;
    std::string token;
    std::istringstream tokenStream(str);
    while (std::getline(tokenStream, token, delimiter)) {
        if (!token.empty()) {
            tokens.push_back(token);
        }
    }
    return tokens;
}

static void printFileContent(const std::string& path) {
    std::ifstream f(path.c_str());
    if (!f.is_open()) {
        f.open(("../" + path).c_str());
    }
    if (!f.is_open()) return;
    std::string line;
    int lineNo = 0;
    while (std::getline(f, line)) {
        lineNo++;
        std::cout << "  " << line << "\n";
    }
    if (lineNo == 0) {
        std::cout << "  (Dosya bos - 0 satir)\n";
    }
}

static void printUsage(const char* progName) {
    std::cout << "Kullanim:\n"
              << "  " << progName << " --config <yapilandirma.yaml>\n"
              << "  " << progName << " [girdi_dosyasi] [secenekler]\n\n"
              << "Artim 5 & 4 - Yapilandirma Odakli Secenekler:\n"
              << "  --config, -c <dosya.yaml>    Bildirimsel YAML yapilandirma dosyasindan boru hattini yukler ve calistirir\n\n"
              << "Artim 3 - Dogrudan CLI Secenekleri:\n"
              << "  --filter <1|2|3|4|5>         Hazir filtre seceneklerinden birini dogrudan calistirir\n"
              << "  --min-status <kod>           Belirtilen durum koduna esit veya buyuk kayitlari filtreler (or. 400)\n"
              << "  --path-prefix <on-ek>        Yolu belirtilen on-ek ile baslayan kayitlari filtreler (or. /api)\n"
              << "  --fields <alan1,alan2,...>   Yalnizca secilen alanlari projeksiyonla ciktiya aktarir\n"
              << "  --out, -o, --output <dosya>  Ciktinin yazilacagi dosya yolu (FileSink)\n"
              << "  --order <sira>               Asama siralamasi: 'filter-first' (varsayilan) veya 'project-first'\n"
              << "  --help, -h                   Bu yardim iletisini gosterir\n\n"
              << "Ornekler:\n"
              << "  " << progName << " --config configs/chaos-test.yaml\n"
              << "  " << progName << " --config configs/empty-file-test.yaml\n"
              << "  " << progName << " --config configs/error-report.yaml\n"
              << "  " << progName << " --config configs/api-traffic.yaml\n"
              << "  " << progName << " --config configs/bandwidth-by-client.yaml\n";
}

// Artım 4 & 5: YAML yapılandırma dosyasından boru hattını yükleyip çalıştıran fonksiyon
static int runFromConfigFile(const std::string& configPath, bool showFilePreview = false) {
    try {
        PipelineLoader loader(StageRegistry::instance());
        PipelineConfig cfg = loader.parseConfigFromFile(configPath);
        Pipeline<> pipeline = loader.loadFromConfig(cfg);

        std::cout << "======================================================================\n";
        std::cout << " LogFlow Boru Hatti (Artim 5 - Hata Yonetimi, Dogrulama & Dead-Letter)\n";
        std::cout << "======================================================================\n";
        std::cout << " Yapilandirma Dosyasi : " << configPath << "\n";
        std::cout << " Boru Hatti Adi       : " << pipeline.getName() << "\n";
        std::cout << " Kaynak (Source)      : " << cfg.source.type;
        if (cfg.source.params.has("path")) {
            std::cout << " (" << cfg.source.params.get("path").asString() << ")";
        }
        std::cout << "\n";
        std::cout << " Asamalar & Politikalar: " << cfg.stages.size() << " asama [";
        for (size_t i = 0; i < cfg.stages.size(); ++i) {
            if (i > 0) std::cout << " -> ";
            std::cout << cfg.stages[i].type << "(" << cfg.stages[i].policy.toString() << ")";
        }
        std::cout << "]\n";
        std::cout << " Ana Hedef (Sink)     : " << cfg.sink.type;
        std::string outPath = "";
        if (cfg.sink.params.has("path")) {
            outPath = cfg.sink.params.get("path").asString();
            std::cout << " (" << outPath << ")";
        }
        std::cout << "\n";

        std::string dlPath = "";
        if (cfg.hasDeadLetter) {
            std::cout << " Dead-Letter Hedefi   : " << cfg.deadLetter.type;
            if (cfg.deadLetter.params.has("path")) {
                dlPath = cfg.deadLetter.params.get("path").asString();
                std::cout << " (" << dlPath << ")";
            }
            std::cout << "\n";
        }
        std::cout << "----------------------------------------------------------------------\n";

        pipeline.run();

        if (!outPath.empty()) {
            std::cout << "-> Ana cikti '" << outPath << "' dosyasina yazildi.\n";
            if (showFilePreview) {
                std::cout << "----------------------------------------------------------------------\n";
                std::cout << " Ana Cikti Icerigi (" << outPath << "):\n";
                printFileContent(outPath);
            }
        }
        if (!dlPath.empty()) {
            std::cout << "-> Dead-Letter ciktisi '" << dlPath << "' dosyasina yazildi.\n";
            if (showFilePreview) {
                std::cout << "----------------------------------------------------------------------\n";
                std::cout << " Dead-Letter Kanal Icerigi (" << dlPath << "):\n";
                printFileContent(dlPath);
            }
        }
        std::cout << "======================================================================\n";
        return 0;
    } catch (const ConfigValidationException& e) {
        std::cerr << e.what() << "\n";
        return 1;
    } catch (const PipelineException& e) {
        std::cerr << "[PipelineException - Kurtarilamaz Hata] " << e.what() << "\n";
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "[Calisma Zamani Hatasi] " << e.what() << "\n";
        return 1;
    }
}

// Artım 5: Hata Politikaları Karşılaştırma Demosu (FAIL_FAST, SKIP, DEAD_LETTER, RETRY(n))
static void runErrorPolicyDemo() {
    std::cout << "\n======================================================================\n";
    std::cout << " Artim 5: Hata Politikalari Demosu (FAIL_FAST, SKIP, DEAD_LETTER, RETRY)\n";
    std::cout << "======================================================================\n";

    // 1. FAIL_FAST Demosu
    std::cout << "\n--- [1] FAIL_FAST Politikasi (Ilk hatali kayitta boru hattini durdurur) ---\n";
    try {
        PipelineLoader loader;
        std::string yamlFailFast =
            "pipeline:\n"
            "  name: demo-fail-fast\n"
            "  source:\n"
            "    type: file\n"
            "    params: { path: data/access-dirty.log }\n"
            "  stages:\n"
            "    - type: parse\n"
            "      policy: FAIL_FAST\n"
            "      params: { format: combined }\n"
            "  sink:\n"
            "    type: file\n"
            "    params: { path: out/demo-failfast.log }\n";
        Pipeline<> p = loader.loadFromString(yamlFailFast);
        p.run();
    } catch (const PipelineException& e) {
        std::cout << "Yakalanan PipelineException:\n  " << e.what() << "\n";
    }

    // 2. SKIP Demosu
    std::cout << "\n--- [2] SKIP Politikasi (Hatali kayitlari atlar ve sayisini tutar) ---\n";
    {
        PipelineLoader loader;
        std::string yamlSkip =
            "pipeline:\n"
            "  name: demo-skip-policy\n"
            "  source:\n"
            "    type: file\n"
            "    params: { path: data/access-dirty.log }\n"
            "  stages:\n"
            "    - type: parse\n"
            "      policy: SKIP\n"
            "      params: { format: combined }\n"
            "    - type: validate\n"
            "      policy: SKIP\n"
            "      params:\n"
            "        requiredFields: [clientIp, method, path, timestamp]\n"
            "        statusRange: [100, 599]\n"
            "        bytesRange: [0, 104857600]\n"
            "        timestampYearRange: [2000, 2035]\n"
            "        validateIp: true\n"
            "  sink:\n"
            "    type: file\n"
            "    params: { path: out/demo-skip.log }\n";
        Pipeline<> p = loader.loadFromString(yamlSkip);
        p.run();
    }

    // 3. DEAD_LETTER Demosu
    std::cout << "\n--- [3] DEAD_LETTER Politikasi (Hatali kayitlari ikincil kanala yonlendirir) ---\n";
    runFromConfigFile("configs/chaos-test.yaml", false);
}

// Artım 4: Yapılandırma zamanı hata yakalama (Fail-Fast / Fail-Loudly) canlı gösterimi
static void runFailFastDemo() {
    PipelineLoader loader(StageRegistry::instance());

    struct ErrorCase {
        std::string title;
        std::string yaml;
    };

    std::vector<ErrorCase> cases = {
        {
            "1. Bilinmeyen Asama Turu (Unknown Stage Type)",
            "pipeline:\n"
            "  name: bad-stage-demo\n"
            "  source:\n"
            "    type: file\n"
            "    params: { path: data/access.log }\n"
            "  stages:\n"
            "    - type: parse\n"
            "      params: { format: clf }\n"
            "    - type: geoip_lookup\n"
            "      params: { db: city.mmdb }\n"
            "  sink:\n"
            "    type: file\n"
            "    params: { path: out/errors.txt }\n"
        },
        {
            "2. Eksik Zorunlu Parametre (Missing Required Parameter)",
            "pipeline:\n"
            "  name: missing-param-demo\n"
            "  source:\n"
            "    type: file\n"
            "    params: { path: data/access.log }\n"
            "  stages:\n"
            "    - type: parse\n"
            "      params: { format: clf }\n"
            "    - type: filter\n"
            "      params: { field: status, op: \">=\" }\n"
            "  sink:\n"
            "    type: file\n"
            "    params: { path: out/errors.txt }\n"
        },
        {
            "3. Yanlis Turde Parametre (Wrong Parameter Type)",
            "pipeline:\n"
            "  name: wrong-type-demo\n"
            "  source:\n"
            "    type: file\n"
            "    params: { path: data/access.log }\n"
            "  stages:\n"
            "    - type: parse\n"
            "      params: { format: clf }\n"
            "    - type: filter\n"
            "      params: { field: status, op: \">=\", value: \"dort_yuz\" }\n"
            "  sink:\n"
            "    type: file\n"
            "    params: { path: out/errors.txt }\n"
        },
        {
            "4. Bos Islem Hatti (Empty Pipeline)",
            "pipeline:\n"
            "  name: empty-pipeline-demo\n"
            "  source:\n"
            "    type: file\n"
            "    params: { path: data/access.log }\n"
            "  stages: []\n"
            "  sink:\n"
            "    type: file\n"
            "    params: { path: out/errors.txt }\n"
        }
    };

    std::cout << "\n======================================================================\n";
    std::cout << " Yapilandirma Zamani Hata Denetimi Demosu (PipelineException)\n";
    std::cout << "======================================================================\n";
    for (const auto& c : cases) {
        std::cout << "\n--- " << c.title << " ---\n";
        try {
            Pipeline<> p = loader.loadFromString(c.yaml);
            p.run();
        } catch (const PipelineException& e) {
            std::cout << "Yakalanan Hata:\n  " << e.what() << "\n";
        }
    }
    std::cout << "======================================================================\n";
}

// Artım 3 Geriye Dönük Uyumluluk
static void executePipeline(const std::string& inputFile,
                            const std::string& outputFile,
                            int filterChoice,
                            int minStatus,
                            const std::string& pathPrefix,
                            const std::string& fieldsArg,
                            const std::string& order) {
    std::cout << "\n======================================================================\n";
    std::cout << " LogFlow Boru Hatti Calistiriliyor...\n";
    std::cout << " Girdi Dosyasi : " << inputFile << "\n";
    if (!outputFile.empty()) {
        std::cout << " Cikti Hedefi  : " << outputFile << " (FileSink)\n";
    } else {
        std::cout << " Cikti Hedefi  : Standart Terminal (ConsoleSink)\n";
    }

    std::vector<std::string> fields = split(fieldsArg, ',');
    bool hasProjection = !fields.empty();

    ParserStage parser;
    auto step = PipelineBuilder::from(new FileLineSource(inputFile))
        .via(&parser, ErrorPolicy::skip(), "parse", false);

    std::vector<Stage<LogRecord, LogRecord>*> filterStages;

    if (filterChoice == 1) {
        std::cout << " Secilen Filtre: [1] Durum Kodu Filtresi (status >= 400)\n";
        filterStages.push_back(new PredicateFilterStage(PredicateFilterStage::statusAtLeast(400)));
    } else if (filterChoice == 2) {
        std::cout << " Secilen Filtre: [2] Yol On-Eki Filtresi (path startsWith /api)\n";
        filterStages.push_back(new PredicateFilterStage(PredicateFilterStage::pathStartsWith("/api")));
    } else if (filterChoice == 3) {
        std::cout << " Secilen Filtre: [3] Zaman Damgasi Araligi (10:01:00 - 10:04:00)\n";
        filterStages.push_back(new PredicateFilterStage([](const LogRecord& r) {
            std::string iso = r.getTimestampIso();
            return (iso >= "2026-09-29T07:01:00Z" && iso <= "2026-09-29T07:04:00Z");
        }));
    } else if (filterChoice == 4) {
        std::cout << " Secilen Filtre: [4] Coklu Filtre Zinciri (status >= 400 VE path startsWith /api)\n";
        filterStages.push_back(new PredicateFilterStage(PredicateFilterStage::statusAtLeast(400)));
        filterStages.push_back(new PredicateFilterStage(PredicateFilterStage::pathStartsWith("/api")));
    } else {
        if (minStatus >= 0) {
            std::cout << " Filtre Kurali : min-status >= " << minStatus << "\n";
            filterStages.push_back(new PredicateFilterStage(PredicateFilterStage::statusAtLeast(minStatus)));
        }
        if (!pathPrefix.empty()) {
            std::cout << " Filtre Kurali : path-prefix = " << pathPrefix << "\n";
            filterStages.push_back(new PredicateFilterStage(PredicateFilterStage::pathStartsWith(pathPrefix)));
        }
        if (filterStages.empty()) {
            std::cout << " Filtre Kurali : Filtresiz (Tum Gecerli Kayitlar)\n";
        }
    }

    if (!fieldsArg.empty()) {
        std::cout << " Projeksiyon   : " << fieldsArg << "\n";
    }
    std::cout << "----------------------------------------------------------------------\n";

    if (order == "project-first") {
        if (hasProjection) {
            step = step.via(new ProjectionStage(fields));
        }
        for (auto* filter : filterStages) {
            step = step.via(filter);
        }
    } else {
        for (auto* filter : filterStages) {
            step = step.via(filter);
        }
        if (hasProjection) {
            step = step.via(new ProjectionStage(fields));
        }
    }

    if (!outputFile.empty()) {
        Pipeline p = step.to(new FileSink(outputFile));
        p.run();
        std::cout << "-> Cikti basariyla '" << outputFile << "' dosyasina yazildi.\n";
    } else {
        Pipeline p = step.to(new ConsoleSink());
        p.run();
    }

    std::cout << "======================================================================\n\n";
}

int main(int argc, char* argv[]) {
    std::string configFile = "";
    std::string inputFile = "";
    std::string outputFile = "";
    int minStatus = -1;
    std::string pathPrefix = "";
    std::string fieldsArg = "";
    std::string order = "filter-first";
    int filterChoice = -1;

    std::vector<std::string> positionalArgs;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            printUsage(argv[0]);
            return 0;
        } else if (arg == "--config" || arg == "-c") {
            if (i + 1 >= argc) {
                std::cerr << "[Yapilandirma Hatasi] '--config' secenegi icin bir YAML dosya yolu belirtilmelidir.\n";
                return 1;
            }
            configFile = argv[++i];
        } else if (arg == "--filter" && i + 1 < argc) {
            filterChoice = std::stoi(argv[++i]);
        } else if (arg == "--min-status" && i + 1 < argc) {
            minStatus = std::stoi(argv[++i]);
        } else if (arg == "--path-prefix" && i + 1 < argc) {
            pathPrefix = argv[++i];
        } else if (arg == "--fields" && i + 1 < argc) {
            fieldsArg = argv[++i];
        } else if ((arg == "--out" || arg == "-o" || arg == "--output") && i + 1 < argc) {
            outputFile = argv[++i];
        } else if (arg == "--order" && i + 1 < argc) {
            order = argv[++i];
        } else if (arg.rfind("--", 0) == 0) {
            std::cerr << "Bilinmeyen secenek: " << arg << "\n";
            printUsage(argv[0]);
            return 1;
        } else {
            positionalArgs.push_back(arg);
        }
    }

    if (!configFile.empty()) {
        return runFromConfigFile(configFile, false);
    }

    if (!positionalArgs.empty()) {
        inputFile = positionalArgs[0];
        if (positionalArgs.size() > 1 && outputFile.empty()) {
            outputFile = positionalArgs[1];
        }
    }

    bool hasCliFlags = (!positionalArgs.empty() || filterChoice > 0 || minStatus >= 0 || !pathPrefix.empty() || !fieldsArg.empty() || !outputFile.empty());

    if (inputFile.empty()) {
        inputFile = "data/access.log";
        std::ifstream testFile(inputFile.c_str());
        if (!testFile.is_open()) {
            inputFile = "data/access-small.log";
            std::ifstream testFile2(inputFile.c_str());
            if (!testFile2.is_open()) {
                inputFile = "access-small.log";
            }
        }
    }

    if (hasCliFlags) {
        executePipeline(inputFile, outputFile, filterChoice, minStatus, pathPrefix, fieldsArg, order);
        return 0;
    }

    // Parametresiz çalıştırma: Artım 5 İnteraktif Menü
    while (true) {
        std::cout << "\n======================================================================\n";
        std::cout << "   LogFlow Boru Hatti (Artim 5) - Hata Yonetimi & Dead-Letter Menusu\n";
        std::cout << "======================================================================\n";
        std::cout << "Lutfen calistirmak istediginiz senaryoyu secin:\n\n";
        std::cout << "  [1] Kaos Testi: Bozuk Log Dosyasi Isleme (configs/chaos-test.yaml)\n";
        std::cout << "      -> data/access-dirty.log -> Ana Hedef (out/clean-access.log) + Dead-Letter (out/dead-letter.log)\n\n";
        std::cout << "  [2] Bos Dosya Kaos Testi (configs/empty-file-test.yaml)\n";
        std::cout << "      -> data/access-empty.log -> Sifir kayitla cokmeden temiz tamamlanma\n\n";
        std::cout << "  [3] Hata Politikalari Karsilastirma Demosu (FAIL_FAST vs SKIP vs DEAD_LETTER)\n\n";
        std::cout << "  [4] Artim 4 Yapilandirmalarini Calistir (error-report, api-traffic, bandwidth)\n\n";
        std::cout << "  [5] Yapilandirma Zamani Hata Denetimi Demosu (PipelineException)\n\n";
        std::cout << "  [6] Ozel YAML Yapilandirma Dosyasi Yolu Girerek Calistir\n";
        std::cout << "  [0] Cikis\n";
        std::cout << "----------------------------------------------------------------------\n";
        std::cout << "Seciminiz [1-6 veya 0]: " << std::flush;

        std::string inputStr;
        if (!std::getline(std::cin, inputStr)) {
            break;
        }

        size_t first = inputStr.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) {
            std::cout << "-> Lutfen 0 ile 6 arasinda bir secim yapip Enter'a basin.\n";
            continue;
        }
        std::string trimmed = inputStr.substr(first);

        int choice = -1;
        try {
            choice = std::stoi(trimmed);
        } catch (...) {
            std::cout << "\n[!] Gecersiz giris! Lutfen 0-6 arasi bir rakam girin.\n";
            continue;
        }

        if (choice == 0) {
            std::cout << "\nLogFlow'dan cikis yapildi. Iyi calismalar!\n";
            break;
        } else if (choice == 1) {
            runFromConfigFile("configs/chaos-test.yaml", true);
        } else if (choice == 2) {
            runFromConfigFile("configs/empty-file-test.yaml", true);
        } else if (choice == 3) {
            runErrorPolicyDemo();
        } else if (choice == 4) {
            runFromConfigFile("configs/error-report.yaml", true);
            runFromConfigFile("configs/api-traffic.yaml", true);
            runFromConfigFile("configs/bandwidth-by-client.yaml", true);
        } else if (choice == 5) {
            runFailFastDemo();
        } else if (choice == 6) {
            std::cout << "YAML dosya yolunu girin (or. configs/chaos-test.yaml): " << std::flush;
            std::string customPath;
            if (std::getline(std::cin, customPath)) {
                size_t f = customPath.find_first_not_of(" \t\r\n");
                size_t l = customPath.find_last_not_of(" \t\r\n");
                if (f != std::string::npos) {
                    runFromConfigFile(customPath.substr(f, l - f + 1), true);
                }
            }
        } else {
            std::cout << "\n[!] Gecersiz secim! Lutfen 0 ile 6 arasinda bir secenek belirleyin.\n";
        }
    }

    return 0;
}
