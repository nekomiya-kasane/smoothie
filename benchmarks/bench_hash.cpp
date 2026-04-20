#include "smoothie/resource/hash.h"

#include <benchmark/benchmark.h>
#include <cstdint>
#include <string>
#include <vector>

using namespace smoothie::resource;

// ── Hash throughput ──────────────────────────────────────────────────────

static void BM_hash64_short(benchmark::State &state) {
    std::string key = "textures/wood.dds";
    for (auto _ : state) {
        benchmark::DoNotOptimize(hash64(key));
    }
    state.SetBytesProcessed(state.iterations() * static_cast<int64_t>(key.size()));
}
BENCHMARK(BM_hash64_short);

static void BM_hash64_medium(benchmark::State &state) {
    std::string key = "locales/zh-Hant-TW/ui/dialogs/preferences/advanced_settings.ftl";
    for (auto _ : state) {
        benchmark::DoNotOptimize(hash64(key));
    }
    state.SetBytesProcessed(state.iterations() * static_cast<int64_t>(key.size()));
}
BENCHMARK(BM_hash64_medium);

static void BM_hash64_long(benchmark::State &state) {
    std::string key(512, 'x');
    for (size_t i = 0; i < 512; ++i) key[i] = static_cast<char>('a' + (i % 26));
    for (auto _ : state) {
        benchmark::DoNotOptimize(hash64(key));
    }
    state.SetBytesProcessed(state.iterations() * static_cast<int64_t>(key.size()));
}
BENCHMARK(BM_hash64_long);

static void BM_hash32_short(benchmark::State &state) {
    std::string key = "count";
    for (auto _ : state) {
        benchmark::DoNotOptimize(hash32(key));
    }
    state.SetBytesProcessed(state.iterations() * static_cast<int64_t>(key.size()));
}
BENCHMARK(BM_hash32_short);

static void BM_hash64_batch(benchmark::State &state) {
    const auto n = state.range(0);
    std::vector<std::string> keys;
    keys.reserve(static_cast<size_t>(n));
    for (int64_t i = 0; i < n; ++i) {
        keys.push_back("resource/item_" + std::to_string(i) + ".bin");
    }
    for (auto _ : state) {
        uint64_t sum = 0;
        for (const auto &k : keys) sum += hash64(k);
        benchmark::DoNotOptimize(sum);
    }
    state.SetItemsProcessed(state.iterations() * n);
}
BENCHMARK(BM_hash64_batch)->Arg(100)->Arg(1000)->Arg(10000);

static void BM_hash64_ns(benchmark::State &state) {
    std::string ns = "core";
    std::string uri = "textures/environment/skybox_hdr.dds";
    for (auto _ : state) {
        benchmark::DoNotOptimize(hash64_ns(ns, uri));
    }
    state.SetBytesProcessed(state.iterations() * static_cast<int64_t>(ns.size() + 1 + uri.size()));
}
BENCHMARK(BM_hash64_ns);
