#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include "PipelineBuilder.hpp"

std::vector<std::string> split(const std::string& s, char delimiter) {
    std::vector<std::string> tokens;
    std::string token;
    std::istringstream tokenStream(s);
    while (std::getline(tokenStream, token, delimiter)) {
        if (!token.empty()) tokens.push_back(token);
    }
    return tokens;
}

int main(int argc, char* argv[]) {
    int minStatus = -1;
    std::string pathPrefix = "";
    std::vector<std::string> fields;
    std::string inputFile = "test_input.log";
    std::string outputFile = "output.log";

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--min-status" && i + 1 < argc) {
            minStatus = std::stoi(argv[++i]);
        } else if (arg == "--path-prefix" && i + 1 < argc) {
            pathPrefix = argv[++i];
        } else if (arg == "--fields" && i + 1 < argc) {
            fields = split(argv[++i], ',');
        } else if (arg == "--in" && i + 1 < argc) {
            inputFile = argv[++i];
        } else if (arg == "--out" && i + 1 < argc) {
            outputFile = argv[++i];
        }
    }

    std::cout << "[INFO] Pipeline yapilandiriliyor..." << std::endl;
    std::cout << "Girdi: " << inputFile << ", Cikti: " << outputFile << std::endl;

    // GCC 4.9 icin kopyalama yapmayan dogrudan ilklendirme:
    PipelineBuilder builder = PipelineBuilder::from(new FileLineSource(inputFile));
    builder.via(new ParserStage());

    // 1. Min Status Filtresi
    if (minStatus != -1) {
        builder.via(new PredicateFilterStage([minStatus](const LogRecord& r) {
            return r.status >= minStatus;
        }));
    }

    // 2. Path Prefix Filtresi
    if (!pathPrefix.empty()) {
        builder.via(new PredicateFilterStage([pathPrefix](const LogRecord& r) {
            return r.path.rfind(pathPrefix, 0) == 0;
        }));
    }

    // 3. Projeksiyon Asamasi (Veri Minimizasyonu)
    if (!fields.empty()) {
        builder.via(new ProjectionStage(fields));
    }

    Pipeline pipeline = builder.to(new FileSink(outputFile));
    pipeline.execute();

    std::cout << "[BASARILI] Pipeline calistirildi. Sonuclar " << outputFile << " dosyasina yazildi." << std::endl;
    return 0;
}
