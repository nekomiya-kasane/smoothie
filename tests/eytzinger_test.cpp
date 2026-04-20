#include "smoothie/detail/eytzinger_array.h"

#include <algorithm>
#include <cstdint>
#include <gtest/gtest.h>
#include <numeric>
#include <vector>

using smoothie::detail::eytzinger_array;

TEST(EytzingerArray, EmptyArray) {
    eytzinger_array<uint64_t> ea;
    EXPECT_EQ(ea.size(), 0u);
    EXPECT_TRUE(ea.empty());
    EXPECT_EQ(ea.find(42), 0u); // not-found sentinel == size()
}

TEST(EytzingerArray, SingleElement) {
    eytzinger_array<uint64_t> ea;
    std::vector<uint64_t> sorted = {100};
    ea.build(sorted);

    EXPECT_EQ(ea.size(), 1u);
    EXPECT_FALSE(ea.empty());
    EXPECT_EQ(ea.find(100), 0u); // found at sorted index 0
    EXPECT_EQ(ea.find(99), 1u);  // not found
    EXPECT_EQ(ea.find(101), 1u); // not found
}

TEST(EytzingerArray, SmallSorted) {
    eytzinger_array<uint64_t> ea;
    std::vector<uint64_t> sorted = {10, 20, 30, 40, 50};
    ea.build(sorted);

    EXPECT_EQ(ea.size(), 5u);

    for (size_t i = 0; i < sorted.size(); ++i) {
        EXPECT_EQ(ea.find(sorted[i]), i) << "key=" << sorted[i];
    }

    // Not-found cases
    EXPECT_EQ(ea.find(5), ea.size());
    EXPECT_EQ(ea.find(15), ea.size());
    EXPECT_EQ(ea.find(55), ea.size());
}

TEST(EytzingerArray, PowerOfTwoMinusOne) {
    // 2^N - 1 elements = perfect binary tree
    eytzinger_array<uint64_t> ea;
    std::vector<uint64_t> sorted(7);
    std::iota(sorted.begin(), sorted.end(), 1); // [1..7]
    ea.build(sorted);

    for (size_t i = 0; i < sorted.size(); ++i) {
        EXPECT_EQ(ea.find(sorted[i]), i);
    }
    EXPECT_EQ(ea.find(0), ea.size());
    EXPECT_EQ(ea.find(8), ea.size());
}

TEST(EytzingerArray, LargeArray) {
    eytzinger_array<uint64_t> ea;
    constexpr size_t N = 10000;
    std::vector<uint64_t> sorted(N);
    for (size_t i = 0; i < N; ++i) sorted[i] = i * 3 + 7;
    ea.build(sorted);

    EXPECT_EQ(ea.size(), N);

    // Check every element
    for (size_t i = 0; i < N; ++i) {
        EXPECT_EQ(ea.find(sorted[i]), i);
    }

    // Check misses at gaps
    EXPECT_EQ(ea.find(sorted[0] - 1), ea.size());
    EXPECT_EQ(ea.find(sorted[N - 1] + 1), ea.size());
    EXPECT_EQ(ea.find(sorted[500] + 1), ea.size());
}

TEST(EytzingerArray, SortedKeys) {
    eytzinger_array<uint64_t> ea;
    std::vector<uint64_t> sorted = {10, 20, 30, 40, 50, 60, 70};
    ea.build(sorted);

    auto recovered = ea.sorted_keys();
    EXPECT_EQ(recovered, sorted);
}

TEST(EytzingerArray, SortedIndicesConsistency) {
    eytzinger_array<uint64_t> ea;
    std::vector<uint64_t> sorted = {5, 15, 25, 35, 45, 55};
    ea.build(sorted);

    auto data = ea.data();
    auto indices = ea.sorted_indices();
    EXPECT_EQ(data.size(), sorted.size());
    EXPECT_EQ(indices.size(), sorted.size());

    // Verify: for each Eytzinger position i, data[i] == sorted[indices[i]]
    for (size_t i = 0; i < data.size(); ++i) {
        EXPECT_EQ(data[i], sorted[indices[i]]) << "Eytzinger pos " << i;
    }
}

TEST(EytzingerArray, DuplicateKeys) {
    // Eytzinger assumes sorted unique input; duplicates should still not crash
    eytzinger_array<uint64_t> ea;
    std::vector<uint64_t> sorted = {10, 10, 20, 20, 30};
    ea.build(sorted);

    // find should return *some* valid index for duplicates
    auto idx = ea.find(10);
    EXPECT_LT(idx, ea.size());
    EXPECT_EQ(sorted[idx], 10u);

    idx = ea.find(20);
    EXPECT_LT(idx, ea.size());
    EXPECT_EQ(sorted[idx], 20u);

    EXPECT_EQ(ea.find(15), ea.size());
}

TEST(EytzingerArray, TwoElements) {
    eytzinger_array<uint64_t> ea;
    std::vector<uint64_t> sorted = {100, 200};
    ea.build(sorted);

    EXPECT_EQ(ea.find(100), 0u);
    EXPECT_EQ(ea.find(200), 1u);
    EXPECT_EQ(ea.find(150), ea.size());
}

TEST(EytzingerArray, ThreeElements) {
    eytzinger_array<uint64_t> ea;
    std::vector<uint64_t> sorted = {1, 2, 3};
    ea.build(sorted);

    EXPECT_EQ(ea.find(1), 0u);
    EXPECT_EQ(ea.find(2), 1u);
    EXPECT_EQ(ea.find(3), 2u);
    EXPECT_EQ(ea.find(0), ea.size());
    EXPECT_EQ(ea.find(4), ea.size());
}

TEST(EytzingerArray, RebuildOverwrite) {
    eytzinger_array<uint64_t> ea;
    std::vector<uint64_t> first = {1, 2, 3};
    ea.build(first);
    EXPECT_EQ(ea.find(2), 1u);

    // Rebuild with different data
    std::vector<uint64_t> second = {10, 20, 30, 40};
    ea.build(second);
    EXPECT_EQ(ea.size(), 4u);
    EXPECT_EQ(ea.find(2), ea.size()); // old key gone
    EXPECT_EQ(ea.find(30), 2u);
}
