// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.
//
// The following only applies to changes made to this file as part of YugabyteDB development.
//
// Portions Copyright (c) YugabyteDB, Inc.
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

#include <boost/algorithm/string/predicate.hpp>

#include "yb/util/logging.h"
#include <glog/stl_logging.h>
#include <gtest/gtest.h>

#include "yb/fs/fs_manager.h"

#include "yb/gutil/strings/util.h"

#include "yb/util/metrics.h"
#include "yb/util/multi_drive_test_env.h"
#include "yb/util/status.h"
#include "yb/util/result.h"
#include "yb/util/test_macros.h"
#include "yb/util/test_util.h"
#include "yb/fs/fs.pb.h"

using std::string;
using std::vector;

DECLARE_string(fs_data_dirs);
DECLARE_string(fs_wal_dirs);
DECLARE_bool(TEST_fail_write_pb_container);

namespace yb {

namespace {

bool HasDirsPrefixString(const vector<string>& dirs, const string& path) {
  if (dirs.size() != 1) {
    return false;
  }
  return HasPrefixString(dirs[0], path);
}

} // namespace

class FsManagerTestBase : public YBTest {
 public:
  void SetUp() override {
    YBTest::SetUp();

    // Initialize File-System Layout
    ReinitFsManager();
    ASSERT_OK(fs_manager_->CreateInitialFileSystemLayout());
    ASSERT_OK(fs_manager_->CheckAndOpenFileSystemRoots());
  }

  void ReinitFsManager() {
    ReinitFsManager({ GetTestPath("fs_root") }, { GetTestPath("fs_root")} );
  }

  void ReinitFsManager(const vector<string>& wal_paths, const vector<string>& data_paths) {
    // Blow away the old memtrackers first.
    fs_manager_.reset();

    FsManagerOpts opts;
    opts.wal_paths = wal_paths;
    opts.data_paths = data_paths;
    opts.server_type = kServerType;
    fs_manager_.reset(new FsManager(env_.get(), opts));
  }

  void ReinitFsManager(const FsManagerOpts& opts) {
    fs_manager_.reset(new FsManager(env_.get(), opts));
  }

  void ValidateRootDataPaths(const string& data_path, const string& wal_path) {
    ASSERT_TRUE(HasDirsPrefixString(fs_manager()->GetConsensusMetadataDirs(), data_path));
    ASSERT_TRUE(HasDirsPrefixString(fs_manager()->GetRaftGroupMetadataDirs(), data_path));
    ASSERT_TRUE(HasDirsPrefixString(fs_manager()->GetDataRootDirs(), data_path));
    if (!wal_path.empty()) {
      ASSERT_TRUE(HasDirsPrefixString(fs_manager()->GetWalRootDirs(), wal_path));
    }
  }

  void SetupFlagsAndBaseDirs(
      const string& data_path, const string& wal_path, FsManagerOpts* out_opts) {
    // Setup data in case empty.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_fs_data_dirs) = data_path;
    if (!data_path.empty()) {
      string path = GetTestPath(data_path);
      ASSERT_OK(env_->CreateDir(path));
      ANNOTATE_UNPROTECTED_WRITE(FLAGS_fs_data_dirs) = path;
    }
    // Setup wal in case empty.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_fs_wal_dirs) = wal_path;
    if (!wal_path.empty()) {
      string path = GetTestPath(wal_path);
      ASSERT_OK(env_->CreateDir(path));
      ANNOTATE_UNPROTECTED_WRITE(FLAGS_fs_wal_dirs) = path;
    }
    // Setup opts from flags and add server type.
    FsManagerOpts opts;
    opts.server_type = kServerType;
    *out_opts = opts;
  }

  void SetupForDelete(ShouldDeleteLogs delete_logs_dir = ShouldDeleteLogs::kFalse) {
    string path = GetTestPath("new_fs_root");
    ASSERT_OK(env_->CreateDir(path));

    ReinitFsManager(vector <string> (), {path});
    ASSERT_OK(fs_manager()->CreateInitialFileSystemLayout());
    ValidateRootDataPaths(path, "");

    log_dir_ = path + "/yb-data/logs";
    ASSERT_OK(env_->CreateDir(log_dir_));
    ASSERT_OK(fs_manager()->DeleteFileSystemLayout(delete_logs_dir));
  }

