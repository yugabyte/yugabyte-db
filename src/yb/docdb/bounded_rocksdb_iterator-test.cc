// Copyright (c) YugabyteDB, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License.  You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied.  See the License for the specific language governing permissions and limitations
// under the License.
//

#include <optional>
#include <string>
#include <vector>

#include "yb/docdb/bounded_rocksdb_iterator.h"
#include "yb/docdb/key_bounds.h"

#include "yb/rocksdb/db.h"
#include "yb/rocksdb/options.h"

#include "yb/util/result.h"
#include "yb/util/test_macros.h"
#include "yb/util/test_util.h"

namespace yb::docdb {

// Tests for BoundedRocksDbIterator, which wraps a RocksDB iterator and restricts the visible
// key range to [key_bounds.lower, key_bounds.upper) (inclusive lower, exclusive upper).
//
// These tests focus on SeekToLast(), which only matters when an upper bound is set, i.e. the 2nd
// post-split tablet bounded above by key_bounds.upper (e.g. during a reverse scan). Because the
// upper bound is exclusive, SeekToLast() must seek to the upper bound and step back to the
// previous key (and fall back to the real last key when the seek finds nothing).
//
// BoundedRocksDbIterator only ever compares keys as raw bytes (via Slice::compare / KeyBounds),
// so the tests run against a plain RocksDB using the default bytewise comparator and write plain
// (non-DocDB-encoded) keys. This keeps the focus on the bound handling itself, independent of any
// DocDB key encoding.
class BoundedRocksDbIteratorTest : public YBTest {
 protected:
  void SetUp() override {
    YBTest::SetUp();
    rocksdb::Options options;
    options.create_if_missing = true;
    rocksdb::DB* db = nullptr;
    ASSERT_OK(rocksdb::DB::Open(options, GetTestPath("rocksdb"), &db));
    db_.reset(db);
  }

  void TearDown() override {
    db_.reset();
    YBTest::TearDown();
  }

  void PutKey(const std::string& key) {
    ASSERT_OK(db_->Put(rocksdb::WriteOptions(), key, key + "_value"));
  }

  void PopulateKeys() {
    for (const auto& key : kKeys) {
      ASSERT_NO_FATALS(PutKey(key));
    }
  }

  // Returns the key that SeekToLast() lands on, or std::nullopt if the iterator is not valid.
  Result<std::optional<std::string>> SeekToLastKey(const KeyBounds& bounds) {
    rocksdb::ReadOptions read_opts;
    read_opts.query_id = rocksdb::kDefaultQueryId;
    BoundedRocksDbIterator it(db_.get(), read_opts, &bounds);
    const auto& entry = it.SeekToLast();
    if (!VERIFY_RESULT(it.CheckedValid())) {
      return std::nullopt;
    }
    return entry.key.ToBuffer();
  }

  static KeyBounds MakeBounds(const std::string& lower, const std::string& upper) {
    return KeyBounds(Slice(lower), Slice(upper));
  }

  // Keys present in the database for the populated test cases.
  const std::vector<std::string> kKeys = {"k1", "k3", "k5", "k7", "k9"};

