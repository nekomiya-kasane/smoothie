#include "smoothie/resource/hash.h"
#include "smoothie/resource/pak_reader.h"
#include "smoothie/resource/pak_writer.h"

#include <benchmark/benchmark.h>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

using namespace smoothie::resource;
namespace fs = std::filesystem;

namespace {

auto bench_tmp_dir() -> fs::path {
    auto p = fs::temp_directory_path() / "smoothie_bench";
    fs::create_directories(p);
    return p;
}

auto make_bytes(std::string_view s) -> std::vector<std::byte> {
    std::vector<std::byte> v(s.size());
    for (size_t i = 0; i < s.size(); ++i) v[i] = static_cast<std::byte>(s[i]);
    return v;
}

auto read_file_bytes(const fs::path &p) -> std::vector<std::byte> {
    std::ifstream f(p, std::ios::binary | std::ios::ate);
    auto sz = f.tellg();
    f.seekg(0);
    std::vector<std::byte> buf(static_cast<size_t>(sz));
    f.read(reinterpret_cast<char *>(buf.data()), sz);
    return buf;
}

struct pak_fixture {
    std::vector<std::byte> file_bytes;
    std::vector<uint64_t> hashes;

    explicit pak_fixture(int n) {
        auto dir = bench_tmp_dir();
        auto pak_path = dir / ("bench_pak_" + std::to_string(n) + ".lpak");

        pak_writer writer;
        hashes.reserve(static_cast<size_t>(n));
        for (int i = 0; i < n; ++i) {
            auto name = "entry_" + std::to_string(i) + ".bin";
            writer.add(name, resource_type::raw, make_bytes("d"));
            hashes.push_back(hash64(name));
        }
        auto wr = writer.write(pak_path);
        if (!wr) throw std::runtime_error("write failed");
        file_bytes = read_file_bytes(pak_path);
    }
};

auto &get_pak_fixture(int n) {
    static std::unique_ptr<pak_fixture> f100, f1k, f10k;
    auto &ptr = (n <= 100) ? f100 : (n <= 1000) ? f1k : f10k;
    if (!ptr) ptr = std::make_unique<pak_fixture>(n);
    return *ptr;
}

} // namespace

// ── pak_reader::open (index construction) ────────────────────────────────

static void BM_pak_reader_open(benchmark::State &state) {
    auto &fix = get_pak_fixture(static_cast<int>(state.range(0)));
    for (auto _ : state) {
        auto rr = pak_reader::open(fix.file_bytes);
        benchmark::DoNotOptimize(rr);
    }
    state.SetItemsProcessed(state.iterations());
    state.SetLabel(std::to_string(state.range(0)) + " entries");
}
BENCHMARK(BM_pak_reader_open)->Arg(100)->Arg(1000)->Arg(10000)->MinTime(0.1);

// ── pak_reader::find (lookup) ────────────────────────────────────────────

static void BM_pak_reader_find(benchmark::State &state) {
    auto &fix = get_pak_fixture(static_cast<int>(state.range(0)));
    auto rr = pak_reader::open(fix.file_bytes);
    if (!rr) {
        state.SkipWithError("open failed");
        return;
    }

    size_t idx = 0;
    for (auto _ : state) {
        benchmark::DoNotOptimize(rr->find(fix.hashes[idx]));
        idx = (idx + 1) % fix.hashes.size();
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_pak_reader_find)->Arg(100)->Arg(1000)->Arg(10000);

// ── pak_reader::data_of ──────────────────────────────────────────────────

static void BM_pak_reader_data_of(benchmark::State &state) {
    auto &fix = get_pak_fixture(1000);
    auto rr = pak_reader::open(fix.file_bytes);
    if (!rr) {
        state.SkipWithError("open failed");
        return;
    }

    auto entries = rr->entries();
    size_t idx = 0;
    for (auto _ : state) {
        benchmark::DoNotOptimize(rr->data_of(entries[idx]));
        idx = (idx + 1) % entries.size();
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_pak_reader_data_of);
