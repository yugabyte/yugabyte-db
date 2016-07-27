// Copyright (c) YugaByte, Inc.

#include "yb/rocksutil/yb_rocksdb.h"

#include <string>

#include "yb/util/test_macros.h"
#include "yb/util/test_util.h"

using std::string;
using yb::YBToRocksDBSlice;
using yb::RocksDBToYBSlice;

TEST(YBRocksDBTest, SliceConversions) {
  string s = "foo";
  s.push_back('\0');
  s += "def";

  {
    rocksdb::Slice rocksdb_slice(s);
    ASSERT_EQ(s.size(), rocksdb_slice.size());
    yb::Slice result_yb_slice = RocksDBToYBSlice(s);
    ASSERT_EQ(s.size(), result_yb_slice.size());
    ASSERT_EQ(s, result_yb_slice.ToString());
  }

  {
    yb::Slice yb_slice(s);
    ASSERT_EQ(s.size(), yb_slice.size());
    rocksdb::Slice result_rocksdb_slice = YBToRocksDBSlice(s);
    ASSERT_EQ(s.size(), result_rocksdb_slice.size());
    ASSERT_EQ(s, result_rocksdb_slice.ToString(false));
  }
}
