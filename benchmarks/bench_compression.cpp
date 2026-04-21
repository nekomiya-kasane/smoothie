#include "smoothie/resource/compression.h"

#include <benchmark/benchmark.h>
#include <cstddef>
#include <vector>

using namespace smoothie::resource;

#if defined(SMOOTHIE_HAS_COMPRESSION)

namespace {

    std::vector<std::byte> make_compressible(size_t size) {
        std::vector<std::byte> v(size);
        for (size_t i = 0; i < size; ++i) {
            v[i] = static_cast<std::byte>(i % 13);
        }
        return v;
    }

    std::vector<std::byte> make_random_ish(size_t size) {
        std::vector<std::byte> v(size);
        uint32_t state = 0xDEADBEEF;
        for (size_t i = 0; i < size; ++i) {
            state = state * 1664525u + 1013904223u;
            v[i] = static_cast<std::byte>(state >> 24);
        }
        return v;
    }

} // namespace

// ── LZ4 compress ────────────────────────────────────────────────────────

static void BM_lz4_compress(benchmark::State &state) {
    auto data = make_compressible(static_cast<size_t>(state.range(0)));
    for (auto _ : state) {
        auto r = compress(data, compression_mode::lz4);
        benchmark::DoNotOptimize(r);
    }
    state.SetBytesProcessed(state.iterations() * state.range(0));
}
BENCHMARK(BM_lz4_compress)->Arg(1024)->Arg(4096)->Arg(65536)->Arg(1048576);

// ── LZ4 decompress ─────────────────────────────────────────────────────

static void BM_lz4_decompress(benchmark::State &state) {
    auto data = make_compressible(static_cast<size_t>(state.range(0)));
    auto compressed = compress(data, compression_mode::lz4);
    auto flags = entry_flags::compressed | entry_flags::lz4;
    auto orig_size = static_cast<uint32_t>(data.size());
    for (auto _ : state) {
        auto r = decompress(*compressed, orig_size, flags);
        benchmark::DoNotOptimize(r);
    }
    state.SetBytesProcessed(state.iterations() * state.range(0));
}
BENCHMARK(BM_lz4_decompress)->Arg(1024)->Arg(4096)->Arg(65536)->Arg(1048576);

// ── Zstd compress ───────────────────────────────────────────────────────

static void BM_zstd_compress(benchmark::State &state) {
    auto data = make_compressible(static_cast<size_t>(state.range(0)));
    for (auto _ : state) {
        auto r = compress(data, compression_mode::zstd);
        benchmark::DoNotOptimize(r);
    }
    state.SetBytesProcessed(state.iterations() * state.range(0));
}
BENCHMARK(BM_zstd_compress)->Arg(1024)->Arg(4096)->Arg(65536)->Arg(1048576);

// ── Zstd decompress ─────────────────────────────────────────────────────

static void BM_zstd_decompress(benchmark::State &state) {
    auto data = make_compressible(static_cast<size_t>(state.range(0)));
    auto compressed = compress(data, compression_mode::zstd);
    auto flags = entry_flags::compressed | entry_flags::zstd;
    auto orig_size = static_cast<uint32_t>(data.size());
    for (auto _ : state) {
        auto r = decompress(*compressed, orig_size, flags);
        benchmark::DoNotOptimize(r);
    }
    state.SetBytesProcessed(state.iterations() * state.range(0));
}
BENCHMARK(BM_zstd_decompress)->Arg(1024)->Arg(4096)->Arg(65536)->Arg(1048576);

// ── LZ4 decompress_into (zero-allocation) ──────────────────────────────

static void BM_lz4_decompress_into(benchmark::State &state) {
    auto data = make_compressible(static_cast<size_t>(state.range(0)));
    auto compressed = compress(data, compression_mode::lz4);
    auto flags = entry_flags::compressed | entry_flags::lz4;
    std::vector<std::byte> out(data.size());
    for (auto _ : state) {
        auto r = decompress_into(*compressed, std::span<std::byte>(out), flags);
        benchmark::DoNotOptimize(r);
    }
    state.SetBytesProcessed(state.iterations() * state.range(0));
}
BENCHMARK(BM_lz4_decompress_into)->Arg(1024)->Arg(4096)->Arg(65536)->Arg(1048576);

