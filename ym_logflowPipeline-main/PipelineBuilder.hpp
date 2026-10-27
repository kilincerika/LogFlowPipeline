#pragma once

#include "Source.hpp"
#include "Stage.hpp"
#include "Sink.hpp"
#include "Emitter.hpp"
#include "LogRecord.hpp"
#include "PipelineExceptions.hpp"
#include "FailedRecord.hpp"
#include "ErrorPolicy.hpp"
#include "RunSummary.hpp"
#include <functional>
#include <vector>
#include <memory>
#include <stdexcept>
#include <string>
#include <chrono>

// İleri bildirim
template <typename In, typename Out>
class Pipeline;

namespace logflow_detail {
    template <typename T>
    inline std::string extractRawInput(const T&) {
        return "<non-string-record>";
    }

    template <>
    inline std::string extractRawInput<std::string>(const std::string& s) {
        return s;
    }

    template <>
    inline std::string extractRawInput<LogRecord>(const LogRecord& r) {
        return r.getRaw();
    }
}

/**
 * @brief Akıcı Boru Hattı İnşa Adımı (PipelineBuilderStep).
 *
 * Artım 5 Mimari Evrimi:
 * - Derleme zamanında tür güvenliği sağlar.
 * - Her aşama düğümünü kendi ayarlanabilir `ErrorPolicy` stratejisiyle
 *   (FAIL_FAST, SKIP, DEAD_LETTER, RETRY(n)) sarmalar.
 * - Başarısız kayıtları ikincil ve bağımsız bir çıktı kanalı olan Dead-Letter
 *   hedefine (`Sink<FailedRecord>`) yönlendirir.
 * - Her aşamada giren, çıkan, atlanan ve dead-letter kanalına giden kayıtları
 *   `RunSummary` içinde muhasebeleştirir.
 */
template <typename CurrentType>
class PipelineBuilderStep {
public:
    std::function<void(Emitter<CurrentType>&)> runner;
    std::vector<std::function<void()>> openHooks;
    std::vector<std::function<void()>> closeHooks;
    std::vector<std::function<void()>> deleters;
    std::shared_ptr<RunSummary> summary;
    std::shared_ptr<Sink<FailedRecord>*> deadLetterSinkSlot;

    PipelineBuilderStep()
        : summary(std::make_shared<RunSummary>()),
          deadLetterSinkSlot(std::make_shared<Sink<FailedRecord>*>(nullptr)) {}

    /**
     * @brief Boru hattına bağımsız ikincil Dead-Letter hedefi (`Sink<FailedRecord>`) bağlar.
     * Bu nokta, boru hattı topolojisinin düz bir çizgi olmaktan çıkıp çatallandığı noktadır.
     */
    PipelineBuilderStep<CurrentType>& withDeadLetterSink(Sink<FailedRecord>* dlSink, bool own = true) {
        if (!dlSink) {
            throw PipelineException("DeadLetterSink bos (null) olamaz!", "PipelineBuilder");
        }
        *deadLetterSinkSlot = dlSink;
        if (own) {
            deleters.push_back([dlSink]() { delete dlSink; });
        }
        return *this;
    }