  void EnsureDataDirNotPresent() {
    std::vector <std::string> data_dirs = fs_manager()->GetDataRootDirs();
    for (const auto& data_dir : data_dirs) {
      bool is_dir = false;
      ASSERT_NOK(env_->IsDirectory(data_dir, &is_dir));
      ASSERT_FALSE(is_dir);
    }
  }

  FsManager *fs_manager() const { return fs_manager_.get(); }

  string log_dir() const { return log_dir_; }

  const char* kServerType = "tserver_test";

 private:
  std::unique_ptr<FsManager> fs_manager_;
  string log_dir_ = "";
};

TEST_F(FsManagerTestBase, TestIllegalPaths) {
  vector<string> illegal = { "", "asdf", "/foo\n\t" };
  for (const string& path : illegal) {
    ReinitFsManager({ path }, { path });
    ASSERT_TRUE(fs_manager()->CreateInitialFileSystemLayout().IsIOError());
  }
}

TEST_F(FsManagerTestBase, TestMultiplePaths) {
  vector<string> wal_paths = { GetTestPath("a"), GetTestPath("b"), GetTestPath("c") };
  vector<string> data_paths = { GetTestPath("a"), GetTestPath("b"), GetTestPath("c") };
  ReinitFsManager(wal_paths, data_paths);
  ASSERT_OK(fs_manager()->CreateInitialFileSystemLayout());
  ASSERT_OK(fs_manager()->CheckAndOpenFileSystemRoots());
}

TEST_F(FsManagerTestBase, TestMatchingPathsWithMismatchedSlashes) {
  vector<string> wal_paths = { GetTestPath("foo") };
  vector<string> data_paths = { wal_paths[0] + "/" };
  ReinitFsManager(wal_paths, data_paths);
  ASSERT_OK(fs_manager()->CreateInitialFileSystemLayout());
}

TEST_F(FsManagerTestBase, TestDuplicatePaths) {
  const string wal_path = GetTestPath("foo");
  const string data_path = GetTestPath("bar");
  ReinitFsManager({ wal_path, wal_path, wal_path }, { data_path, data_path, data_path });
  ASSERT_OK(fs_manager()->CreateInitialFileSystemLayout());
  ASSERT_EQ(
      vector<string>({ JoinPathSegments(
          GetServerTypeDataPath(wal_path, kServerType),
          fs_manager()->kWalDirName) }),
      fs_manager()->GetWalRootDirs());
  ASSERT_EQ(
      vector<string>({ JoinPathSegments(
          GetServerTypeDataPath(data_path, kServerType),
          fs_manager()->kDataDirName) }),
      fs_manager()->GetDataRootDirs());
}

TEST_F(FsManagerTestBase, TestListTablets) {
  auto tablet_ids = ASSERT_RESULT(fs_manager()->ListTabletIds(CleanupTemporaryFiles::kTrue));
  ASSERT_EQ(0, tablet_ids.size());

  string path = fs_manager()->GetRaftGroupMetadataDirs()[0];
  std::unique_ptr<WritableFile> writer;
  ASSERT_OK(env_->NewWritableFile(
      JoinPathSegments(path, "foo.tmp"), &writer));
  ASSERT_OK(env_->NewWritableFile(
      JoinPathSegments(path, "foo.tmp.abc123"), &writer));
  ASSERT_OK(env_->NewWritableFile(
      JoinPathSegments(path, ".hidden"), &writer));
  ASSERT_OK(env_->NewWritableFile(
      JoinPathSegments(path, "a_tablet_sort_of"), &writer));

  tablet_ids = ASSERT_RESULT(fs_manager()->ListTabletIds(CleanupTemporaryFiles::kTrue));
  ASSERT_EQ(1, tablet_ids.size()) << tablet_ids;
}

TEST_F(FsManagerTestBase, TestCannotUseNonEmptyFsRoot) {
  string path = GetTestPath("new_fs_root");
  ReinitFsManager({ path }, { path });
  ASSERT_OK(fs_manager()->CreateInitialFileSystemLayout());
  {
    std::unique_ptr<WritableFile> writer;
    ASSERT_OK(env_->NewWritableFile(
        JoinPathSegments(GetServerTypeDataPath(path, kServerType), "some_file"), &writer));
  }

  // Try to create the FS layout. It should fail.
  ASSERT_TRUE(fs_manager()->CreateInitialFileSystemLayout().IsAlreadyPresent());
}

TEST_F(FsManagerTestBase, TestEmptyDataPath) {
  ReinitFsManager(vector<string>(), vector<string>());
  Status s = fs_manager()->CreateInitialFileSystemLayout();
  ASSERT_TRUE(s.IsIOError());
  ASSERT_STR_CONTAINS(s.ToString(),
                      "List of data directories (fs_data_dirs) not provided");
}

TEST_F(FsManagerTestBase, TestOnlyDataPath) {
  string path = GetTestPath("new_fs_root");
  ASSERT_OK(env_->CreateDir(path));

  ReinitFsManager(vector<string>(), { path });
  ASSERT_OK(fs_manager()->CreateInitialFileSystemLayout());
  ValidateRootDataPaths(path, "");
}

TEST_F(FsManagerTestBase, TestPathsFromFlags) {
  FsManagerOpts opts;
  // If no wal specified, we inherit from fs_data_dirs
  {
    SetupFlagsAndBaseDirs("new_data_root_1", "", &opts);
    ReinitFsManager(opts);
    Status s = fs_manager()->CreateInitialFileSystemLayout();
    ASSERT_OK(s);
    ValidateRootDataPaths(FLAGS_fs_data_dirs, FLAGS_fs_data_dirs);
  }
  // If they are both set, we expect them to be whatever they are set to.
  {
    SetupFlagsAndBaseDirs("new_data_root_2", "new_wal_root_2", &opts);
    ReinitFsManager(opts);
    Status s = fs_manager()->CreateInitialFileSystemLayout();
    ASSERT_OK(s);
    ValidateRootDataPaths(FLAGS_fs_data_dirs, FLAGS_fs_wal_dirs);
  }

  // If no data_dirs is set, we expect a failure!
  {
    SetupFlagsAndBaseDirs("", "new_wal_root_3", &opts);
    ReinitFsManager(opts);
    Status s = fs_manager()->CreateInitialFileSystemLayout();
    ASSERT_TRUE(s.IsIOError());
    ASSERT_STR_CONTAINS(s.ToString(),
                        "List of data directories (fs_data_dirs) not provided");
  }
}

TEST_F(FsManagerTestBase, TestDataDirDeletedAndNotLogDir) {
  SetupForDelete();

  EnsureDataDirNotPresent();

  bool is_dir = false;
  ASSERT_OK(env_->IsDirectory(log_dir(), &is_dir));
  ASSERT_TRUE(is_dir);
}

TEST_F(FsManagerTestBase, TestLogDirAlsoDeleted) {
  SetupForDelete(ShouldDeleteLogs::kTrue);

  EnsureDataDirNotPresent();

  bool is_dir = false;
  ASSERT_NOK(env_->IsDirectory(log_dir(), &is_dir));
  ASSERT_FALSE(is_dir);
}

TEST_F(FsManagerTestBase, MultiDriveWithoutMeta) {
  auto paths = { GetTestPath("d1"), GetTestPath("d2") };
  ReinitFsManager(paths, paths);
  ASSERT_OK(fs_manager()->CreateInitialFileSystemLayout());
  ASSERT_OK(env_->DeleteRecursively(fs_manager()->GetRaftGroupMetadataDirs()[0]));

  // Deleted tablet-meta should be created
  ASSERT_OK(fs_manager()->CheckAndOpenFileSystemRoots());
  ASSERT_OK(fs_manager()->ListTabletIds(CleanupTemporaryFiles::kTrue));
}

TEST_F(FsManagerTestBase, AutoFlagsTest) {
  auto path1 = GetTestPath("ad1");
  auto path2 = GetTestPath("ad2");
  const auto paths = {path1, path2};
  ReinitFsManager(paths, paths);
  BlockIdPB msg;
  msg.set_id(123);

  ASSERT_TRUE(fs_manager()->GetAutoFlagsConfigPath().empty());

  // Verify read required before write
  ASSERT_NOK(fs_manager()->WriteAutoFlagsConfig(&msg));
  ASSERT_TRUE(fs_manager()->ReadAutoFlagsConfig(&msg).IsNotFound());
  ASSERT_TRUE(fs_manager()->ReadAutoFlagsConfig(&msg).IsNotFound());

  string auto_flags_path = fs_manager()->GetAutoFlagsConfigPath();
  ASSERT_FALSE(auto_flags_path.empty());

  // Read should still fail with same error
  ASSERT_TRUE(fs_manager()->ReadAutoFlagsConfig(&msg).IsNotFound());

  // Verify clean write
  ASSERT_OK(fs_manager()->WriteAutoFlagsConfig(&msg));
  ASSERT_EQ(fs_manager()->GetAutoFlagsConfigPath(), auto_flags_path);

  // Verify failure mid write
  msg.set_id(456);
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_fail_write_pb_container) = true;
  ASSERT_NOK(fs_manager()->WriteAutoFlagsConfig(&msg));
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_fail_write_pb_container) = false;
  ASSERT_OK(fs_manager()->ReadAutoFlagsConfig(&msg));
  ASSERT_EQ(msg.id(), 123);

  // Swap paths and validate
  const auto paths2 = {path2, path1};
  ReinitFsManager(paths2, paths2);
  ASSERT_OK(fs_manager()->ReadAutoFlagsConfig(&msg));
  ASSERT_EQ(msg.id(), 123);
  ASSERT_EQ(fs_manager()->GetAutoFlagsConfigPath(), auto_flags_path);

  // Delete of file system should delete the config file
  ASSERT_OK(fs_manager()->DeleteFileSystemLayout(ShouldDeleteLogs::kFalse));
  ASSERT_FALSE(env_->FileExists(auto_flags_path));
}

