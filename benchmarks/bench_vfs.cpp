#include "smoothie/resource/hash.h"
#include "smoothie/resource/pak_writer.h"
#include "smoothie/resource/vfs.h"

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
    for (size_t i = 0; i < s.size(); ++i) {
        v[i] = static_cast<std::byte>(s[i]);
    }
    return v;
}

struct vfs_fixture {
    vfs filesystem;
    std::vector<uint64_t> hashes;
    std::vector<std::string> uris;

    explicit vfs_fixture(int n) {
        auto dir = bench_tmp_dir();
        auto pak_path = dir / ("bench_vfs_" + std::to_string(n) + ".lpak");

        pak_writer writer;
        hashes.reserve(static_cast<size_t>(n));
        uris.reserve(static_cast<size_t>(n));
        for (int i = 0; i < n; ++i) {
            auto name = "resource/item_" + std::to_string(i) + ".bin";
            auto payload = make_bytes("data_payload_" + std::to_string(i));
            writer.add(name, resource_type::raw, payload);
            hashes.push_back(hash64(name));
            uris.push_back("res://" + name);
        }
        auto r = writer.write(pak_path);
        if (!r) {
            throw std::runtime_error("pak_writer::write failed");
        }
        auto mr = filesystem.mount("bench", pak_path);
        if (!mr) {
            throw std::runtime_error("vfs::mount failed");
        }
    }
};

auto &get_vfs_fixture(int n) {
    static std::unique_ptr<vfs_fixture> f100, f1k, f10k;
    auto &ptr = (n <= 100) ? f100 : (n <= 1000) ? f1k : f10k;
    if (!ptr) {
        ptr = std::make_unique<vfs_fixture>(n);
    }
    return *ptr;
}

} // namespace

// ── VFS get(hash) latency ────────────────────────────────────────────────

static void BM_vfs_get(benchmark::State &state) {
    auto &fix = get_vfs_fixture(static_cast<int>(state.range(0)));
    size_t idx = 0;
    for (auto _ : state) {
        benchmark::DoNotOptimize(fix.filesystem.get(fix.hashes[idx]));
        idx = (idx + 1) % fix.hashes.size();
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_vfs_get)->Arg(100)->Arg(1000)->Arg(10000);

// ── VFS get_dynamic(uri) latency ─────────────────────────────────────────

static void BM_vfs_get_dynamic(benchmark::State &state) {
    auto &fix = get_vfs_fixture(1000);
    size_t idx = 0;
    for (auto _ : state) {
        benchmark::DoNotOptimize(fix.filesystem.get_dynamic(fix.uris[idx % 100]));
        idx = (idx + 1);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_vfs_get_dynamic);

// ── VFS contains() ───────────────────────────────────────────────────────

static void BM_vfs_contains(benchmark::State &state) {
    auto &fix = get_vfs_fixture(1000);
    size_t idx = 0;
    for (auto _ : state) {
        benchmark::DoNotOptimize(fix.filesystem.contains(fix.hashes[idx]));
        idx = (idx + 1) % fix.hashes.size();
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_vfs_contains);

// ── Bulk VFS lookup throughput ───────────────────────────────────────────

static void BM_bulk_vfs_get(benchmark::State &state) {
    auto &fix = get_vfs_fixture(static_cast<int>(state.range(0)));
    for (auto _ : state) {
        int found = 0;
        for (const auto h : fix.hashes) {
            if (fix.filesystem.get(h).has_value()) {
                ++found;
            }
        }
        benchmark::DoNotOptimize(found);
    }
    state.SetItemsProcessed(state.iterations() * static_cast<int64_t>(fix.hashes.size()));
}
BENCHMARK(BM_bulk_vfs_get)->Arg(1000)->Arg(10000);

// ── VFS mount latency ────────────────────────────────────────────────────

static void BM_vfs_mount(benchmark::State &state) {
    auto dir = bench_tmp_dir();
    auto pak_path = dir / "bench_mount.lpak";
    {
        pak_writer writer;
        for (int i = 0; i < 1000; ++i) {
            auto name = "mount/item_" + std::to_string(i) + ".bin";
            writer.add(name, resource_type::raw, make_bytes("data"));
        }
        auto r = writer.write(pak_path);
        if (!r) {
            throw std::runtime_error("write failed");
        }
    }

    for (auto _ : state) {
        vfs fs;
        auto r = fs.mount("bench", pak_path);
        benchmark::DoNotOptimize(r);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_vfs_mount)->MinTime(0.5);
