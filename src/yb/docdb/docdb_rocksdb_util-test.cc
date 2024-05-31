// Copyright (c) YugaByte, Inc.
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

#include "yb/docdb/docdb_rocksdb_util.h"
#include "yb/rocksdb/table.h"

#include "yb/util/result.h"
#include "yb/util/test_util.h"

DECLARE_int32(num_cpus);
DECLARE_int32(rocksdb_max_background_flushes);
DECLARE_bool(rocksdb_disable_compactions);
DECLARE_int32(rocksdb_base_background_compactions);
DECLARE_int32(rocksdb_max_background_compactions);
DECLARE_int32(priority_thread_pool_size);
DECLARE_int32(block_restart_interval);
DECLARE_int32(index_block_restart_interval);
DECLARE_int64(rocksdb_compact_flush_rate_limit_bytes_per_sec);
DECLARE_string(rocksdb_compact_flush_rate_limit_sharing_mode);

namespace yb {
namespace docdb {

class DocDBRocksDBUtilTest : public YBTest {};

TEST_F(DocDBRocksDBUtilTest, CaseInsensitiveCompressionType) {
  rocksdb::CompressionType got_compression_type =
      CHECK_RESULT(TEST_GetConfiguredCompressionType("snappy"));

  ASSERT_EQ(got_compression_type, rocksdb::kSnappyCompression);

  got_compression_type = CHECK_RESULT(TEST_GetConfiguredCompressionType("SNappy"));
  ASSERT_EQ(got_compression_type, rocksdb::kSnappyCompression);
  got_compression_type = CHECK_RESULT(TEST_GetConfiguredCompressionType("snaPPy"));
  ASSERT_EQ(got_compression_type, rocksdb::kSnappyCompression);

  ASSERT_NOK(TEST_GetConfiguredCompressionType("snappy-"));

  got_compression_type = CHECK_RESULT(TEST_GetConfiguredCompressionType("Lz4"));
  ASSERT_EQ(got_compression_type, rocksdb::kLZ4Compression);

  got_compression_type = CHECK_RESULT(TEST_GetConfiguredCompressionType("zLiB"));
  ASSERT_EQ(got_compression_type, rocksdb::kZlibCompression);
}

TEST_F(DocDBRocksDBUtilTest, MaxBackgroundFlushesDefault) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_num_cpus) = 16;
  auto options = TEST_AutoInitFromRocksDBFlags();
  CHECK_EQ(options.max_background_flushes, 3);
}

TEST_F(DocDBRocksDBUtilTest, MaxBackgroundFlushesDefaultLimit) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_num_cpus) = 32;
  auto options = TEST_AutoInitFromRocksDBFlags();
  CHECK_EQ(options.max_background_flushes, 4);
}

TEST_F(DocDBRocksDBUtilTest, MaxBackgroundFlushesCompactionsDisabled) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_rocksdb_max_background_flushes) = 10;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_rocksdb_disable_compactions) = true;
  auto options = TEST_AutoInitFromRocksDBFlags();
  CHECK_EQ(options.max_background_flushes, 10);
}

TEST_F(DocDBRocksDBUtilTest, BaseBackgroundCompactionsDefault) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_rocksdb_max_background_compactions) = 10;
  auto options = TEST_AutoInitFromRocksDBFlags();
  CHECK_EQ(options.base_background_compactions, 10);
}

TEST_F(DocDBRocksDBUtilTest, BaseBackgroundCompactionsDisabled) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_rocksdb_disable_compactions) = true;
  auto options = TEST_AutoInitFromRocksDBFlags();
  CHECK_EQ(options.base_background_compactions, -1);
}

TEST_F(DocDBRocksDBUtilTest, BaseBackgroundCompactionsExplicit) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_rocksdb_base_background_compactions) = 23;
  auto options = TEST_AutoInitFromRocksDBFlags();
  CHECK_EQ(options.base_background_compactions, 23);
}

TEST_F(DocDBRocksDBUtilTest, MaxBackgroundCompactionsDefault) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_num_cpus) = 12;
  auto options = TEST_AutoInitFromRocksDBFlags();
  CHECK_EQ(options.max_background_compactions, 3);
}

