#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <format>
#include <string>
#include <string_view>
#include <vector>

#include "smoothie/types.h"
#include "smoothie/resource/lpak_format.h"
#include "smoothie/resource/hash.h"

using namespace smoothie;
using namespace smoothie::resource;

// ── error_code to_string_view ───────────────────────────────────────────

TEST(Types, ErrorCodeToStringView) {
    EXPECT_EQ(to_string_view(error_code::ok), "ok");
    EXPECT_EQ(to_string_view(error_code::not_found), "not_found");
    EXPECT_EQ(to_string_view(error_code::corrupted), "corrupted");
    EXPECT_EQ(to_string_view(error_code::mmap_failed), "mmap_failed");
    EXPECT_EQ(to_string_view(error_code::version_mismatch), "version_mismatch");
    EXPECT_EQ(to_string_view(error_code::already_mounted), "already_mounted");
    EXPECT_EQ(to_string_view(error_code::not_mounted), "not_mounted");
    EXPECT_EQ(to_string_view(error_code::io_error), "io_error");
    EXPECT_EQ(to_string_view(error_code::invalid_argument), "invalid_argument");
}

TEST(Types, ErrorCodeFormatter) {
    auto s = std::format("{}", error_code::not_found);
    EXPECT_EQ(s, "not_found");

    s = std::format("{}", error_code::corrupted);
    EXPECT_EQ(s, "corrupted");
}

// ── resource_view ───────────────────────────────────────────────────────

TEST(ResourceView, BorrowedView) {
    std::string data = "hello borrowed view";
    auto sp = std::span<const std::byte>(
        reinterpret_cast<const std::byte*>(data.data()), data.size());
    resource_view rv(sp);

    EXPECT_FALSE(rv.empty());
    EXPECT_EQ(rv.size(), data.size());
    EXPECT_FALSE(rv.is_owned());
    EXPECT_EQ(rv.as_string_view(), data);
    EXPECT_EQ(rv.as_string(), data);
}

TEST(ResourceView, OwnedView) {
    std::string data = "hello owned view";
    std::vector<std::byte> vec(data.size());
    std::memcpy(vec.data(), data.data(), data.size());
    resource_view rv(std::move(vec));

    EXPECT_FALSE(rv.empty());
    EXPECT_EQ(rv.size(), data.size());
    EXPECT_TRUE(rv.is_owned());
    EXPECT_EQ(rv.as_string_view(), data);
}

TEST(ResourceView, EmptyBorrowed) {
    std::span<const std::byte> sp;
    resource_view rv(sp);
    EXPECT_TRUE(rv.empty());
    EXPECT_EQ(rv.size(), 0u);
}

TEST(ResourceView, EmptyOwned) {
    std::vector<std::byte> vec;
    resource_view rv(std::move(vec));
    EXPECT_TRUE(rv.empty());
    EXPECT_EQ(rv.size(), 0u);
}

TEST(ResourceView, Subspan) {
    std::string data = "0123456789";
    auto sp = std::span<const std::byte>(
        reinterpret_cast<const std::byte*>(data.data()), data.size());
    resource_view rv(sp);

    auto sub = rv.subspan(3, 4);
    EXPECT_EQ(sub.size(), 4u);
    std::string_view sub_sv(reinterpret_cast<const char*>(sub.data()), sub.size());
    EXPECT_EQ(sub_sv, "3456");
}

TEST(ResourceView, MoveConstruct) {
    std::string data = "move me";
    std::vector<std::byte> vec(data.size());
    std::memcpy(vec.data(), data.data(), data.size());
    resource_view rv1(std::move(vec));

    resource_view rv2(std::move(rv1));
    EXPECT_EQ(rv2.as_string_view(), data);
    EXPECT_TRUE(rv2.is_owned());
}

TEST(ResourceView, CopyBorrowed) {
    std::string data = "borrow copy";
    auto sp = std::span<const std::byte>(
        reinterpret_cast<const std::byte*>(data.data()), data.size());
    resource_view rv1(sp);

    resource_view rv2(rv1);
    EXPECT_EQ(rv2.as_string_view(), data);
    EXPECT_FALSE(rv2.is_owned());
}

// ── lpak_format helpers ─────────────────────────────────────────────────

TEST(LpakFormat, VersionEncoding) {
    auto v = make_lpak_version(1, 0);
    EXPECT_EQ(v, lpak_version);
    EXPECT_EQ(lpak_version_major_of(v), 1u);
    EXPECT_EQ(lpak_version_minor_of(v), 0u);

    auto v2 = make_lpak_version(3, 7);
    EXPECT_EQ(lpak_version_major_of(v2), 3u);
    EXPECT_EQ(lpak_version_minor_of(v2), 7u);
}

TEST(LpakFormat, AlignUp) {
    EXPECT_EQ(align_up(0, 4096), 0u);
    EXPECT_EQ(align_up(1, 4096), 4096u);
    EXPECT_EQ(align_up(4096, 4096), 4096u);
    EXPECT_EQ(align_up(4097, 4096), 8192u);
}

TEST(LpakFormat, EntryFlagsBitwise) {
    auto f = entry_flags::compressed | entry_flags::lz4;
    EXPECT_TRUE(has_flag(f, entry_flags::compressed));
    EXPECT_TRUE(has_flag(f, entry_flags::lz4));
    EXPECT_FALSE(has_flag(f, entry_flags::zstd));
    EXPECT_FALSE(has_flag(f, entry_flags::encrypted));
}

// ── hash functions ──────────────────────────────────────────────────────

TEST(Hash, Hash64Deterministic) {
    auto h1 = hash64("test/path");
    auto h2 = hash64("test/path");
    EXPECT_EQ(h1, h2);
}

TEST(Hash, Hash64DifferentStrings) {
    auto h1 = hash64("path/a");
    auto h2 = hash64("path/b");
    EXPECT_NE(h1, h2);
}

TEST(Hash, Hash64NsEquivalent) {
    // hash64_ns("ns", "uri") should == hash64("ns/uri")
    auto h1 = hash64_ns("textures", "hero.png");
    auto h2 = hash64("textures/hero.png");
    EXPECT_EQ(h1, h2);
}

TEST(Hash, Hash32Deterministic) {
    auto h1 = hash32("hello");
    auto h2 = hash32("hello");
    EXPECT_EQ(h1, h2);
    EXPECT_NE(hash32("a"), hash32("b"));
}

TEST(Hash, UserDefinedLiteral) {
    using namespace smoothie::resource;
    auto h = "textures/hero.png"_h64;
    EXPECT_EQ(h, hash64("textures/hero.png"));
}