// ==========================================================================
//  Storage-tier label tests
// ==========================================================================

class StorageTierTest : public YBTest {
 public:
  void SetUp() override {
    YBTest::SetUp();
  }

  // Build a FsManager directly from explicit paths + tiers without going
  // through the gflag-based FsManagerOpts() constructor.
  std::unique_ptr<FsManager> MakeFsManager(
      const vector<string>& data_paths,
      const std::unordered_map<string, string>& tier_by_path,
      const vector<string>& wal_paths = {}) {
    FsManagerOpts opts;
    opts.data_paths = data_paths;
    opts.tier_by_path = tier_by_path;
    opts.wal_paths = wal_paths.empty() ? data_paths : wal_paths;
    opts.server_type = kServerType;
    auto mgr = std::make_unique<FsManager>(env_.get(), opts);
    CHECK_OK(mgr->CreateInitialFileSystemLayout());
    CHECK_OK(mgr->CheckAndOpenFileSystemRoots());
    return mgr;
  }

  const char* kServerType = "tserver_test";
};

// All paths labeled: tier map is populated correctly.
TEST_F(StorageTierTest, AllLabeled) {
  auto fast_path = GetTestPath("nvme0");
  auto slow_path = GetTestPath("hdd1");
  ASSERT_OK(env_->CreateDir(fast_path));
  ASSERT_OK(env_->CreateDir(slow_path));

  auto mgr = MakeFsManager({fast_path, slow_path}, {{fast_path, "ssd"}, {slow_path, "hdd"}});

  // GetDataRootsByTier should have exactly two entries.
  const auto& by_tier = mgr->GetDataRootsByTier();
  ASSERT_EQ(2u, by_tier.size());
  ASSERT_EQ(1u, by_tier.count("ssd"));
  ASSERT_EQ(1u, by_tier.count("hdd"));

  // Each tier maps to exactly one directory.
  ASSERT_EQ(1u, mgr->GetDataRootDirsForTier("ssd").size());
  ASSERT_EQ(1u, mgr->GetDataRootDirsForTier("hdd").size());
  ASSERT_TRUE(mgr->GetDataRootDirsForTier("tape").empty());

  // GetTierForDataRoot round-trips for each canonicalized fs root.
  // The FsRootDirs returned by GetFsRootDirs() are the canonicalized fs roots.
  for (const auto& fs_root : mgr->GetFsRootDirs()) {
    const auto& tier = mgr->GetTierForDataRoot(fs_root);
    ASSERT_TRUE(tier == "ssd" || tier == "hdd")
        << "Unexpected tier for root " << fs_root << ": " << tier;
  }
}

