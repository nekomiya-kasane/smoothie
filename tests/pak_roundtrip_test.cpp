#include "smoothie/resource/compression.h"
#include "smoothie/resource/hash.h"
#include "smoothie/resource/lpak_format.h"
#include "smoothie/resource/pak_reader.h"
#include "smoothie/resource/pak_writer.h"

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

// Helper: make a byte vector from a string
static auto to_bytes(std::string_view s) -> std::vector<std::byte> {
    std::vector<std::byte> v(s.size());
    std::memcpy(v.data(), s.data(), s.size());
    return v;
}

// Helper: read file into byte vector
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

class PakRoundtripFixture : public ::testing::Test {
  protected:
    std::filesystem::path tmp_path_;

    void SetUp() override { tmp_path_ = std::filesystem::temp_directory_path() / "smoothie_test.lpak"; }
    void TearDown() override { std::filesystem::remove(tmp_path_); }
};

TEST_F(PakRoundtripFixture, WriteAndOpenUncompressed) {
    pak_writer w;
    w.add("textures/hero.png", resource_type::image, to_bytes("PNG_DATA_HERE_1234"));
    w.add("config/settings.json", resource_type::config, to_bytes(R"({"key":"value"})"));
    w.add("shaders/basic.glsl", resource_type::shader, to_bytes("void main() {}"));
    EXPECT_EQ(w.entry_count(), 3u);

    auto wr = w.write(tmp_path_);
    ASSERT_TRUE(wr.has_value()) << wr.error().message;

    auto buf = read_file_bytes(tmp_path_);
    ASSERT_GT(buf.size(), sizeof(file_header));

    auto reader = pak_reader::open(buf);
    ASSERT_TRUE(reader.has_value()) << reader.error().message;
    EXPECT_EQ(reader->entry_count(), 3u);
}

TEST_F(PakRoundtripFixture, FindByHash) {
    pak_writer w;
    w.add("textures/hero.png", resource_type::image, to_bytes("HERO_DATA"));
    w.add("fonts/roboto.ttf", resource_type::font, to_bytes("FONT_DATA"));
    auto wr = w.write(tmp_path_);
    ASSERT_TRUE(wr.has_value());

    auto buf = read_file_bytes(tmp_path_);
    auto reader = pak_reader::open(buf);
    ASSERT_TRUE(reader.has_value());

    auto h1 = hash64("textures/hero.png");
    auto entry = reader->find(h1);
    ASSERT_NE(entry, nullptr);

    auto h2 = hash64("fonts/roboto.ttf");
    entry = reader->find(h2);
    ASSERT_NE(entry, nullptr);

    // Not found
    auto h3 = hash64("nonexistent/path");
    entry = reader->find(h3);
    EXPECT_EQ(entry, nullptr);
}

TEST_F(PakRoundtripFixture, DataOfRawBytes) {
    auto content = to_bytes("Hello, resource data!");
    pak_writer w;
    w.add("data/test.bin", resource_type::raw, content);
    auto wr = w.write(tmp_path_);
    ASSERT_TRUE(wr.has_value());

    auto buf = read_file_bytes(tmp_path_);
    auto reader = pak_reader::open(buf);
    ASSERT_TRUE(reader.has_value());

    auto entry = reader->find(hash64("data/test.bin"));
    ASSERT_NE(entry, nullptr);

    auto raw = reader->data_of(*entry);
    ASSERT_TRUE(raw.has_value()) << static_cast<int>(raw.error());
    EXPECT_EQ(raw->size(), content.size());
    EXPECT_TRUE(std::equal(raw->begin(), raw->end(), content.begin()));
}

TEST_F(PakRoundtripFixture, DataOfViewDecompressesTransparently) {
    // Use highly compressible data so LZ4 output is smaller than input
    std::string repeated;
    for (int i = 0; i < 200; ++i) {
        repeated += "ABCDEFGH";
    }
    auto content = to_bytes(repeated);

    pak_writer w;
    w.add("docs/readme.txt", resource_type::raw, content, compression_mode::lz4);
    auto wr = w.write(tmp_path_);
    ASSERT_TRUE(wr.has_value());

    auto buf = read_file_bytes(tmp_path_);
    auto reader = pak_reader::open(buf);
    ASSERT_TRUE(reader.has_value());

    auto entry = reader->find(hash64("docs/readme.txt"));
    ASSERT_NE(entry, nullptr);
    EXPECT_TRUE(has_flag(static_cast<entry_flags>(entry->flags), entry_flags::compressed));

    auto view = reader->data_of_view(*entry);
    ASSERT_TRUE(view.has_value()) << view.error().message;
    EXPECT_EQ(view->size(), content.size());
    EXPECT_EQ(view->as_string_view(), repeated);
}