TEST_F(DocDBRocksDBUtilTest, MaxBackgroundCompactionsDisabled) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_rocksdb_disable_compactions) = true;
  auto options = TEST_AutoInitFromRocksDBFlags();
  CHECK_EQ(options.max_background_compactions, 1);
}

TEST_F(DocDBRocksDBUtilTest, MaxBackgroundCompactionsExplicit) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_rocksdb_max_background_compactions) = 23;
  auto options = TEST_AutoInitFromRocksDBFlags();
  CHECK_EQ(options.max_background_compactions, 23);
}

TEST_F(DocDBRocksDBUtilTest, PriorityThreadPoolSizeDefaultLowCpus) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_num_cpus) = 3;
  CHECK_EQ(GetGlobalRocksDBPriorityThreadPoolSize(), 1);
}

TEST_F(DocDBRocksDBUtilTest, PriorityThreadPoolSizeDefaultFewCpus) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_num_cpus) = 5;
  CHECK_EQ(GetGlobalRocksDBPriorityThreadPoolSize(), 2);
}

TEST_F(DocDBRocksDBUtilTest, PriorityThreadPoolSizeDefaultManyCpus) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_num_cpus) = 10;
  CHECK_EQ(GetGlobalRocksDBPriorityThreadPoolSize(), 4);
}

TEST_F(DocDBRocksDBUtilTest, PriorityThreadPoolSizeTakesExplicitSetting) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_priority_thread_pool_size) = 16;
  CHECK_EQ(GetGlobalRocksDBPriorityThreadPoolSize(), 16);
}

TEST_F(DocDBRocksDBUtilTest, PriorityThreadPoolSizeTakesMaxBackgroundCompaction) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_rocksdb_max_background_compactions) = 4;
  CHECK_EQ(GetGlobalRocksDBPriorityThreadPoolSize(), 4);
}

TEST_F(DocDBRocksDBUtilTest, PriorityThreadPoolSizeCompactionDisabled) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_rocksdb_disable_compactions) = true;
  CHECK_EQ(GetGlobalRocksDBPriorityThreadPoolSize(), 1);
}

TEST_F(DocDBRocksDBUtilTest, MinBlockRestartInterval) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_block_restart_interval) = 0;
  auto blockBasedOptions = TEST_AutoInitFromRocksDbTableFlags();
  CHECK_EQ(blockBasedOptions.block_restart_interval, 16);
}

TEST_F(DocDBRocksDBUtilTest, MaxBlockRestartInterval) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_block_restart_interval) = 512;
  auto blockBasedOptions = TEST_AutoInitFromRocksDbTableFlags();
  CHECK_EQ(blockBasedOptions.block_restart_interval, 256);
}

TEST_F(DocDBRocksDBUtilTest, ValidBlockRestartInterval) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_block_restart_interval) = 8;
  auto blockBasedOptions = TEST_AutoInitFromRocksDbTableFlags();
  CHECK_EQ(blockBasedOptions.block_restart_interval, 8);
}

TEST_F(DocDBRocksDBUtilTest, DefaultBlockRestartInterval) {
  auto blockBasedOptions = TEST_AutoInitFromRocksDbTableFlags();
  CHECK_EQ(blockBasedOptions.block_restart_interval, 16);
}

TEST_F(DocDBRocksDBUtilTest, MinIndexBlockRestartInterval) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_index_block_restart_interval) = 0;
  auto blockBasedOptions = TEST_AutoInitFromRocksDbTableFlags();
  CHECK_EQ(blockBasedOptions.index_block_restart_interval, 1);
}

TEST_F(DocDBRocksDBUtilTest, MaxIndexBlockRestartInterval) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_index_block_restart_interval) = 512;
  auto blockBasedOptions = TEST_AutoInitFromRocksDbTableFlags();
  CHECK_EQ(blockBasedOptions.index_block_restart_interval, 256);
}

TEST_F(DocDBRocksDBUtilTest, ValidIndexBlockRestartInterval) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_index_block_restart_interval) = 8;
  auto blockBasedOptions = TEST_AutoInitFromRocksDbTableFlags();
  CHECK_EQ(blockBasedOptions.index_block_restart_interval, 8);
}