    /**
     * @brief Araya yeni bir aşama ve aşamaya özel hata politikası ekler.
     */
    template <typename NextType>
    PipelineBuilderStep<NextType> via(Stage<CurrentType, NextType>* stage,
                                      const ErrorPolicy& policy,
                                      const std::string& customStageName = "",
                                      bool own = true) {
        if (!stage) {
            throw PipelineException("Stage bos (null) olamaz!", "PipelineBuilder");
        }

        stage->onAttachedToPipeline();
        std::string resolvedName = customStageName.empty() ? stage->stageTypeName() : customStageName;

        PipelineBuilderStep<NextType> next;
        next.summary = this->summary;
        next.deadLetterSinkSlot = this->deadLetterSinkSlot;

        size_t stageIdx = next.summary->registerStage(resolvedName, policy);

        next.openHooks = this->openHooks;
        next.openHooks.push_back([stage]() { stage->open(); });

        next.closeHooks = this->closeHooks;
        next.closeHooks.push_back([stage]() { stage->close(); });

        next.deleters = this->deleters;
        if (own) {
            next.deleters.push_back([stage]() { delete stage; });
        }

        auto prevRunner = this->runner;
        auto sumPtr = next.summary;
        auto dlSlot = next.deadLetterSinkSlot;

        next.runner = [prevRunner, stage, policy, resolvedName, stageIdx, sumPtr, dlSlot](Emitter<NextType>& out) {
            struct TopologicalStageRouter : public Emitter<CurrentType> {
                Stage<CurrentType, NextType>* stg;
                Emitter<NextType>& downstream;
                ErrorPolicy pol;
                std::string stgName;
                size_t stgIdx;
                RunSummary* sum;
                Sink<FailedRecord>* dlSink;

                TopologicalStageRouter(Stage<CurrentType, NextType>* s,
                                       Emitter<NextType>& o,
                                       const ErrorPolicy& p,
                                       const std::string& name,
                                       size_t idx,
                                       RunSummary* sm,
                                       Sink<FailedRecord>* dl)
                    : stg(s), downstream(o), pol(p), stgName(name), stgIdx(idx), sum(sm), dlSink(dl) {}

                void emit(const CurrentType& item) override {
                    if (sum) sum->recordStageInput(stgIdx);

                    struct ForwardProbeEmitter : public Emitter<NextType> {
                        Emitter<NextType>& target;
                        RunSummary* s;
                        size_t idx;
                        bool emitted = false;

                        ForwardProbeEmitter(Emitter<NextType>& t, RunSummary* sm, size_t i)
                            : target(t), s(sm), idx(i) {}

                        void emit(const NextType& outItem) override {
                            emitted = true;
                            if (s) s->recordStageOutput(idx);
                            target.emit(outItem);
                        }
                    } probe(downstream, sum, stgIdx);

                    int maxAttempts = pol.isRetry() ? (1 + pol.maxRetries()) : 1;
                    bool succeeded = false;
                    StageException lastEx(stgName, "", "STAGE_ERROR", "Bilinmeyen asama hatasi");

                    for (int attempt = 0; attempt < maxAttempts; ++attempt) {
                        if (attempt > 0 && sum) {
                            sum->recordStageRetry(stgIdx);
                        }
                        try {
                            stg->process(item, probe);
                            succeeded = true;
                            break;
                        } catch (const StageException& ex) {
                            lastEx = ex;
                        }
                    }

                    if (succeeded) {
                        if (!probe.emitted && sum) {
                            if (stgName == "filter") {
                                sum->recordStageFiltered(stgIdx);
                            } else {
                                sum->recordStageSkipped(stgIdx);
                            }
                        }
                        return;
                    }

                    // StageException yakalandı ve tüm RETRY hakları tükendi -> Topolojik Hata Yönlendirmesi
                    ErrorPolicyType effectiveAction = pol.isRetry() ? pol.retryFallback() : pol.type();

                    if (effectiveAction == ErrorPolicyType::FAIL_FAST) {
                        if (sum) sum->recordStageSkipped(stgIdx);
                        throw PipelineException(
                            "[FAIL_FAST] '" + stgName + "' asamasinda kurtarilamaz olarak isaretlenen kayit hatasi: " +
                            lastEx.what(),
                            stgName);
                    } else if (effectiveAction == ErrorPolicyType::SKIP) {
                        if (sum) sum->recordStageSkipped(stgIdx);
                    } else if (effectiveAction == ErrorPolicyType::DEAD_LETTER) {
                        if (sum) sum->recordStageDeadLettered(stgIdx);
                        std::string rawStr = lastEx.getRawInput().empty()
                                                 ? logflow_detail::extractRawInput(item)
                                                 : lastEx.getRawInput();
                        FailedRecord failed(rawStr, stgName, lastEx, std::chrono::system_clock::now());
                        if (dlSink) {
                            dlSink->consume(failed);
                        }
                    }
                }
            } router(stage, out, policy, resolvedName, stageIdx, sumPtr.get(), dlSlot ? *dlSlot : nullptr);

            prevRunner(router);
        };
        return next;
    }

    /**
     * @brief Geriye dönük uyumlu via() aşırı yüklemesi (varsayılan politika: SKIP).
     */
    template <typename NextType>
    PipelineBuilderStep<NextType> via(Stage<CurrentType, NextType>* stage, bool own = true) {
        return via<NextType>(stage, ErrorPolicy::skip(), "", own);
    }

    /**
     * @brief Boru hattını bir ana hedef (`Sink<CurrentType>`) ile sonlandırır ve `Pipeline` üretir.
     */
    Pipeline<void, void> to(Sink<CurrentType>* sink, bool own = true);
};

/**
 * @brief Akıcı (Fluent) Boru Hattı Oluşturucu (PipelineBuilder).
 */
class PipelineBuilder {
public:
    template <typename T>
    static PipelineBuilderStep<T> from(Source<T>* src, bool own = true) {
        if (!src) {
            throw PipelineException("Source bos (null) olamaz!", "PipelineBuilder");
        }

        PipelineBuilderStep<T> step;
        if (own) {
            step.deleters.push_back([src]() { delete src; });
        }
        auto sumPtr = step.summary;
        step.runner = [src, sumPtr](Emitter<T>& out) {
            struct SourceCountingEmitter : public Emitter<T> {
                Emitter<T>& downstream;
                RunSummary* sum;
                SourceCountingEmitter(Emitter<T>& d, RunSummary* s) : downstream(d), sum(s) {}
                void emit(const T& item) override {
                    if (sum) sum->incrementRecordsIn();
                    downstream.emit(item);
                }
            } countingOut(out, sumPtr.get());
            src->produce(countingOut);
        };
        return step;
    }
};
