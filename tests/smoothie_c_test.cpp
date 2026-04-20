#include "smoothie/resource/hash.h"
#include "smoothie/resource/pak_writer.h"
#include "smoothie/smoothie_c.h"

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

// ── Version / error_message ─────────────────────────────────────────

TEST(SmoothieC, ApiVersion) {
    auto ver = smoothie_api_version();
    EXPECT_EQ(ver, SMOOTHIE_C_API_VERSION);
}

TEST(SmoothieC, ErrorMessage) {
    auto msg = smoothie_error_message(SMOOTHIE_OK);
    ASSERT_NE(msg, nullptr);
    EXPECT_STREQ(msg, "ok");

    msg = smoothie_error_message(SMOOTHIE_ERROR_NOT_FOUND);
    ASSERT_NE(msg, nullptr);
    EXPECT_STREQ(msg, "not found");

    msg = smoothie_error_message(SMOOTHIE_ERROR_NULL_POINTER);
    ASSERT_NE(msg, nullptr);
}

// ── Hash ────────────────────────────────────────────────────────────

TEST(SmoothieC, Hash64) {
    const char *s = "textures/hero.png";
    auto h = smoothie_hash64(s, std::strlen(s));
    EXPECT_EQ(h, hash64("textures/hero.png"));
}

// ── pak_writer C API ────────────────────────────────────────────────

class SmcPakWriterFixture : public ::testing::Test {
  protected:
    std::filesystem::path tmp_path_;
    void SetUp() override { tmp_path_ = std::filesystem::temp_directory_path() / "smoothie_c_pak_test.lpak"; }
    void TearDown() override { std::filesystem::remove(tmp_path_); }
};

TEST_F(SmcPakWriterFixture, CreateAndDestroy) {
    smoothie_pak_writer *w = nullptr;
    auto rc = smoothie_pak_writer_create(&w);
    ASSERT_EQ(rc, SMOOTHIE_OK);
    ASSERT_NE(w, nullptr);
    EXPECT_EQ(smoothie_pak_writer_entry_count(w), 0u);
    smoothie_pak_writer_destroy(w);
}

TEST_F(SmcPakWriterFixture, CreateNullPtr) {
    auto rc = smoothie_pak_writer_create(nullptr);
    EXPECT_EQ(rc, SMOOTHIE_ERROR_NULL_POINTER);
}

TEST_F(SmcPakWriterFixture, AddAndWrite) {
    smoothie_pak_writer *w = nullptr;
    smoothie_pak_writer_create(&w);

    const char *data1 = "hello";
    auto rc = smoothie_pak_writer_add(w, "test/file.txt", SMOOTHIE_RESOURCE_RAW, data1, 5);
    EXPECT_EQ(rc, SMOOTHIE_OK);
    EXPECT_EQ(smoothie_pak_writer_entry_count(w), 1u);

    const char *data2 = "world";
    rc = smoothie_pak_writer_add(w, "test/file2.txt", SMOOTHIE_RESOURCE_CONFIG, data2, 5);
    EXPECT_EQ(rc, SMOOTHIE_OK);
    EXPECT_EQ(smoothie_pak_writer_entry_count(w), 2u);

    rc = smoothie_pak_writer_write(w, tmp_path_.string().c_str());
    EXPECT_EQ(rc, SMOOTHIE_OK);

    EXPECT_TRUE(std::filesystem::exists(tmp_path_));
    EXPECT_GT(std::filesystem::file_size(tmp_path_), 0u);

    smoothie_pak_writer_destroy(w);
}

TEST_F(SmcPakWriterFixture, AddNullPtr) {
    auto rc = smoothie_pak_writer_add(nullptr, "x", SMOOTHIE_RESOURCE_RAW, "y", 1);
    EXPECT_EQ(rc, SMOOTHIE_ERROR_NULL_POINTER);

    smoothie_pak_writer *w = nullptr;
    smoothie_pak_writer_create(&w);
    rc = smoothie_pak_writer_add(w, nullptr, SMOOTHIE_RESOURCE_RAW, "y", 1);
    EXPECT_EQ(rc, SMOOTHIE_ERROR_NULL_POINTER);
    rc = smoothie_pak_writer_add(w, "x", SMOOTHIE_RESOURCE_RAW, nullptr, 1);
    EXPECT_EQ(rc, SMOOTHIE_ERROR_NULL_POINTER);
    smoothie_pak_writer_destroy(w);
}

TEST_F(SmcPakWriterFixture, SetCompression) {
    smoothie_pak_writer *w = nullptr;
    smoothie_pak_writer_create(&w);
    smoothie_pak_writer_set_compression(w, SMOOTHIE_COMPRESS_LZ4);
    // Just verify no crash — compression is tested at C++ level
    smoothie_pak_writer_destroy(w);
}

// ── VFS C API ───────────────────────────────────────────────────────

class SmcVfsFixture : public ::testing::Test {
  protected:
    std::filesystem::path pak_path_;
    void SetUp() override {
        pak_path_ = std::filesystem::temp_directory_path() / "smoothie_c_vfs_test.lpak";
        pak_writer w;
        w.add("textures/hero.png", resource_type::image, to_bytes("HERO_DATA"));
        w.add("config/app.json", resource_type::config, to_bytes(R"({"v":1})"));
        auto r = w.write(pak_path_);
        ASSERT_TRUE(r.has_value()) << r.error().message;
    }
    void TearDown() override { std::filesystem::remove(pak_path_); }
};