TEST_F(DocDBRocksDBUtilTest, DefaultIndexBlockRestartInterval) {
  auto blockBasedOptions = TEST_AutoInitFromRocksDbTableFlags();
  CHECK_EQ(blockBasedOptions.index_block_restart_interval, 1);
}

TEST_F(DocDBRocksDBUtilTest, RocksDBRateLimiter) {
  // Check `ParseEnumInsensitive<RateLimiterSharingMode>`
  {
    using Ret = Result<RateLimiterSharingMode>;
    using State = std::tuple<std::string, Ret>;
    State states[] {
      std::make_tuple(ToString(RateLimiterSharingMode::NONE),
                      Ret(RateLimiterSharingMode::NONE)),
      std::make_tuple(ToString(RateLimiterSharingMode::TSERVER),
                      Ret(RateLimiterSharingMode::TSERVER)),
      std::make_tuple("none", Ret(RateLimiterSharingMode::NONE)),
      std::make_tuple("nOnE", Ret(RateLimiterSharingMode::NONE)),
      std::make_tuple("tserver", Ret(RateLimiterSharingMode::TSERVER)),
      std::make_tuple("TServeR", Ret(RateLimiterSharingMode::TSERVER)),
      std::make_tuple("",
                      Ret(STATUS(InvalidArgument,
                                 "yb::docdb::RateLimiterSharingMode invalid value: "))),
      std::make_tuple("none-",
                      Ret(STATUS(InvalidArgument,
                                 "yb::docdb::RateLimiterSharingMode invalid value: none-"))),
      std::make_tuple("t_server",
                      Ret(STATUS(InvalidArgument,
                                 "yb::docdb::RateLimiterSharingMode invalid value: t_server")))
    };

    for (const auto& s : states) {
      auto pr = ParseEnumInsensitive<RateLimiterSharingMode>(std::get<std::string>(s));
      ASSERT_EQ(pr.ok(), std::get<Ret>(s).ok());
      if (pr.ok()) {
        ASSERT_EQ(*pr, *std::get<Ret>(s));
      } else {
        ASSERT_EQ(pr.status().ToString(false, true),
                  std::get<Ret>(s).status().ToString(false, true));
      }
    }
  }

  // Check `GetRocksDBRateLimiterSharingMode`
  {
    for (auto mode : RateLimiterSharingModeList()) {
      ANNOTATE_UNPROTECTED_WRITE(FLAGS_rocksdb_compact_flush_rate_limit_sharing_mode) =
          ToString(mode);
      ASSERT_EQ(mode, GetRocksDBRateLimiterSharingMode());
    }

    // Check zero bps case
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_rocksdb_compact_flush_rate_limit_bytes_per_sec) = 0;
    auto util_limiter = CreateRocksDBRateLimiter();
    ASSERT_EQ(util_limiter.get(), nullptr);

    // Check non-zero case, should be same to direct call to `rocksdb::NewGenericRateLimiter`
    constexpr int64_t kBPS = 64_MB;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_rocksdb_compact_flush_rate_limit_bytes_per_sec) = kBPS;
    std::shared_ptr<rocksdb::RateLimiter> raw_limiter(rocksdb::NewGenericRateLimiter(kBPS));
    ASSERT_ONLY_NOTNULL(raw_limiter.get());
    util_limiter = CreateRocksDBRateLimiter();
    ASSERT_ONLY_NOTNULL(util_limiter.get());
    ASSERT_EQ(util_limiter->GetSingleBurstBytes(), raw_limiter->GetSingleBurstBytes());

    // Decrease bytes per sec
    constexpr auto kFactor = 2;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_rocksdb_compact_flush_rate_limit_bytes_per_sec)
      = kBPS / kFactor;
    ASSERT_ONLY_NOTNULL(raw_limiter.get());
    util_limiter = CreateRocksDBRateLimiter();
    ASSERT_ONLY_NOTNULL(util_limiter.get());
    ASSERT_EQ(util_limiter->GetSingleBurstBytes(), raw_limiter->GetSingleBurstBytes() / kFactor);
  }
}

}  // namespace docdb
}  // namespace yb
