#pragma once

#include "Source.hpp"
#include "Stage.hpp"
#include "Sink.hpp"
#include "PipelineBuilder.hpp"
#include "RunSummary.hpp"
#include <stdexcept>
#include <functional>
#include <vector>
#include <memory>
#include <chrono>
#include <iostream>

template <typename In = void, typename Out = void>
class Pipeline;

/**
 * @brief Genel ve Dinamik Boru Hattı (Artım 3–5).
 *
 * Artım 5 Güncellemesi:
 * - Çalıştırma sonunda gerçek geçen süreyi (wall-clock time), gelen/çıkan kayıt
 *   sayısını, her aşamada atılan kayıt sayısını ve dead-letter kanalına giden
 *   kayıt sayısını içeren `RunSummary` üretir ve yazdırır.
 */
template <>
class Pipeline<void, void> {
private:
    std::function<void()> runFn_;
    std::vector<std::function<void()>> deleters_;
    std::string name_;
    std::shared_ptr<RunSummary> summary_;
    bool printSummaryOnRun_ = true;

public:
    Pipeline() : summary_(std::make_shared<RunSummary>()) {}

    Pipeline(std::function<void()> runFn,
             std::vector<std::function<void()>> deleters,
             std::string name = "",
             std::shared_ptr<RunSummary> summary = nullptr)
        : runFn_(std::move(runFn)),
          deleters_(std::move(deleters)),
          name_(std::move(name)),
          summary_(summary ? std::move(summary) : std::make_shared<RunSummary>()) {
        if (!name_.empty()) {
            summary_->setPipelineName(name_);
        }
    }

    ~Pipeline() {
        for (auto& del : deleters_) {
            if (del) del();
        }
    }

    Pipeline(Pipeline&& other) noexcept
        : runFn_(std::move(other.runFn_)),
          deleters_(std::move(other.deleters_)),
          name_(std::move(other.name_)),
          summary_(std::move(other.summary_)),
          printSummaryOnRun_(other.printSummaryOnRun_) {
        other.deleters_.clear();
    }

    Pipeline& operator=(Pipeline&& other) noexcept {
        if (this != &other) {
            for (auto& del : deleters_) {
                if (del) del();
            }
            runFn_ = std::move(other.runFn_);
            deleters_ = std::move(other.deleters_);
            name_ = std::move(other.name_);
            summary_ = std::move(other.summary_);
            printSummaryOnRun_ = other.printSummaryOnRun_;
            other.deleters_.clear();
        }
        return *this;
    }

    Pipeline(const Pipeline&) = delete;
    Pipeline& operator=(const Pipeline&) = delete;

    void setName(std::string name) {
        name_ = std::move(name);
        if (summary_) {
            summary_->setPipelineName(name_);
        }
    }
    const std::string& getName() const { return name_; }

    void setPrintSummary(bool enabled) noexcept { printSummaryOnRun_ = enabled; }
    bool isPrintSummaryEnabled() const noexcept { return printSummaryOnRun_; }

    const RunSummary& getSummary() const { return *summary_; }
    const RunSummary& summary() const { return *summary_; }

    RunSummary run() {
        if (summary_) {
            summary_->reset();
            if (!name_.empty()) {
                summary_->setPipelineName(name_);
            }
        }
        auto startTp = std::chrono::steady_clock::now();
        try {
            if (runFn_) {
                runFn_();
            }
        } catch (...) {
            auto endTp = std::chrono::steady_clock::now();
            if (summary_) {
                auto us = std::chrono::duration_cast<std::chrono::microseconds>(endTp - startTp).count();
                summary_->setElapsedMicroseconds(us);
            }
            throw;
        }
        auto endTp = std::chrono::steady_clock::now();
        if (summary_) {
            auto us = std::chrono::duration_cast<std::chrono::microseconds>(endTp - startTp).count();
            summary_->setElapsedMicroseconds(us);
            if (printSummaryOnRun_) {
                summary_->print(std::cout);
            }
            return *summary_;
        }
        return RunSummary(name_);
    }
};

