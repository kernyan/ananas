/*-
 * SPDX-License-Identifier: Zlib
 *
 * Copyright (c) 2009-2018 Rink Springer <rink@rink.nu>
 * For conditions of distribution and use, see LICENSE file
 */
#include <gtest/gtest.h>

#if 1
#include "../../include/ananas/util/vector.h"
#else
#include <vector>
namespace util = std;
#endif

TEST(Vector, empty)
{
    util::vector<int> v;
    EXPECT_TRUE(v.empty());
    EXPECT_EQ(0, v.size());
}

TEST(Vector, push_back)
{
    util::vector<int> v;
    v.push_back(1);
    ASSERT_FALSE(v.empty());
    EXPECT_EQ(1, v.size());
    v.push_back(2);
    ASSERT_FALSE(v.empty());
    EXPECT_EQ(2, v.size());
    v.push_back(3);
    ASSERT_FALSE(v.empty());
    EXPECT_EQ(3, v.size());

    EXPECT_EQ(1, v.front());
    EXPECT_EQ(3, v.back());

    int i = 0;
    for (auto& n : v) {
        i++;
        EXPECT_EQ(i, n);
    }
    EXPECT_EQ(3, i);
}

TEST(Vector, pop_back)
{
    util::vector<int> v;
    v.push_back(1);
    v.push_back(2);
    EXPECT_EQ(2, v.size());
    v.pop_back();
    ASSERT_FALSE(v.empty());
    EXPECT_EQ(1, v.size());
    EXPECT_EQ(1, v.back());
    v.pop_back();
    ASSERT_TRUE(v.empty());
    EXPECT_EQ(0, v.size());
}

TEST(Vector, erase)
{
    // Remove from center
    {
        util::vector<int> v;
        v.push_back(1);
        v.push_back(2);
        v.push_back(3);

        v.erase(v.begin() + 1);
        ASSERT_FALSE(v.empty());
        ASSERT_EQ(2, v.size());
        EXPECT_EQ(1, v.front());
        EXPECT_EQ(3, v.back());
    }
    // Remove from begin
    {
        util::vector<int> v;
        v.push_back(1);
        v.push_back(2);
        v.push_back(3);
        v.push_back(4);
        v.push_back(5);

        v.erase(v.begin(), v.begin() + 3);
        ASSERT_FALSE(v.empty());
        ASSERT_EQ(2, v.size());
        EXPECT_EQ(4, v.front());
        EXPECT_EQ(5, v.back());
    }
    // Remove from end
    {
        util::vector<int> v;
        v.push_back(1);
        v.push_back(2);
        v.push_back(3);

        v.erase(v.begin() + 2);
        ASSERT_FALSE(v.empty());
        ASSERT_EQ(2, v.size());
        EXPECT_EQ(1, v.front());
        EXPECT_EQ(2, v.back());
    }
}

TEST(Vector, ErasingAnItemsMultipleTimesWorks)
{
    // 1, 2, ..., 9
    util::vector<int> v;
    for (int n = 0; n < 10; ++n)
        v.push_back(n);

    // Remove 0 .. 5
    for(int n = 0; n < 5; ++n)
        v.erase(v.begin(), v.begin() + 1);

    // 0, 6, 7, 8, 9
    ASSERT_EQ(5, v.size());
    EXPECT_EQ(5, v[0]);
    EXPECT_EQ(6, v[1]);
    EXPECT_EQ(7, v[2]);
    EXPECT_EQ(8, v[3]);
    EXPECT_EQ(9, v[4]);
}

TEST(Vector, resize)
{
    util::vector<int> v;
    v.resize(3);
    EXPECT_EQ(3, v.size());
    for (auto n : v) {
        EXPECT_EQ(0, n);
    }

    v.push_back(1);
    v.push_back(2);
    EXPECT_EQ(5, v.size());
    v.resize(10);
    EXPECT_EQ(10, v.size());

    size_t n = 0;
    for (/* nothing */; n < 3; n++) {
        EXPECT_EQ(0, v[n]);
    }
    EXPECT_EQ(1, v[n]);
    n++;
    EXPECT_EQ(2, v[n]);
    n++;
    for (/* nothing */; n < v.size(); n++) {
        EXPECT_EQ(0, v[n]);
    }
}

TEST(Vector, InsertNewItems)
{
    util::vector<int> v;
    v.insert(v.begin(), 1);
    v.insert(v.begin(), 2);
    v.insert(v.begin(), 3);
    ASSERT_EQ(3, v.size());
    EXPECT_EQ(3, v[0]);
    EXPECT_EQ(2, v[1]);
    EXPECT_EQ(1, v[2]);
}

TEST(Vector, InsertWithExistingItems)
{
    util::vector<int> v;
    v.resize(3);
    v.insert(v.begin(), 1);
    ASSERT_EQ(4, v.size());
    EXPECT_EQ(1, v.front());
    for (size_t n = 1; n < v.size(); n++) {
        EXPECT_EQ(0, v[n]);
    }
    v.insert(v.begin() + 3, 2);
    ASSERT_EQ(5, v.size());
    {
        size_t n = 0;
        EXPECT_EQ(1, v[n]);
        n++;
        for (/* nothing */; n < 3; n++) {
            EXPECT_EQ(0, v[n]);
        }
        EXPECT_EQ(2, v[n]);
        n++;
        for (/* nothing */; n < v.size(); n++) {
            EXPECT_EQ(0, v[n]);
        }
    }
}

