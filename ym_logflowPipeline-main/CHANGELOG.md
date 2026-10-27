# Değişiklik Günlüğü (Change Log)

**Dönem Projesi:** LogFlow İşlem Hattı (Pipe and Filter)  
**Öğrenci:** YunusEmre Demirci (2404040249)  

---

## [v5] Sürüm 5.0 (Artım 5 — Hata Yönetimi, Doğrulama ve Dead-Letter Kanalı)
**Tarih:** Ekim 2026  
**Etiket:** `v5`  
**Mimari Odak:** Hata yönetimini mimari ve topolojik bir konu olarak ele alma, ikincil kanallar (Dead-Letter Channel), bildirimsel doğrulama (`ValidationStage`), çalıştırma özeti (`RunSummary`) ve kaos testi (`data/access-dirty.log`).

### 1. Yeni Eklenen Dosyalar
- [`PipelineExceptions.hpp`](file:///home/yunus/Desktop/yazılım_mimarisi/ym_logflowPipeline/PipelineExceptions.hpp): Kurtarılabilir kayıt hatası (`StageException`) ve kurtarılamaz işlem hattı hatası (`PipelineException`).
- [`FailedRecord.hpp`](file:///home/yunus/Desktop/yazılım_mimarisi/ym_logflowPipeline/FailedRecord.hpp): Başarısız kaydın tanılama bağlamını (`rawInput`, `stageName`, `exception`, `timestamp`) taşıyan `FailedRecord` sınıfı.
- [`ErrorPolicy.hpp`](file:///home/yunus/Desktop/yazılım_mimarisi/ym_logflowPipeline/ErrorPolicy.hpp): Aşama başına ayarlanabilir hata politikaları (`FAIL_FAST`, `SKIP`, `DEAD_LETTER`, `RETRY(n)`).
- [`DeadLetterSink.hpp`](file:///home/yunus/Desktop/yazılım_mimarisi/ym_logflowPipeline/DeadLetterSink.hpp): İkincil çıktı kanalı hedefleri (`DeadLetterFileSink`, `DeadLetterConsoleSink`, `DeadLetterMemorySink`).
- [`ValidationStage.hpp`](file:///home/yunus/Desktop/yazılım_mimarisi/ym_logflowPipeline/ValidationStage.hpp): Zorunlu alanlar, sayısal aralıklar, zaman damgası tutarlılığı (1970/2099) ve IPv4 biçimini denetleyen bildirimsel doğrulama aşaması.
- [`RunSummary.hpp`](file:///home/yunus/Desktop/yazılım_mimarisi/ym_logflowPipeline/RunSummary.hpp): Gelen/çıkan kayıt, aşama bazında atılan kayıt, dead-letter sayısı ve gerçek geçen süre (8–9. hafta ölçüm temeli) özeti.
- [`data/access-dirty.log`](file:///home/yunus/Desktop/yazılım_mimarisi/ym_logflowPipeline/data/access-dirty.log) & [`data/empty.log`](file:///home/yunus/Desktop/yazılım_mimarisi/ym_logflowPipeline/data/empty.log): Kesilmiş satırlar, UTF-8 olmayan baytlar, negatif bayt sayıları, 1970/2099 zaman damgaları, 100 KB tek satır ve boş dosya içeren kaos testi verileri.
- [`configs/chaos-test.yaml`](file:///home/yunus/Desktop/yazılım_mimarisi/ym_logflowPipeline/configs/chaos-test.yaml) & [`configs/empty-file-test.yaml`](file:///home/yunus/Desktop/yazılım_mimarisi/ym_logflowPipeline/configs/empty-file-test.yaml): Dead-Letter kanalı ve doğrulama kurallarını kullanan kaos testi yapılandırmaları.

### 2. Değiştirilen Dosyalar
- [`ParserStage.hpp`](file:///home/yunus/Desktop/yazılım_mimarisi/ym_logflowPipeline/ParserStage.hpp): UTF-8 bayt geçerliliği (`NON_UTF8_BYTES`), 100 KB aşırı uzun satır koruması (`OVERSIZED_LINE`), kesilmiş satır (`TRUNCATED_LINE`) ve `StageException` fırlatma desteği eklendi.
- [`PipelineBuilder.hpp`](file:///home/yunus/Desktop/yazılım_mimarisi/ym_logflowPipeline/PipelineBuilder.hpp) & [`Pipeline.hpp`](file:///home/yunus/Desktop/yazılım_mimarisi/ym_logflowPipeline/Pipeline.hpp): Topolojik hata yönlendirici (`TopologicalStageRouter`), ikincil `withDeadLetterSink` bağlantısı ve `RunSummary` entegrasyonu eklendi.
- [`StageFactory.hpp`](file:///home/yunus/Desktop/yazılım_mimarisi/ym_logflowPipeline/StageFactory.hpp), [`StageRegistry.hpp`](file:///home/yunus/Desktop/yazılım_mimarisi/ym_logflowPipeline/StageRegistry.hpp) & [`PipelineLoader.hpp`](file:///home/yunus/Desktop/yazılım_mimarisi/ym_logflowPipeline/PipelineLoader.hpp): `validate` aşaması, aşama başına `policy`/`onError` ayrıştırması ve `deadLetter` hedef yükleme desteği eklendi.
- [`ARCHITECTURE.md`](file:///home/yunus/Desktop/yazılım_mimarisi/ym_logflowPipeline/ARCHITECTURE.md): `StageException` vs `PipelineException`, ikincil Dead-Letter boru hattı mimari diyagramı ve kaos testi hata-politika eşleştirme tablosu eklendi.
- [`tests/test_runner.cpp`](file:///home/yunus/Desktop/yazılım_mimarisi/ym_logflowPipeline/tests/test_runner.cpp): Test sayısı 42'den 52'ye çıkarıldı.

---

## [v4] Sürüm 4.0 (Artım 4 — Yapılandırma Odaklı Birleştirme ve Aşama Kaydı)
**Tarih:** Ekim 2026  
**Etiket:** `v4`  
- `StageFactory`, `StageRegistry` ve `PipelineLoader` ile YAML tabanlı bildirimsel boru hattı birleştirme eklendi.

---

## [v3] Sürüm 3.0 (Artım 3 — Bileşim: Filtreleme, Projeksiyon ve Pipeline Builder)
**Tarih:** Ekim 2026  
**Etiket:** `v3`  
- `PredicateFilterStage`, `ProjectionStage`, `FileSink` ve `PipelineBuilder` eklendi.

---

## [v2] Sürüm 2.0 (Artım 2 — Tipli Kayıtlar ve İlk Gerçek Filtre)
**Tarih:** Ekim 2026  
**Etiket:** `v2`  
- `LogRecord` alan modeli, Builder deseni ve `ParserStage` CLF ayrıştırıcısı eklendi.

---

## [v1.0] Sürüm 1.0 (Artım 1 — Boru Hattı Başlangıcı)
**Tarih:** Eylül 2026  
**Etiket:** `v1.0`  
- `Source`, `Sink`, `Stage`, `Emitter` temel arayüzleri kuruldu.
