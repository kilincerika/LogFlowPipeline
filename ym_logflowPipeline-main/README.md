# LogFlow - Yazılım Mimarisi Dönem Projesi
## Artım 5: Hata Yönetimi, Doğrulama ve Dead-Letter Kanalı ★★★

- **Öğrenci Adı Soyadı:** YunusEmre Demirci  
- **Öğrenci Numarası:** 2404040249  
- **Ders:** Yazılım Tasarımı ve Mimarisi  
- **Sürüm Etiketi:** `v5`  
- **GitHub Deposu:** [DolphinBlacksmith/ym_logflowPipeline](https://github.com/DolphinBlacksmith/ym_logflowPipeline)

---

## 1. Mimari Odak ve Hedefler

LogFlow projesinin 5. Artımında hata yönetimi mimari bir konu olarak ele alınmış, ikincil kanallar (Dead-Letter) ve hata yalıtımı uygulanmıştır:

1. **Hata Modeli (`StageException` vs `PipelineException`):** Tek bir kayıt işlenirken oluşan kurtarılabilir hatalar (`StageException`) ile işlem hattının devam edemeyeceği kurtarılamaz sistem/yapılandırma/G-Ç hataları (`PipelineException`) birbirinden ayrılmıştır.
2. **Aşama Başına Ayarlanabilir Hata Politikaları (`ErrorPolicy`):** Her aşama için YAML yapılandırma dosyasında `FAIL_FAST`, `SKIP`, `DEAD_LETTER` ve `RETRY(n)` politikaları seçilebilir hale getirilmiştir.
3. **Topolojik Hata Yönlendirmesi ve Dead-Letter Kanalı (`FailedRecord` & `DeadLetterSink`):** Hatalı kayıtlar aşama içindeki `try/catch` ile sessizce yutulmaz; topolojik yönlendirici tarafından `FailedRecord(rawInput, stageName, exception, timestamp)` nesnesine dönüştürülerek bağımsız ikincil çıktı kanalına (`DeadLetterFileSink` / `DeadLetterConsoleSink`) yönlendirilir.
4. **Bildirimsel Doğrulama Aşaması (`ValidationStage`):** Zorunlu alanlar (`requiredFields`), sayısal aralıklar (`statusRange`, `bytesRange`), zaman damgası tutarlılığı (`timestampYearRange`) ve IP biçimi (`validateIp`) kurallarını bildirimsel olarak denetler.
5. **Çalıştırma Özeti (`RunSummary`) ve Kaos Testi (`data/access-dirty.log`):** Her çalıştırmanın sonunda gelen kayıt sayısı, çıkan kayıt sayısı, aşama bazında atılan kayıt sayısı, dead-letter kanalına gönderilen kayıt sayısı ve gerçek geçen süre (wall-clock time + throughput) tam mutabakatla raporlanır.

---

## 2. Artım 5 Yeni ve Güncellenen Bileşenler

| Bileşen / Dosya | Sorumluluk ve Mimari Rol |
|---|---|
| [`PipelineExceptions.hpp`](file:///home/yunus/Desktop/yazılım_mimarisi/ym_logflowPipeline/PipelineExceptions.hpp) | Kurtarılabilir aşama kayıt hatası (`StageException`) ve kurtarılamaz işlem hattı hatası (`PipelineException`) sınıflarını tanımlar. |
| [`FailedRecord.hpp`](file:///home/yunus/Desktop/yazılım_mimarisi/ym_logflowPipeline/FailedRecord.hpp) | Dead-letter kanalında taşınan `FailedRecord(rawInput, stageName, exception, timestamp)` tanılama modelini tanımlar. |
| [`ErrorPolicy.hpp`](file:///home/yunus/Desktop/yazılım_mimarisi/ym_logflowPipeline/ErrorPolicy.hpp) | `FAIL_FAST`, `SKIP`, `DEAD_LETTER` ve `RETRY(n)` aşama hata politikalarını temsil eder ve YAML'dan ayrıştırır. |
| [`DeadLetterSink.hpp`](file:///home/yunus/Desktop/yazılım_mimarisi/ym_logflowPipeline/DeadLetterSink.hpp) | `FailedRecord` nesnelerini ikincil hedefe yazan `DeadLetterFileSink`, `DeadLetterConsoleSink` ve `DeadLetterMemorySink` sınıflarını içerir. |
| [`ValidationStage.hpp`](file:///home/yunus/Desktop/yazılım_mimarisi/ym_logflowPipeline/ValidationStage.hpp) | Zorunlu alanlar, sayısal aralıklar (negatif bayt tespiti), zaman damgası tutarlılığı (1970 ve 2099 tespiti) ve IPv4 biçimi doğrulamasını uygular. |
| [`RunSummary.hpp`](file:///home/yunus/Desktop/yazılım_mimarisi/ym_logflowPipeline/RunSummary.hpp) | Gelen/çıkan kayıt, aşama başına atılan kayıt, dead-letter sayıları, gerçek geçen süre ve saniye başına kayıt hızı (8–9. hafta temeli) özetini üretir. |
| [`configs/chaos-test.yaml`](file:///home/yunus/Desktop/yazılım_mimarisi/ym_logflowPipeline/configs/chaos-test.yaml) | Bozuk log dosyasını (`data/access-dirty.log`) işleyip geçerli kayıtları `out/clean-access.log` ana hedefine, hatalı kayıtları `out/dead-letter.log` hedefine yönlendiren yapılandırma. |
| [`data/access-dirty.log`](file:///home/yunus/Desktop/yazılım_mimarisi/ym_logflowPipeline/data/access-dirty.log) | Kesilmiş satırlar, UTF-8 olmayan baytlar, negatif bayt sayıları, 1970 ve 2099 zaman damgaları, 100 KB'lık tek satır ve geçersiz IP içeren kaos testi verisi. |

---

## 3. Çalıştırma Kılavuzu

```bash
# Projeyi derle
make

# Kaos Testi: Bozuk log dosyasını (data/access-dirty.log) işle
./logflow --config configs/chaos-test.yaml

# Ana hedefe ulaşan temiz kayıtları görüntüle (3 geçerli kayıt)
cat out/clean-access.log

# Dead-Letter hedefine yönlendirilen hatalı kayıtları görüntüle (8 hatalı kayıt)
cat out/dead-letter.log

# Boş dosya kaos testini (data/access-empty.log) çalıştır
./logflow --config configs/empty-file-test.yaml

# 52 Birim, Entegrasyon ve Kaos Testini Çalıştır
make test
```

### Kaos Testi Örnek Çalıştırma Özeti (`./logflow --config configs/chaos-test.yaml`)

```text
----------------------------------------------------------------------
 CALISTIRMA OZETI (RunSummary) - [chaos-resilience-pipeline]
----------------------------------------------------------------------
 Gelen Kayit Sayisi (recordsIn)        : 11
 Cikan Kayit Sayisi (recordsOut)       : 3
 Dead-Letter Kanalina Giden Kayit      : 8
 Atlanan / Elenen Kayit (SKIP/Filter)  : 0 (SKIP: 0, Filter: 0)
 Asama Bazinda Atilan Kayit Sayisi     :
   - [1] parse      (Politika: DEAD_LETTER) : 5 atildi [DEAD_LETTER: 5, SKIP: 0]
   - [2] validate   (Politika: DEAD_LETTER) : 3 atildi [DEAD_LETTER: 3, SKIP: 0]
   - [3] project    (Politika: SKIP       ) : 0 atildi [DEAD_LETTER: 0, SKIP: 0]
 Sayisal Mutabakat (In == Out + Drop)  : DOGRULANDI (TAM TUTARLI) [In(11) == Out(3) + Skipped(0) + DeadLetter(8)]
 Gercek Gecen Sure (Wall-Clock Time)   : 2.340 ms (2340 us)
 Isleme Hizi (Throughput - 8/9. Hafta) : 4700.9 kayit/sn
----------------------------------------------------------------------
```
