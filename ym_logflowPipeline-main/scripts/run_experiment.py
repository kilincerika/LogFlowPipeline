#!/usr/bin/env python3
"""
LogFlow Artım 3 - Sıralama ve Bileşilebilirlik Deneyi (Ordering Experiment)

Bu betik, aynı log veri kümesi üzerinde iki farklı pipeline sıralamasını
deneysel olarak karşılaştırır:
  Sıra A (Filter-First):  Parser -> Filter -> Projection -> FileSink
  Sıra B (Project-First): Parser -> Projection -> Filter -> FileSink

Doğrulamalar:
1. İki sıranın ürettiği çıktıların birebir (byte-for-byte) aynı olduğunu kanıtlar (diff / SHA256).
2. İki sıranın yürütme sürelerini (çalışma zamanı, CPU süresi, saniyedeki kayıt işleme kapasitesi) ölçer.
3. Mimari ödünleşim (trade-off) analizini istatistiksel verilerle ortaya koyar.
"""

import os
import sys
import time
import subprocess
import hashlib
import random

BASE_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
BIN_PATH = os.path.join(BASE_DIR, "logflow")
DATA_DIR = os.path.join(BASE_DIR, "data")
OUTPUT_DIR = os.path.join(BASE_DIR, "output")
BENCHMARK_LOG = os.path.join(DATA_DIR, "benchmark_25k.log")

OUT_A = os.path.join(OUTPUT_DIR, "bench_out_filter_first.log")
OUT_B = os.path.join(OUTPUT_DIR, "bench_out_project_first.log")

RECORD_COUNT = 25_000
REPEAT_RUNS = 3

def generate_benchmark_data(path, count):
    if os.path.exists(path) and os.path.getsize(path) > 0:
        print(f"[*] Benchmark verisi zaten mevcut: {path} ({count:,} kayit)")
        return

    print(f"[*] {count:,} satirlik sentetik CLF log verisi uretiliyor: {path}...")
    os.makedirs(os.path.dirname(path), exist_ok=True)

    ips = [f"192.168.1.{i}" for i in range(1, 255)] + [f"10.0.{i}.{j}" for i in range(1, 10) for j in range(1, 20)]
    methods = ["GET", "POST", "PUT", "DELETE"]
    paths = [
        "/index.html", "/api/v1/users", "/api/v1/auth", "/api/v2/products",
        "/static/style.css", "/static/bundle.js", "/images/logo.png",
        "/api/orders", "/dashboard", "/api/settings", "/about", "/contact"
    ]
    # Filtrelenecek durum kodları (status >= 400 ve /api kombinasyonu yaklaşık %15-20 oranında)
    statuses = [200, 200, 200, 200, 200, 204, 301, 302, 400, 401, 403, 404, 500, 502, 503]
    user_agents = [
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 Chrome/117.0.0.0 Safari/537.36",
        "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/605.1.15 Safari/605.1.15",
        "curl/7.88.1",
        "PostmanRuntime/7.32.3"
    ]

    with open(path, "w", encoding="utf-8") as f:
        for _ in range(count):
            ip = random.choice(ips)
            method = random.choice(methods)
            p = random.choice(paths)
            status = random.choice(statuses)
            bytes_sent = random.randint(128, 65536) if status != 204 else 0
            ua = random.choice(user_agents)
            day = random.randint(1, 28)
            hour = random.randint(0, 23)
            minute = random.randint(0, 59)
            second = random.randint(0, 59)
            ts = f"{day:02d}/Sep/2026:{hour:02d}:{minute:02d}:{second:02d} +0300"
            line = f'{ip} - - [{ts}] "{method} {p} HTTP/1.1" {status} {bytes_sent} "{ua}"\n'
            f.write(line)

    print(f"[+] Veri uretimi tamamlandi. Boyut: {os.path.getsize(path) / (1024*1024):.2f} MB")

def run_pipeline(order, in_path, out_path):
    cmd = [
        BIN_PATH,
        in_path,
        "--min-status", "400",
        "--path-prefix", "/api",
        "--fields", "timestamp,clientIp,status,path",
        "--order", order,
        "--out", out_path
    ]
    t0 = time.perf_counter()
    res = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    t1 = time.perf_counter()
    if res.returncode != 0:
        print(f"Hata olustu ({order}):", res.stderr)
        sys.exit(1)
    return (t1 - t0)

def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        while chunk := f.read(65536):
            h.update(chunk)
    return h.hexdigest()