TEST(Vector, iterator)
{
    util::vector<int> v;
    v.push_back(1);
    v.push_back(2);
    v.push_back(3);

    auto it = v.begin();
    EXPECT_EQ(1, *it);
    ++it;
    EXPECT_EQ(2, *it);
    ++it;
    EXPECT_EQ(3, *it);
    ++it;
    EXPECT_EQ(v.end(), it);
    --it;
    EXPECT_EQ(3, *it);
    --it;
    EXPECT_EQ(2, *it);
    --it;
    EXPECT_EQ(1, *it);
    EXPECT_EQ(v.begin(), it);

    EXPECT_EQ(1, *it++);
    EXPECT_EQ(2, *it++);
    EXPECT_EQ(3, *it++);
    EXPECT_EQ(v.end(), it);

    --it;
    EXPECT_EQ(3, *it--);
    EXPECT_EQ(2, *it--);
    EXPECT_EQ(1, *it);
    EXPECT_EQ(v.begin(), it);
}

TEST(Vector, iterator_empty_vector)
{
    util::vector<int> v;

    EXPECT_EQ(v.begin(), v.end());
}

namespace
{
    struct Entry {
        Entry() = default;
        Entry(int n) : t_n(n) {}
        Entry(Entry&& n) : t_n(n.t_n) { n.t_n = -1; }
        int t_n = 0;
    };

    bool operator==(const Entry& a, const Entry& b) { return a.t_n == b.t_n; }
} // namespace

TEST(Vector, emplace_back)
{
    util::vector<Entry> v;
    v.emplace_back(1);
    Entry e{2};
    v.emplace_back(std::move(e));
    ASSERT_EQ(2, v.size());
    EXPECT_EQ(Entry{1}, v.front());
    EXPECT_EQ(Entry{2}, v.back());
    EXPECT_EQ(Entry{-1}, e);
}

TEST(Vector, move)
{
    util::vector<int> v;
    v.push_back(1);
    v.push_back(2);
    v.push_back(3);

    util::vector<int> w(std::move(v));
    ASSERT_TRUE(v.empty());
    ASSERT_EQ(3, w.size());
    for (size_t n = 0; n < w.size(); n++) {
        EXPECT_EQ(n + 1, w[n]);
    }

    EXPECT_EQ(v.end(), v.begin());
}

TEST(Vector, copy)
{
    util::vector<int> v;
    v.push_back(1);
    v.push_back(2);
    v.push_back(3);

    util::vector<int> w(v);
    ASSERT_FALSE(w.empty());
    ASSERT_EQ(v.size(), w.size());
    for (size_t n = 0; n < w.size(); n++) {
        EXPECT_EQ(v[n], w[n]);
    }
}

TEST(Vector, assign)
{
    util::vector<int> v, w;
    v.push_back(1);
    v.push_back(2);
    v.push_back(3);

    w = v;
    ASSERT_FALSE(w.empty());
    ASSERT_EQ(v.size(), w.size());
    for (size_t n = 0; n < w.size(); n++) {
        EXPECT_EQ(v[n], w[n]);
    }
}

TEST(Vector, RemoveDoesNothingWhenEmpty)
{
    util::vector<int> v;
    v.remove(0);
    ASSERT_TRUE(v.empty());
}

TEST(Vector, RemoveIgnoresNotMatchingElements)
{
    util::vector<int> v;
    v.push_back(1);
    v.remove(0);
    ASSERT_EQ(1, v.size());
    EXPECT_EQ(1, v.front());
}

TEST(Vector, RemoveRemovesSingleMatchingElements)
{
    util::vector<int> v;
    v.push_back(1);
    v.remove(1);
    ASSERT_TRUE(v.empty());
}

TEST(Vector, RemoveRemovesMultipleMatchingElements)
{
    util::vector<int> v;
    v.push_back(1);
    v.push_back(1);
    v.push_back(1);
    v.remove(1);
    ASSERT_TRUE(v.empty());
}

TEST(Vector, RemoveRemovesMultipleMatchingElementsAtTheFront)
{
    util::vector<int> v;
    v.push_back(1);
    v.push_back(1);
    v.push_back(1);
    v.push_back(2);
    v.remove(1);
    ASSERT_EQ(1, v.size());
    EXPECT_EQ(2, v.front());
}

TEST(Vector, RemoveRemovesMultipleMatchingElementsAtTheBack)
{
    util::vector<int> v;
    v.push_back(0);
    v.push_back(1);
    v.push_back(1);
    v.remove(1);
    ASSERT_EQ(1, v.size());
    EXPECT_EQ(0, v.front());
}

TEST(Vector, RemoveRemovesWithInterleavingMatches)
{
    util::vector<int> v;
    v.push_back(0);
    v.push_back(1);
    v.push_back(2);
    v.push_back(1);
    v.push_back(0);
    v.remove(1);
    ASSERT_EQ(3, v.size());
    EXPECT_EQ(0, v[0]);
    EXPECT_EQ(2, v[1]);
    EXPECT_EQ(0, v[2]);
}