/**
 * @brief Tipli Boru Hattı (Artım 2 Geriye Dönük Uyumluluk).
 */
template <typename In, typename Out>
class Pipeline {
private:
    Source<In>* source = nullptr;
    Stage<In, Out>* stage = nullptr;
    Sink<Out>* sink = nullptr;

    class StageOutputEmitter : public Emitter<Out> {
    private:
        Sink<Out>& targetSink;
    public:
        explicit StageOutputEmitter(Sink<Out>& s) : targetSink(s) {}
        void emit(const Out& item) override {
            targetSink.consume(item);
        }
    };

    class SourceInputEmitter : public Emitter<In> {
    private:
        Stage<In, Out>& targetStage;
        StageOutputEmitter& outputEmitter;
    public:
        SourceInputEmitter(Stage<In, Out>& stg, StageOutputEmitter& outEmit)
            : targetStage(stg), outputEmitter(outEmit) {}

        void emit(const In& item) override {
            targetStage.process(item, outputEmitter);
        }
    };

public:
    Pipeline(Source<In>* src, Stage<In, Out>* stg, Sink<Out>* snk)
        : source(src), stage(stg), sink(snk) {}

    void run() {
        if (!source || !stage || !sink) {
            throw PipelineException("Source, Stage ve Sink bos (null) olamaz!", "Pipeline");
        }

        sink->open();
        stage->open();
        try {
            StageOutputEmitter stageOutEmitter(*sink);
            SourceInputEmitter sourceInEmitter(*stage, stageOutEmitter);
            source->produce(sourceInEmitter);
        } catch (...) {
            stage->close();
            sink->close();
            throw;
        }
        stage->close();
        sink->close();
    }
};

template <typename CurrentType>
Pipeline<void, void> PipelineBuilderStep<CurrentType>::to(Sink<CurrentType>* sink, bool own) {
    if (!sink) {
        throw PipelineException("Sink bos (null) olamaz!", "PipelineBuilder");
    }

    auto dlSlot = this->deadLetterSinkSlot;

    auto opens = this->openHooks;
    opens.push_back([sink]() { sink->open(); });
    opens.push_back([dlSlot]() {
        if (dlSlot && *dlSlot) {
            (*dlSlot)->open();
        }
    });

    auto closes = this->closeHooks;
    closes.push_back([sink]() { sink->close(); });
    closes.push_back([dlSlot]() {
        if (dlSlot && *dlSlot) {
            (*dlSlot)->close();
        }
    });

    auto dels = this->deleters;
    if (own) {
        dels.push_back([sink]() { delete sink; });
    }

    auto theRunner = this->runner;
    auto sumPtr = this->summary;

    auto runAll = [theRunner, sink, opens, closes, sumPtr]() {
        for (auto& o : opens) {
            o();
        }

        std::exception_ptr caughtException = nullptr;
        try {
            struct FinalEmitter : public Emitter<CurrentType> {
                Sink<CurrentType>* s;
                RunSummary* sum;
                FinalEmitter(Sink<CurrentType>* s, RunSummary* sm) : s(s), sum(sm) {}
                void emit(const CurrentType& item) override {
                    s->consume(item);
                    if (sum) sum->incrementRecordsOut();
                }
            } finalEmitter(sink, sumPtr.get());

            theRunner(finalEmitter);
        } catch (...) {
            caughtException = std::current_exception();
        }

        for (auto it = closes.rbegin(); it != closes.rend(); ++it) {
            try {
                (*it)();
            } catch (...) {
                if (!caughtException) caughtException = std::current_exception();
            }
        }

        if (caughtException) {
            std::rethrow_exception(caughtException);
        }
    };

    return Pipeline<void, void>(runAll, dels, "", sumPtr);
}