  std::unique_ptr<rocksdb::DB> db_;
};

TEST_F(BoundedRocksDbIteratorTest, SeekToLastNoBounds) {
  ASSERT_NO_FATALS(PopulateKeys());
  // Empty lower and upper: SeekToLast should return the actual last key.
  ASSERT_EQ(ASSERT_RESULT(SeekToLastKey(KeyBounds())), "k9");
}

TEST_F(BoundedRocksDbIteratorTest, SeekToLastUpperBoundMatchesExistingKey) {
  ASSERT_NO_FATALS(PopulateKeys());
  // Upper bound is exclusive and equals an existing key: the last visible key is the one before it.
  // Regression case -- the old code seeked to the upper bound without stepping back, landing on
  // "k5", which then fails the exclusive-upper bounds check and yields no last key instead of "k3".
  ASSERT_EQ(ASSERT_RESULT(SeekToLastKey(MakeBounds(/* lower= */ "", /* upper= */ "k5"))), "k3");
}

TEST_F(BoundedRocksDbIteratorTest, SeekToLastUpperBoundBetweenKeys) {
  ASSERT_NO_FATALS(PopulateKeys());
  // Upper bound falls between two keys: Seek(upper) lands on "k7", Prev() yields "k5".
  ASSERT_EQ(ASSERT_RESULT(SeekToLastKey(MakeBounds(/* lower= */ "", /* upper= */ "k6"))), "k5");
}

TEST_F(BoundedRocksDbIteratorTest, SeekToLastUpperBoundAboveAllKeys) {
  ASSERT_NO_FATALS(PopulateKeys());
  // Upper bound is past every key: Seek(upper) finds nothing, fall back to the real last key.
  ASSERT_EQ(ASSERT_RESULT(SeekToLastKey(MakeBounds(/* lower= */ "", /* upper= */ "z"))), "k9");
}

TEST_F(BoundedRocksDbIteratorTest, SeekToLastUpperBoundBelowAllKeys) {
  ASSERT_NO_FATALS(PopulateKeys());
  // Upper bound excludes every key: Seek(upper) lands on "k1", Prev() steps before the first key.
  ASSERT_EQ(ASSERT_RESULT(SeekToLastKey(MakeBounds(/* lower= */ "", /* upper= */ "a"))),
            std::nullopt);
}

TEST_F(BoundedRocksDbIteratorTest, SeekToLastWithinLowerAndUpperBound) {
  ASSERT_NO_FATALS(PopulateKeys());
  // Both bounds set: last key in [k3, k8) is "k7".
  ASSERT_EQ(ASSERT_RESULT(SeekToLastKey(MakeBounds(/* lower= */ "k3", /* upper= */ "k8"))), "k7");
}

TEST_F(BoundedRocksDbIteratorTest, SeekToLastPrevFallsBelowLowerBound) {
  ASSERT_NO_FATALS(PopulateKeys());
  // Range [k4, k5) contains no key. Seek(upper="k5") lands on "k5", Prev() yields "k3", which is
  // below the lower bound and must be filtered out.
  ASSERT_EQ(ASSERT_RESULT(SeekToLastKey(MakeBounds(/* lower= */ "k4", /* upper= */ "k5"))),
            std::nullopt);
}

TEST_F(BoundedRocksDbIteratorTest, SeekToLastEmptyUpperBoundRespectsLowerBound) {
  ASSERT_NO_FATALS(PopulateKeys());
  // Empty upper bound takes the SeekToLast() fast path; the resulting key still satisfies the
  // lower bound, so "k9" is returned.
  ASSERT_EQ(ASSERT_RESULT(SeekToLastKey(MakeBounds(/* lower= */ "k1", /* upper= */ ""))), "k9");
}

TEST_F(BoundedRocksDbIteratorTest, SeekToLastEmptyUpperBoundLowerBoundAboveAllKeys) {
  ASSERT_NO_FATALS(PopulateKeys());
  // Empty upper bound: SeekToLast() returns "k9", but the lower bound is past every key, so it is
  // filtered out.
  ASSERT_EQ(ASSERT_RESULT(SeekToLastKey(MakeBounds(/* lower= */ "z", /* upper= */ ""))),
            std::nullopt);
}

TEST_F(BoundedRocksDbIteratorTest, SeekToLastEmptyDatabase) {
  // No keys written. SeekToLast() must report an invalid (empty) result for every bound variant.
  ASSERT_EQ(ASSERT_RESULT(SeekToLastKey(KeyBounds())), std::nullopt);
  ASSERT_EQ(ASSERT_RESULT(SeekToLastKey(MakeBounds(/* lower= */ "", /* upper= */ "k5"))),
            std::nullopt);
  ASSERT_EQ(ASSERT_RESULT(SeekToLastKey(MakeBounds(/* lower= */ "k1", /* upper= */ "k9"))),
            std::nullopt);
}

}  // namespace yb::docdb