// No paths labeled: all roots fall back to "ssd" (kDefaultStorageTier).
TEST_F(StorageTierTest, NoLabels) {
  auto path_a = GetTestPath("a");
  auto path_b = GetTestPath("b");
  ASSERT_OK(env_->CreateDir(path_a));
  ASSERT_OK(env_->CreateDir(path_b));

  // Every empty tier_by_path entry gets "ssd".
  auto mgr = MakeFsManager({path_a, path_b}, {} /* no tiers */);

  const auto& by_tier = mgr->GetDataRootsByTier();
  ASSERT_EQ(1u, by_tier.size());
  ASSERT_EQ(1u, by_tier.count("ssd"));
  ASSERT_EQ(2u, by_tier.at("ssd").size());

  for (const auto& fs_root : mgr->GetFsRootDirs()) {
    ASSERT_EQ("ssd", mgr->GetTierForDataRoot(fs_root));
  }
}

// Mixed: first path explicitly labeled "hdd", second unlabeled (falls back to "ssd").
TEST_F(StorageTierTest, MixedLabels) {
  auto hdd_path = GetTestPath("hdd_mixed");
  auto plain_path = GetTestPath("plain_mixed");
  ASSERT_OK(env_->CreateDir(hdd_path));
  ASSERT_OK(env_->CreateDir(plain_path));

  // plain_path is absent from the map so it should fall back to "ssd".
  auto mgr = MakeFsManager({hdd_path, plain_path}, {{hdd_path, "hdd"}});

  const auto& by_tier = mgr->GetDataRootsByTier();
  ASSERT_EQ(2u, by_tier.size());
  ASSERT_EQ(1u, by_tier.count("hdd"));
  ASSERT_EQ(1u, by_tier.count("ssd"));
}

