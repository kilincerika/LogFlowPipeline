#pragma once

#include <stdexcept>
#include <string>

/**
 * @brief Kurtarılamaz İşlem Hattı Hatası (PipelineException).
 *
 * Artım 5 Görev 1:
 * İşlem hattının çalışmaya devam edemeyeceği sistem/topoloji düzeyindeki
 * kurtarılamaz (unrecoverable) hataları temsil eder:
 * - Yapılandırma hataları (ConfigValidationException bu sınıftan türetilir)
 * - Okunamayan kaynak dosya (FileLineSource açılamaması)
 * - Hedef dosyanın açılamaması veya diskin dolması (FileSink / DeadLetterSink I/O hatası)
 * - Bir aşamada FAIL_FAST hata politikasının tetiklenmesi
 */
class PipelineException : public std::runtime_error {
private:
    std::string componentName_;

public:
    explicit PipelineException(const std::string& message, std::string componentName = "pipeline")
        : std::runtime_error(message), componentName_(std::move(componentName)) {}

    ~PipelineException() noexcept override = default;

    const std::string& getComponentName() const noexcept { return componentName_; }
};

/**
 * @brief Kurtarılabilir Aşama Kayıt Hatası (StageException).
 *
 * Artım 5 Görev 1:
 * Bir aşamada (Stage) tek bir kaydın işlenmesi sırasında oluşan ve işlem hattının
 * tamamını çökertmek yerine aşamanın hata politikasına (FAIL_FAST, SKIP,
 * DEAD_LETTER, RETRY(n)) göre yönetilebilen kurtarılabilir (recoverable) hatadır.
 *
 * Örnekler:
 * - Kesilmiş (truncated) log satırı
 * - UTF-8 olmayan bayt dizisi
 * - 100 KB'lık aşırı uzun satır
 * - Negatif bayt sayısı, 1970/2099 tarihli zaman damgası, geçersiz IP biçimi
 */
class StageException : public std::runtime_error {
private:
    std::string stageName_;
    std::string rawInput_;
    std::string errorCode_;

public:
    StageException(std::string stageName,
                   std::string rawInput,
                   std::string errorCode,
                   const std::string& message)
        : std::runtime_error("[" + stageName + ":" + errorCode + "] " + message),
          stageName_(std::move(stageName)),
          rawInput_(std::move(rawInput)),
          errorCode_(std::move(errorCode)) {}

    StageException(std::string stageName,
                   std::string rawInput,
                   const std::string& message)
        : std::runtime_error("[" + stageName + "] " + message),
          stageName_(std::move(stageName)),
          rawInput_(std::move(rawInput)),
          errorCode_("STAGE_ERROR") {}

    ~StageException() noexcept override = default;

    const std::string& getStageName() const noexcept { return stageName_; }
    const std::string& getRawInput() const noexcept { return rawInput_; }
    const std::string& getErrorCode() const noexcept { return errorCode_; }
};