// ── Zstd decompress_into (zero-allocation) ─────────────────────────────

static void BM_zstd_decompress_into(benchmark::State &state) {
    auto data = make_compressible(static_cast<size_t>(state.range(0)));
    auto compressed = compress(data, compression_mode::zstd);
    auto flags = entry_flags::compressed | entry_flags::zstd;
    std::vector<std::byte> out(data.size());
    for (auto _ : state) {
        auto r = decompress_into(*compressed, std::span<std::byte>(out), flags);
        benchmark::DoNotOptimize(r);
    }
    state.SetBytesProcessed(state.iterations() * state.range(0));
}
BENCHMARK(BM_zstd_decompress_into)->Arg(1024)->Arg(4096)->Arg(65536)->Arg(1048576);

// ── LZ4 decompress_reuse (amortized zero-allocation) ───────────────────

static void BM_lz4_decompress_reuse(benchmark::State &state) {
    auto data = make_compressible(static_cast<size_t>(state.range(0)));
    auto compressed = compress(data, compression_mode::lz4);
    auto flags = entry_flags::compressed | entry_flags::lz4;
    auto orig_size = static_cast<uint32_t>(data.size());
    std::vector<std::byte> reuse_buf;
    for (auto _ : state) {
        auto r = decompress_reuse(*compressed, orig_size, flags, reuse_buf);
        benchmark::DoNotOptimize(r);
        benchmark::DoNotOptimize(reuse_buf.data());
    }
    state.SetBytesProcessed(state.iterations() * state.range(0));
}
BENCHMARK(BM_lz4_decompress_reuse)->Arg(1024)->Arg(4096)->Arg(65536)->Arg(1048576);

// ── Zstd decompress_reuse (amortized zero-allocation) ──────────────────

static void BM_zstd_decompress_reuse(benchmark::State &state) {
    auto data = make_compressible(static_cast<size_t>(state.range(0)));
    auto compressed = compress(data, compression_mode::zstd);
    auto flags = entry_flags::compressed | entry_flags::zstd;
    auto orig_size = static_cast<uint32_t>(data.size());
    std::vector<std::byte> reuse_buf;
    for (auto _ : state) {
        auto r = decompress_reuse(*compressed, orig_size, flags, reuse_buf);
        benchmark::DoNotOptimize(r);
        benchmark::DoNotOptimize(reuse_buf.data());
    }
    state.SetBytesProcessed(state.iterations() * state.range(0));
}
BENCHMARK(BM_zstd_decompress_reuse)->Arg(1024)->Arg(4096)->Arg(65536)->Arg(1048576);

// ── Random data (worst-case compression) ────────────────────────────────

static void BM_lz4_compress_random(benchmark::State &state) {
    auto data = make_random_ish(static_cast<size_t>(state.range(0)));
    for (auto _ : state) {
        auto r = compress(data, compression_mode::lz4);
        benchmark::DoNotOptimize(r);
    }
    state.SetBytesProcessed(state.iterations() * state.range(0));
}
BENCHMARK(BM_lz4_compress_random)->Arg(65536)->Arg(1048576);

static void BM_zstd_compress_random(benchmark::State &state) {
    auto data = make_random_ish(static_cast<size_t>(state.range(0)));
    for (auto _ : state) {
        auto r = compress(data, compression_mode::zstd);
        benchmark::DoNotOptimize(r);
    }
    state.SetBytesProcessed(state.iterations() * state.range(0));
}
BENCHMARK(BM_zstd_compress_random)->Arg(65536)->Arg(1048576);

#endif // SMOOTHIE_HAS_COMPRESSION