// Duplicate paths: tier map should not duplicate entries.
TEST_F(StorageTierTest, DuplicatePaths) {
  auto path_d = GetTestPath("dup_d");
  ASSERT_OK(env_->CreateDir(path_d));

  auto mgr = MakeFsManager({path_d, path_d}, {{path_d, "hdd"}});

  // Duplicates are deduplicated; each tier should have at most 1 entry.
  const auto& dirs = mgr->GetDataRootDirsForTier("hdd");
  ASSERT_EQ(1u, dirs.size());
}

// Unknown root returns "ssd" (kDefaultStorageTier) from GetTierForDataRoot.
TEST_F(StorageTierTest, UnknownRootReturnsDefault) {
  auto path_e = GetTestPath("path_e");
  ASSERT_OK(env_->CreateDir(path_e));

  auto mgr = MakeFsManager({path_e}, {{path_e, "ssd"}});

  ASSERT_EQ("ssd", mgr->GetTierForDataRoot("/nonexistent/root"));
}

// A label outside the static set {ssd, hdd} is rejected at init time.
TEST_F(StorageTierTest, InvalidTierRejected) {
  auto path = GetTestPath("bad_tier");
  ASSERT_OK(env_->CreateDir(path));

  FsManagerOpts opts;
  opts.data_paths = {path};
  opts.tier_by_path = {{path, "gold"}};  // not a valid storage tier
  opts.wal_paths = {path};
  opts.server_type = kServerType;
  auto mgr = std::make_unique<FsManager>(env_.get(), opts);

  Status s = mgr->CreateInitialFileSystemLayout();
  ASSERT_NOK(s);
  ASSERT_TRUE(s.IsInvalidArgument()) << s.ToString();
  ASSERT_STR_CONTAINS(s.ToString(), "Invalid storage tier");
}