TEST_F(SmcVfsFixture, CreateAndDestroy) {
    smoothie_vfs *v = nullptr;
    auto rc = smoothie_vfs_create(&v);
    ASSERT_EQ(rc, SMOOTHIE_OK);
    ASSERT_NE(v, nullptr);
    EXPECT_EQ(smoothie_vfs_mount_count(v), 0u);
    EXPECT_EQ(smoothie_vfs_resource_count(v), 0u);
    smoothie_vfs_destroy(v);
}

TEST_F(SmcVfsFixture, CreateNullPtr) {
    auto rc = smoothie_vfs_create(nullptr);
    EXPECT_EQ(rc, SMOOTHIE_ERROR_NULL_POINTER);
}

TEST_F(SmcVfsFixture, MountAndGet) {
    smoothie_vfs *v = nullptr;
    smoothie_vfs_create(&v);

    auto rc = smoothie_vfs_mount(v, "core", pak_path_.string().c_str(), 0);
    EXPECT_EQ(rc, SMOOTHIE_OK);
    EXPECT_EQ(smoothie_vfs_mount_count(v), 1u);
    EXPECT_EQ(smoothie_vfs_resource_count(v), 2u);

    auto h = hash64("textures/hero.png");
    const void *out_data = nullptr;
    size_t out_size = 0;
    rc = smoothie_vfs_get(v, h, &out_data, &out_size);
    EXPECT_EQ(rc, SMOOTHIE_OK);
    EXPECT_EQ(out_size, 9u); // "HERO_DATA"
    EXPECT_EQ(std::memcmp(out_data, "HERO_DATA", 9), 0);

    smoothie_vfs_destroy(v);
}

TEST_F(SmcVfsFixture, MountMemory) {
    smoothie_vfs *v = nullptr;
    smoothie_vfs_create(&v);

    std::ifstream f(pak_path_, std::ios::binary | std::ios::ate);
    auto sz = static_cast<size_t>(f.tellg());
    f.seekg(0);
    std::vector<char> buf(sz);
    f.read(buf.data(), static_cast<std::streamsize>(sz));

    auto rc = smoothie_vfs_mount_memory(v, "mem", buf.data(), buf.size(), 0);
    EXPECT_EQ(rc, SMOOTHIE_OK);
    EXPECT_EQ(smoothie_vfs_mount_count(v), 1u);

    smoothie_vfs_destroy(v);
}

TEST_F(SmcVfsFixture, Contains) {
    smoothie_vfs *v = nullptr;
    smoothie_vfs_create(&v);
    smoothie_vfs_mount(v, "core", pak_path_.string().c_str(), 0);

    EXPECT_EQ(smoothie_vfs_contains(v, hash64("textures/hero.png")), 1);
    EXPECT_EQ(smoothie_vfs_contains(v, hash64("nope")), 0);

    smoothie_vfs_destroy(v);
}

TEST_F(SmcVfsFixture, Unmount) {
    smoothie_vfs *v = nullptr;
    smoothie_vfs_create(&v);
    smoothie_vfs_mount(v, "core", pak_path_.string().c_str(), 0);
    EXPECT_EQ(smoothie_vfs_mount_count(v), 1u);

    auto rc = smoothie_vfs_unmount(v, "core");
    EXPECT_EQ(rc, SMOOTHIE_OK);
    EXPECT_EQ(smoothie_vfs_mount_count(v), 0u);

    rc = smoothie_vfs_unmount(v, "core");
    EXPECT_EQ(rc, SMOOTHIE_ERROR_NOT_MOUNTED);

    smoothie_vfs_destroy(v);
}

TEST_F(SmcVfsFixture, GetNotFound) {
    smoothie_vfs *v = nullptr;
    smoothie_vfs_create(&v);
    smoothie_vfs_mount(v, "core", pak_path_.string().c_str(), 0);

    const void *out_data = nullptr;
    size_t out_size = 0;
    auto rc = smoothie_vfs_get(v, hash64("nope"), &out_data, &out_size);
    EXPECT_EQ(rc, SMOOTHIE_ERROR_NOT_FOUND);

    smoothie_vfs_destroy(v);
}

TEST_F(SmcVfsFixture, GetNullPtrArgs) {
    smoothie_vfs *v = nullptr;
    smoothie_vfs_create(&v);
    smoothie_vfs_mount(v, "core", pak_path_.string().c_str(), 0);

    size_t out_size = 0;
    auto rc = smoothie_vfs_get(v, hash64("textures/hero.png"), nullptr, &out_size);
    EXPECT_EQ(rc, SMOOTHIE_ERROR_NULL_POINTER);

    const void *out_data = nullptr;
    rc = smoothie_vfs_get(v, hash64("textures/hero.png"), &out_data, nullptr);
    EXPECT_EQ(rc, SMOOTHIE_ERROR_NULL_POINTER);

    smoothie_vfs_destroy(v);
}

TEST_F(SmcVfsFixture, DestroyNull) {
    // Should not crash
    smoothie_vfs_destroy(nullptr);
    smoothie_pak_writer_destroy(nullptr);
}
