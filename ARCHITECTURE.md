\# Artım 3: Mimari Dokümantasyon ve Bileşim Deneyi (v3)



\## 1. Mimari Tasarım İlkeleri

\- \*\*Modülerlik ve Tek Sorumluluk:\*\* Pipeline içindeki her aşama (`Stage`) yalnızca tek bir işten sorumludur. `ParserStage` ham metni ayrıştırır, `PredicateFilterStage` filtreleme yapar, `ProjectionStage` alan indirger, `FileSink` ise diske yazar.

\- \*\*Bileşilebilirlik (Composability):\*\* Aşamalar `PipelineBuilder` sayesinde istenen sırada ve sayıda zincirlenebilir.

\- \*\*Yaşam Döngüsü Kancaları (Lifecycle Hooks):\*\* `FileSink::close()` metodu ile arabellekteki (buffer) verilerin diske yazılması (`flush`) garanti altına alınır; böylece veri kaybı önlenir.



\## 2. Sıralama Deneyi ve Ödünleşimler (Trade-offs)



Filtreleme ve projeksiyon aşamalarının sırası, doğruluk ve performansı doğrudan etkiler:



\### Senaryo 1: Filtreleme -> Projeksiyon (Önerilen ve Uygulanan Sıra)

\- \*\*Akış:\*\* `FileLineSource` -> `ParserStage` -> `PredicateFilterStage` -> `ProjectionStage` -> `FileSink`

\- \*\*Performans:\*\* Yüksek. Şartı sağlamayan kayıtlar en başta elendiği için bellek kopyalama maliyeti getiren `ProjectionStage` yalnızca filtreden geçen az sayıdaki kayda uygulanır.

\- \*\*Doğruluk:\*\* Filtre koşulları kaydın tüm orijinal alanlarına eksiksiz erişebilir.



\### Senaryo 2: Projeksiyon -> Filtreleme (Riskli ve Düşük Performans)

\- \*\*Akış:\*\* `FileLineSource` -> `ParserStage` -> `ProjectionStage` -> `PredicateFilterStage` -> `FileSink`

\- \*\*Performans:\*\* Düşük. İlerleyen aşamalarda elenecek olan tüm satırlar için gereksiz yere projeksiyon işlemi çalıştırılır.

\- \*\*Mantıksal Hata Riski:\*\* Eğer `ProjectionStage` bir kayıttan filtre için gerekli bir alanı (örneğin `status` veya `path`) çıkarırsa, sonraki filtre aşaması bu alanı bulamaz ve pipeline hatalı çalışır.



\### Senaryo 3: Filtreleme -> Ayrıştırma (İmkânsız)

\- Ham log satırı `LogRecord` nesnesine dönüştürülmeden sayısal durum kodu (`status >= 400`) veya yol bilgisi filtrelenemez. Bu nedenle `ParserStage` her zaman filtrelerden önce yer almalıdır.

