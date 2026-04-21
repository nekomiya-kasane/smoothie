#include "smoothie/resource/compression.h"
#include "smoothie/resource/hash.h"
#include "smoothie/resource/pak_writer.h"
#include "smoothie/resource/vfs.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <string>
#include <string_view>
#include <vector>

using namespace smoothie::resource;

static auto to_bytes(std::string_view s) -> std::vector<std::byte> {
    std::vector<std::byte> v(s.size());
    std::memcpy(v.data(), s.data(), s.size());
    return v;
}

static auto read_file_bytes(const std::filesystem::path &p) -> std::vector<std::byte> {
    std::ifstream f(p, std::ios::binary | std::ios::ate);
    if (!f) {
        return {};
    }
    auto sz = static_cast<size_t>(f.tellg());
    f.seekg(0);
    std::vector<std::byte> buf(sz);
    f.read(reinterpret_cast<char *>(buf.data()), static_cast<std::streamsize>(sz));
    return buf;
}

class VfsFixture : public ::testing::Test {
  protected:
    std::filesystem::path pak1_path_, pak2_path_;

    void SetUp() override {
        pak1_path_ = std::filesystem::temp_directory_path() / "smoothie_vfs_test1.lpak";
        pak2_path_ = std::filesystem::temp_directory_path() / "smoothie_vfs_test2.lpak";

        {
            pak_writer w;
            w.add("textures/hero.png", resource_type::image, to_bytes("HERO_IMG_DATA"));
            w.add("config/settings.json", resource_type::config, to_bytes(R"({"key":"val1"})"));
            w.add("shared/common.txt", resource_type::raw, to_bytes("shared_from_pak1"));
            auto r = w.write(pak1_path_);
            ASSERT_TRUE(r.has_value()) << r.error().message;
        }
        {
            pak_writer w;
            w.add("fonts/roboto.ttf", resource_type::font, to_bytes("FONT_TTF_DATA"));
            w.add("shared/common.txt", resource_type::raw, to_bytes("shared_from_pak2"));
            auto r = w.write(pak2_path_);
            ASSERT_TRUE(r.has_value()) << r.error().message;
        }
    }

    void TearDown() override {
        std::filesystem::remove(pak1_path_);
        std::filesystem::remove(pak2_path_);
    }
};

TEST_F(VfsFixture, MountAndGet) {
    vfs v;
    EXPECT_EQ(v.mount_count(), 0u);
    EXPECT_EQ(v.resource_count(), 0u);

    auto r = v.mount("core", pak1_path_);
    ASSERT_TRUE(r.has_value()) << r.error().message;
    EXPECT_EQ(v.mount_count(), 1u);
    EXPECT_EQ(v.resource_count(), 3u);

    auto data = v.get(hash64("textures/hero.png"));
    ASSERT_TRUE(data.has_value());
    std::string_view sv(reinterpret_cast<const char *>(data->data()), data->size());
    EXPECT_EQ(sv, "HERO_IMG_DATA");
}

TEST_F(VfsFixture, MountMemoryBuffer) {
    auto buf = read_file_bytes(pak1_path_);
    vfs v;
    auto r = v.mount("mem", std::move(buf));
    ASSERT_TRUE(r.has_value()) << r.error().message;
    EXPECT_EQ(v.mount_count(), 1u);

    EXPECT_TRUE(v.contains(hash64("textures/hero.png")));
    EXPECT_FALSE(v.contains(hash64("nonexistent")));
}

TEST_F(VfsFixture, MountEmbedded) {
    auto buf = read_file_bytes(pak1_path_);
    vfs v;
    auto r = v.mount_embedded("embedded", buf);
    ASSERT_TRUE(r.has_value()) << r.error().message;
    EXPECT_EQ(v.mount_count(), 1u);

    auto data = v.get(hash64("config/settings.json"));
    ASSERT_TRUE(data.has_value());
}

TEST_F(VfsFixture, MountMmap) {
    vfs v;
    auto r = v.mount_mmap("mmapped", pak1_path_);
    ASSERT_TRUE(r.has_value()) << r.error().message;
    EXPECT_EQ(v.mount_count(), 1u);

    auto data = v.get(hash64("textures/hero.png"));
    ASSERT_TRUE(data.has_value());
    std::string_view sv(reinterpret_cast<const char *>(data->data()), data->size());
    EXPECT_EQ(sv, "HERO_IMG_DATA");
}

TEST_F(VfsFixture, Unmount) {
    vfs v;
    v.mount("core", pak1_path_);
    EXPECT_EQ(v.mount_count(), 1u);

    auto r = v.unmount("core");
    ASSERT_TRUE(r.has_value()) << r.error().message;
    EXPECT_EQ(v.mount_count(), 0u);
    EXPECT_EQ(v.resource_count(), 0u);
}

TEST_F(VfsFixture, UnmountNotFound) {
    vfs v;
    auto r = v.unmount("nope");
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, smoothie::error_code::not_mounted);
}

TEST_F(VfsFixture, DuplicateMountName) {
    vfs v;
    v.mount("core", pak1_path_);
    auto r = v.mount("core", pak2_path_);
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, smoothie::error_code::already_mounted);
}