def main():
    if not os.path.exists(BIN_PATH):
        print(f"[*] {BIN_PATH} derleniyor...")
        subprocess.run(["make", "-C", BASE_DIR], check=True)

    os.makedirs(OUTPUT_DIR, exist_ok=True)
    generate_benchmark_data(BENCHMARK_LOG, RECORD_COUNT)

    print("\n" + "=" * 65)
    print(" LOGFLOW ARTIM 3 - SIRALAMA VE BILESILEBILIRLIK DENEYI")
    print("=" * 65)
    print(f"Girdi Boyutu: {RECORD_COUNT:,} kayit")
    print("Uygulanan Filtre: min-status >= 400 AND path-prefix = /api")
    print("Uygulanan Projeksiyon: timestamp,clientIp,status,path")
    print(f"Tekrar Sayisi: {REPEAT_RUNS} kosu")
    print("-" * 65)

    times_a = []
    times_b = []

    print("[*] Sira A (Filter-First: Parser -> Filter -> Project -> Sink) calistiriliyor...")
    for i in range(REPEAT_RUNS):
        dur = run_pipeline("filter-first", BENCHMARK_LOG, OUT_A)
        times_a.append(dur)
        print(f"  Kosu {i+1}: {dur*1000:.2f} ms ({RECORD_COUNT/dur:,.0f} kayit/sn)")

    print("\n[*] Sira B (Project-First: Parser -> Project -> Filter -> Sink) calistiriliyor...")
    for i in range(REPEAT_RUNS):
        dur = run_pipeline("project-first", BENCHMARK_LOG, OUT_B)
        times_b.append(dur)
        print(f"  Kosu {i+1}: {dur*1000:.2f} ms ({RECORD_COUNT/dur:,.0f} kayit/sn)")

    print("\n" + "-" * 65)
    print("1. CIKTI DOGRULUK VE BILESILEBILIRLIK KANITI (EQUIVALENCE TEST):")
    hash_a = sha256_file(OUT_A)
    hash_b = sha256_file(OUT_B)
    size_a = os.path.getsize(OUT_A)
    size_b = os.path.getsize(OUT_B)

    print(f"  Sira A Dosya Boyutu: {size_a:,} bayt | SHA-256: {hash_a}")
    print(f"  Sira B Dosya Boyutu: {size_b:,} bayt | SHA-256: {hash_b}")

    if hash_a == hash_b:
        print("  [DOGRULANDI]: Her iki sira da BYTE-FOR-BYTE BIREBIR AYNI CIKTIYI uretmistir!")
    else:
        print("  [HATA]: Ciktilar esit degil!")
        sys.exit(1)

    with open(OUT_A, "r") as f:
        filtered_lines = sum(1 for _ in f)
    print(f"  Girdi Satir Sayisi   : {RECORD_COUNT:,}")
    print(f"  Filtrelenen / Kalan  : {filtered_lines:,} ({filtered_lines/RECORD_COUNT*100:.2f}%)")
    print(f"  Elenen Satir Sayisi  : {RECORD_COUNT - filtered_lines:,} ({(1 - filtered_lines/RECORD_COUNT)*100:.2f}%)")

    print("\n" + "-" * 65)
    print("2. PERFORMANS VE ODUNLESIM (TRADE-OFF) ANALIZI:")
    avg_a = sum(times_a) / len(times_a)
    avg_b = sum(times_b) / len(times_b)
    speedup = ((avg_b - avg_a) / avg_b) * 100
    ratio = avg_b / avg_a

    print(f"  Sira A Ortalama Sure : {avg_a*1000:.2f} ms | Gecis: {RECORD_COUNT/avg_a:,.0f} kayit/sn")
    print(f"  Sira B Ortalama Sure : {avg_b*1000:.2f} ms | Gecis: {RECORD_COUNT/avg_b:,.0f} kayit/sn")
    print(f"  Fark                 : {abs(avg_b - avg_a)*1000:.2f} ms")
    print(f"  Filter-First Kazanci : %{speedup:.2f} daha hizli ({ratio:.2f}x hiz artisi)")
    print("=" * 65)

    return {
        "record_count": RECORD_COUNT,
        "filtered_count": filtered_lines,
        "hash_a": hash_a,
        "hash_b": hash_b,
        "avg_a_ms": avg_a * 1000,
        "avg_b_ms": avg_b * 1000,
        "throughput_a": RECORD_COUNT / avg_a,
        "throughput_b": RECORD_COUNT / avg_b,
        "speedup_pct": speedup,
        "ratio": ratio
    }

if __name__ == "__main__":
    main()
