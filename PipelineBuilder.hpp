#ifndef PIPELINE_BUILDER_HPP
#define PIPELINE_BUILDER_HPP

#include <memory>
#include <vector>
#include <utility>
#include "PipelineStage.hpp"
#include "FilterStages.hpp"
#include "FileSink.hpp"

class Pipeline {
private:
    std::unique_ptr<FileLineSource> source;
    std::unique_ptr<ParserStage> parser;
    std::vector<std::unique_ptr<PipelineStage>> stages;
    std::unique_ptr<FileSink> sink;
public:
    Pipeline(std::unique_ptr<FileLineSource> src,
             std::unique_ptr<ParserStage> prs,
             std::vector<std::unique_ptr<PipelineStage>> stgs,
             std::unique_ptr<FileSink> snk)
        : source(std::move(src)), parser(std::move(prs)),
          stages(std::move(stgs)), sink(std::move(snk)) {}

    void execute() {
        if (!source || !parser || !sink) return;

        source->open();
        for (auto& stage : stages) stage->open();
        sink->open();

        std::string line;
        while (source->nextLine(line)) {
            LogRecord record;
            if (!parser->parse(line, record)) continue;

            bool accepted = true;
            for (auto& stage : stages) {
                if (!stage->process(record)) {
                    accepted = false;
                    break;
                }
            }
            if (accepted) {
                sink->write(record);
            }
        }

        sink->close();
        for (auto& stage : stages) stage->close();
        source->close();
    }
};

class PipelineBuilder {
private:
    std::unique_ptr<FileLineSource> source;
    std::unique_ptr<ParserStage> parser;
    std::vector<std::unique_ptr<PipelineStage>> stages;

public:
    PipelineBuilder() = default;

    // GCC 4.9 kopyalama denemesini engellemek icin Move Constructor tanimliyoruz:
    PipelineBuilder(PipelineBuilder&& other) noexcept
        : source(std::move(other.source)),
          parser(std::move(other.parser)),
          stages(std::move(other.stages)) {}

    PipelineBuilder& operator=(PipelineBuilder&& other) noexcept {
        if (this != &other) {
            source = std::move(other.source);
            parser = std::move(other.parser);
            stages = std::move(other.stages);
        }
        return *this;
    }

    // Kopyalamayi acikca kapatiyoruz
    PipelineBuilder(const PipelineBuilder&) = delete;
    PipelineBuilder& operator=(const PipelineBuilder&) = delete;

    static PipelineBuilder from(FileLineSource* src) {
        PipelineBuilder builder;
        builder.source.reset(src);
        return builder;
    }

    PipelineBuilder& via(ParserStage* prs) {
        parser.reset(prs);
        return *this;
    }

    PipelineBuilder& via(PipelineStage* stage) {
        stages.emplace_back(stage);
        return *this;
    }

    Pipeline to(FileSink* snk) {
        return Pipeline(std::move(source), std::move(parser), std::move(stages), std::unique_ptr<FileSink>(snk));
    }
};

#endif // PIPELINE_BUILDER_HPP