TEST_F(VfsFixture, PriorityOverride) {
    vfs v;
    v.mount("low", pak1_path_, 0);
    v.mount("high", pak2_path_, 10);

    // "shared/common.txt" exists in both; higher priority should win
    auto data = v.get(hash64("shared/common.txt"));
    ASSERT_TRUE(data.has_value());
    std::string_view sv(reinterpret_cast<const char *>(data->data()), data->size());
    EXPECT_EQ(sv, "shared_from_pak2");
}

TEST_F(VfsFixture, GetNotFound) {
    vfs v;
    v.mount("core", pak1_path_);
    auto data = v.get(hash64("nonexistent/path"));
    EXPECT_FALSE(data.has_value());
    EXPECT_EQ(data.error(), smoothie::error_code::not_found);
}

TEST_F(VfsFixture, Contains) {
    vfs v;
    v.mount("core", pak1_path_);
    EXPECT_TRUE(v.contains(hash64("textures/hero.png")));
    EXPECT_FALSE(v.contains(hash64("nope")));
}

TEST_F(VfsFixture, MountNames) {
    vfs v;
    v.mount("alpha", pak1_path_);
    v.mount("beta", pak2_path_);
    auto names = v.mount_names();
    EXPECT_EQ(names.size(), 2u);
    // order matches insertion order
    EXPECT_EQ(names[0], "alpha");
    EXPECT_EQ(names[1], "beta");
}

TEST_F(VfsFixture, Enumerate) {
    vfs v;
    v.mount("core", pak1_path_);
    auto hashes = v.enumerate();
    EXPECT_EQ(hashes.size(), 3u); // 3 entries in pak1
}

TEST_F(VfsFixture, MountInfo) {
    vfs v;
    v.mount("core", pak1_path_, 5);
    auto infos = v.mount_info();
    ASSERT_EQ(infos.size(), 1u);
    EXPECT_EQ(infos[0].name, "core");
    EXPECT_EQ(infos[0].priority, 5);
    EXPECT_EQ(infos[0].resource_count, 3u);
    EXPECT_GT(infos[0].data_size, 0u);
    EXPECT_FALSE(infos[0].is_embedded);
    EXPECT_FALSE(infos[0].is_mmap);
}

TEST_F(VfsFixture, GetView) {
    vfs v;
    v.mount("core", pak1_path_);
    auto view = v.get_view(hash64("config/settings.json"));
    ASSERT_TRUE(view.has_value()) << view.error().message;
    EXPECT_EQ(view->as_string_view(), R"({"key":"val1"})");
}

TEST_F(VfsFixture, GetViewNotFound) {
    vfs v;
    v.mount("core", pak1_path_);
    auto view = v.get_view(hash64("nope"));
    EXPECT_FALSE(view.has_value());
}

TEST_F(VfsFixture, GetEntryInfo) {
    vfs v;
    v.mount("core", pak1_path_);
    auto info = v.get_entry_info(hash64("textures/hero.png"));
    ASSERT_TRUE(info.has_value());
    EXPECT_EQ(info->semantic_hash, hash64("textures/hero.png"));
    EXPECT_GT(info->data_size, 0u);
}

TEST_F(VfsFixture, GetEntryInfoNotFound) {
    vfs v;
    v.mount("core", pak1_path_);
    auto info = v.get_entry_info(hash64("nope"));
    EXPECT_FALSE(info.has_value());
}

TEST_F(VfsFixture, GetDynamicResProtocol) {
    vfs v;
    v.mount("core", pak1_path_);

    // res://core/textures/hero.png — mount-specific lookup
    auto view = v.get_dynamic("res://core/textures/hero.png");
    ASSERT_TRUE(view.has_value());
    EXPECT_EQ(view->as_string_view(), "HERO_IMG_DATA");

    // res://textures/hero.png — fallback to hash lookup
    auto view2 = v.get_dynamic("res://textures/hero.png");
    ASSERT_TRUE(view2.has_value());
    EXPECT_EQ(view2->as_string_view(), "HERO_IMG_DATA");
}

TEST_F(VfsFixture, GetDynamicFileProtocol) {
    // Write a temp file and read it via file:// URI
    auto tmp = std::filesystem::temp_directory_path() / "smoothie_vfs_dyn_test.txt";
    {
        std::ofstream out(tmp);
        out << "dynamic file content";
    }

    vfs v;
    auto view = v.get_dynamic("file://" + tmp.string());
    ASSERT_TRUE(view.has_value());
    EXPECT_EQ(view->as_string_view(), "dynamic file content");
    std::filesystem::remove(tmp);
}

TEST_F(VfsFixture, RebuildIndex) {
    vfs v;
    v.mount("core", pak1_path_);
    // rebuild_index should not crash or change resource count
    v.rebuild_index();
    EXPECT_EQ(v.resource_count(), 3u);
}

TEST_F(VfsFixture, StatsAndReset) {
    vfs v;
    auto s = v.stats();
    // Stats should be zero-initialized
    (void)s;
    v.reset_stats();
}

TEST_F(VfsFixture, WatchUnwatch) {
    vfs v;
    auto token = v.watch([](std::string_view) {});
    v.unwatch(token);
}

TEST_F(VfsFixture, MoveConstruct) {
    vfs v1;
    v1.mount("core", pak1_path_);
    EXPECT_EQ(v1.mount_count(), 1u);

    vfs v2(std::move(v1));
    EXPECT_EQ(v2.mount_count(), 1u);
    EXPECT_TRUE(v2.contains(hash64("textures/hero.png")));
}