// IsValidStorageTier / ValidStorageTiers reflect the static set.
TEST_F(StorageTierTest, ValidStorageTierSet) {
  ASSERT_TRUE(FsManager::IsValidStorageTier("ssd"));
  ASSERT_TRUE(FsManager::IsValidStorageTier("hdd"));
  ASSERT_FALSE(FsManager::IsValidStorageTier("gold"));
  ASSERT_FALSE(FsManager::IsValidStorageTier(""));

  // The default tier must always be a valid tier.
  ASSERT_TRUE(FsManager::IsValidStorageTier(FsManager::kDefaultStorageTier));
}

// ParseDataDirSpec edge cases exercised via FLAGS_fs_data_dirs.
// These go through the full FsManagerOpts() constructor code path.
TEST_F(StorageTierTest, FlagParsingLabeled) {
  auto fast_path = GetTestPath("nvme_flag");
  auto slow_path = GetTestPath("hdd_flag");
  ASSERT_OK(env_->CreateDir(fast_path));
  ASSERT_OK(env_->CreateDir(slow_path));

  // Set flag to "path:ssd,path:hdd" format.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_fs_data_dirs) =
      fast_path + ":ssd," + slow_path + ":hdd";
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_fs_wal_dirs) = "";

  FsManagerOpts opts;
  opts.server_type = kServerType;
  // opts.data_paths and tier_by_path are populated by the default constructor.
  ASSERT_EQ(2u, opts.data_paths.size());
  ASSERT_EQ(fast_path, opts.data_paths[0]);
  ASSERT_EQ(slow_path, opts.data_paths[1]);
  ASSERT_EQ(2u, opts.tier_by_path.size());
  ASSERT_EQ("ssd", opts.tier_by_path[fast_path]);
  ASSERT_EQ("hdd", opts.tier_by_path[slow_path]);

  // WAL defaults to the fastest available tier (default tier "ssd").
  ASSERT_EQ(1u, opts.wal_paths.size());
  ASSERT_EQ(fast_path, opts.wal_paths[0]);
}

// If no default-tier roots exist, WAL falls back to the next configured tier.
TEST_F(StorageTierTest, FlagParsingWalFallbackToNextTier) {
  auto hdd_path_a = GetTestPath("hdd_a");
  auto hdd_path_b = GetTestPath("hdd_b");
  ASSERT_OK(env_->CreateDir(hdd_path_a));
  ASSERT_OK(env_->CreateDir(hdd_path_b));

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_fs_data_dirs) =
      hdd_path_a + ":hdd," + hdd_path_b + ":hdd";
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_fs_wal_dirs) = "";

  FsManagerOpts opts;
  opts.server_type = kServerType;

  ASSERT_EQ(2u, opts.wal_paths.size());
  ASSERT_EQ(hdd_path_a, opts.wal_paths[0]);
  ASSERT_EQ(hdd_path_b, opts.wal_paths[1]);
}

// ParseDataDirSpec: trailing colon is treated as unlabeled.
TEST_F(StorageTierTest, FlagParsingTrailingColon) {
  auto path_t = GetTestPath("trailing_colon");
  ASSERT_OK(env_->CreateDir(path_t));

  // "path:" has an empty tier suffix is treated as unlabeled.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_fs_data_dirs) = path_t + ":";
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_fs_wal_dirs) = "";

  FsManagerOpts opts;
  opts.server_type = kServerType;
  ASSERT_EQ(1u, opts.data_paths.size());
  // Trailing colon is stripped: path is just path_t, tier defaults to "ssd".
  ASSERT_EQ(path_t, opts.data_paths[0]);
  ASSERT_EQ("ssd", opts.tier_by_path[path_t]);
}