TEST_F(PakRoundtripFixture, ZstdCompressedRoundTrip) {
    // Use highly compressible data so Zstd output is smaller than input
    std::string repeated;
    for (int i = 0; i < 200; ++i) {
        repeated += "ZSTDTEST";
    }
    auto content = to_bytes(repeated);
    pak_writer w;
    w.add("data/zstd.bin", resource_type::raw, content, compression_mode::zstd);
    auto wr = w.write(tmp_path_);
    ASSERT_TRUE(wr.has_value());

    auto buf = read_file_bytes(tmp_path_);
    auto reader = pak_reader::open(buf);
    ASSERT_TRUE(reader.has_value());

    auto entry = reader->find(hash64("data/zstd.bin"));
    ASSERT_NE(entry, nullptr);

    auto view = reader->data_of_view(*entry);
    ASSERT_TRUE(view.has_value()) << view.error().message;
    EXPECT_EQ(view->size(), content.size());
    EXPECT_EQ(std::memcmp(view->data().data(), content.data(), content.size()), 0);
}

TEST_F(PakRoundtripFixture, ValidateIndexChecksum) {
    pak_writer w;
    w.add("a", resource_type::raw, to_bytes("aaa"));
    w.add("b", resource_type::raw, to_bytes("bbb"));
    auto wr = w.write(tmp_path_);
    ASSERT_TRUE(wr.has_value());

    auto buf = read_file_bytes(tmp_path_);
    auto reader = pak_reader::open(buf);
    ASSERT_TRUE(reader.has_value());
    EXPECT_TRUE(reader->validate_index_checksum());
}

TEST_F(PakRoundtripFixture, IterateEntries) {
    pak_writer w;
    w.add("e1", resource_type::raw, to_bytes("1"));
    w.add("e2", resource_type::image, to_bytes("22"));
    w.add("e3", resource_type::font, to_bytes("333"));
    auto wr = w.write(tmp_path_);
    ASSERT_TRUE(wr.has_value());

    auto buf = read_file_bytes(tmp_path_);
    auto reader = pak_reader::open(buf);
    ASSERT_TRUE(reader.has_value());

    size_t count = 0;
    for (const auto &e : *reader) {
        (void)e;
        ++count;
    }
    EXPECT_EQ(count, 3u);
}

TEST_F(PakRoundtripFixture, HeaderAccess) {
    pak_writer w;
    w.add("test", resource_type::raw, to_bytes("data"));
    auto wr = w.write(tmp_path_);
    ASSERT_TRUE(wr.has_value());

    auto buf = read_file_bytes(tmp_path_);
    auto reader = pak_reader::open(buf);
    ASSERT_TRUE(reader.has_value());

    auto *hdr = reader->header();
    ASSERT_NE(hdr, nullptr);
    EXPECT_EQ(hdr->entry_count, 1u);
    EXPECT_EQ(std::memcmp(hdr->magic, "LPAK", 4), 0);
}

TEST_F(PakRoundtripFixture, OpenCorruptedMagic) {
    pak_writer w;
    w.add("x", resource_type::raw, to_bytes("y"));
    auto wr = w.write(tmp_path_);
    ASSERT_TRUE(wr.has_value());

    auto buf = read_file_bytes(tmp_path_);
    // Corrupt the magic bytes
    buf[0] = std::byte{0xFF};
    buf[1] = std::byte{0xFF};

    auto reader = pak_reader::open(buf);
    EXPECT_FALSE(reader.has_value());
}

TEST_F(PakRoundtripFixture, OpenEmptyBuffer) {
    std::vector<std::byte> empty;
    auto reader = pak_reader::open(empty);
    EXPECT_FALSE(reader.has_value());
}

TEST_F(PakRoundtripFixture, OpenTruncatedBuffer) {
    std::vector<std::byte> tiny(10, std::byte{0});
    auto reader = pak_reader::open(tiny);
    EXPECT_FALSE(reader.has_value());
}

TEST_F(PakRoundtripFixture, ManyEntries) {
    pak_writer w;
    constexpr int N = 200;
    for (int i = 0; i < N; ++i) {
        auto path = "item/" + std::to_string(i);
        auto data = to_bytes("data_" + std::to_string(i));
        w.add(path, resource_type::raw, data);
    }
    auto wr = w.write(tmp_path_);
    ASSERT_TRUE(wr.has_value());

    auto buf = read_file_bytes(tmp_path_);
    auto reader = pak_reader::open(buf);
    ASSERT_TRUE(reader.has_value());
    EXPECT_EQ(reader->entry_count(), static_cast<uint32_t>(N));

    // Verify every entry is findable
    for (int i = 0; i < N; ++i) {
        auto path = "item/" + std::to_string(i);
        auto entry = reader->find(hash64(path));
        EXPECT_NE(entry, nullptr) << "Missing: " << path;
    }
}

TEST_F(PakRoundtripFixture, DefaultCompression) {
    pak_writer w;
    w.set_default_compression(compression_mode::lz4);
    auto content = to_bytes("repeated repeated repeated repeated repeated repeated repeated!!");
    w.add("auto_compressed", resource_type::raw, content);
    auto wr = w.write(tmp_path_);
    ASSERT_TRUE(wr.has_value());

    auto buf = read_file_bytes(tmp_path_);
    auto reader = pak_reader::open(buf);
    ASSERT_TRUE(reader.has_value());

    auto entry = reader->find(hash64("auto_compressed"));
    ASSERT_NE(entry, nullptr);
    EXPECT_TRUE(has_flag(static_cast<entry_flags>(entry->flags), entry_flags::compressed));

    auto view = reader->data_of_view(*entry);
    ASSERT_TRUE(view.has_value()) << view.error().message;
    EXPECT_EQ(view->size(), content.size());
}
