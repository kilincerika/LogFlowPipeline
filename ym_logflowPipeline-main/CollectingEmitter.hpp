#pragma once

#include "Emitter.hpp"
#include <vector>

/**
 * @brief Test Dublörü (Test Double) - Toplayıcı Emitter (Collecting Emitter).
 * 
 * Mimari Önemi:
 * Boru hattı aşamalarının birim testlerinde (unit testing) dosya sistemine veya
 * harici I/O hedeflerine dokunulmaması esastır. Bu sınıf, Emitter<T> arayüzünü
 * uygulayarak aşama tarafından yayılan (emit edilen) tüm çıktıları bellek içi
 * bir vektörde toplar.
 * 
 * Böylece testler:
 * 1. Dosya sistemi oluşturma/silme yükünden kurtulur.
 * 2. Milisaniyeler içinde deterministik olarak koşar.
 * 3. Yayılan kayıtların adedini ve içeriğini doğrudan doğrulamayı sağlar.
 * 
 * Öğrenciler bu test dublörünü sonraki tüm haftalarda yeniden kullanacaktır.
 */
template <typename T>
class CollectingEmitter : public Emitter<T> {
private:
    std::vector<T> collectedItems;

public:
    CollectingEmitter() = default;
    ~CollectingEmitter() override = default;

    /**
     * @brief Emitter arayüzünden gelen nesneyi vektörde saklar.
     */
    void emit(const T& item) override {
        collectedItems.push_back(item);
    }

    /**
     * @brief Toplanan tüm öğelerin listesini döndürür.
     */
    const std::vector<T>& getItems() const {
        return collectedItems;
    }

    /**
     * @brief Toplanan öğe sayısını döndürür.
     */
    size_t size() const {
        return collectedItems.size();
    }

    /**
     * @brief Toplanan öğe olup olmadığını kontrol eder.
     */
    bool empty() const {
        return collectedItems.empty();
    }

    /**
     * @brief İndeksteki öğeye erişim sağlar.
     */
    const T& operator[](size_t index) const {
        return collectedItems[index];
    }

    /**
     * @brief Toplanan öğeleri temizler.
     */
    void clear() {
        collectedItems.clear();
    }
};