// ParseDataDirSpec: no colon is treated as unlabeled, full path preserved.
TEST_F(StorageTierTest, FlagParsingNoColon) {
  auto path_nc = GetTestPath("no_colon");
  ASSERT_OK(env_->CreateDir(path_nc));

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_fs_data_dirs) = path_nc;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_fs_wal_dirs) = "";

  FsManagerOpts opts;
  opts.server_type = kServerType;
  ASSERT_EQ(1u, opts.data_paths.size());
  ASSERT_EQ(path_nc, opts.data_paths[0]);
  ASSERT_EQ("ssd", opts.tier_by_path[path_nc]);
}

// Explicitly setting --fs_wal_dirs with a ":tier" annotation must be rejected.
TEST_F(StorageTierTest, WalDirWithTierRejected) {
  auto wal_path = GetTestPath("wal_with_tier");
  ASSERT_OK(env_->CreateDir(wal_path));

  FsManagerOpts opts;
  opts.data_paths = {wal_path};
  opts.tier_by_path = {{wal_path, "ssd"}};
  // Deliberately set a WAL path with a tier annotation.
  opts.wal_paths = {wal_path + ":ssd"};
  opts.server_type = kServerType;
  auto mgr = std::make_unique<FsManager>(env_.get(), opts);

  Status s = mgr->CreateInitialFileSystemLayout();
  ASSERT_NOK(s);
  ASSERT_TRUE(s.IsInvalidArgument()) << s.ToString();
  ASSERT_STR_CONTAINS(s.ToString(), "Storage-tier annotation");
  ASSERT_STR_CONTAINS(s.ToString(), "--fs_wal_dirs");
}

class FsManagerTestDriveFault : public YBTest {
 public:
  void SetUp() override {
    MultiDriveTestEnv* new_env = new MultiDriveTestEnv();
    env_.reset(new_env);
    new_env->AddFailedPath(GetTestPath(kFailedDrive));
    YBTest::SetUp();

    // Initialize File-System Layout
    ReinitFsManager();
    Status s = fs_manager_->CheckAndOpenFileSystemRoots();
    ASSERT_OK(fs_manager_->CreateInitialFileSystemLayout());
  }

  void ReinitFsManager() {
    ASSERT_OK(env_->CreateDirs(GetTestPath(kOkDrive)));
    ASSERT_OK(env_->CreateDirs(GetTestPath(kFailedDrive)));
    const vector<string> paths { GetTestPath(kOkDrive), GetTestPath(kFailedDrive) };
    ReinitFsManager(paths, paths);
  }

  void ReinitFsManager(const vector<string>& wal_paths, const vector<string>& data_paths) {
    // Blow away the old memtrackers first.
    fs_manager_.reset();

    FsManagerOpts opts;
    opts.wal_paths = wal_paths;
    opts.data_paths = data_paths;
    opts.server_type = kServerType;
    opts.metric_registry = &metric_registry_;
    fs_manager_ = std::make_unique<FsManager>(env_.get(), opts);
  }

  FsManager *fs_manager() const { return fs_manager_.get(); }

  const char* kServerType = "tserver_test";
  const char* kOkDrive = "dir1";
  const char* kFailedDrive = "dir2";

 private:
  std::unique_ptr<FsManager> fs_manager_;
  MetricRegistry metric_registry_;
};

TEST_F(FsManagerTestDriveFault, SingleDriveFault) {
  auto dirs = fs_manager()->GetDataRootDirs();
  EXPECT_EQ(dirs.size(), 1);
  EXPECT_TRUE(Slice(dirs[0]).starts_with(Slice(GetTestPath(kOkDrive))));
}


} // namespace yb
