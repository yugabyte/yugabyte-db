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
#include "kudu/fs/block_manager_util.h"

#include <string>
#include <vector>

#include <google/protobuf/repeated_field.h>
#include <gtest/gtest.h>

#include "kudu/fs/fs.pb.h"
#include "kudu/gutil/stl_util.h"
#include "kudu/gutil/strings/substitute.h"
#include "kudu/util/path_util.h"
#include "kudu/util/test_util.h"

namespace kudu {
namespace fs {

using google::protobuf::RepeatedPtrField;
using std::string;
using std::vector;
using strings::Substitute;

TEST_F(KuduTest, Lifecycle) {
  string kType = "asdf";
  string kFileName = JoinPathSegments(GetTestDataDirectory(), "foo");
  string kUuid = "a_uuid";

  // Test that the metadata file was created.
  {
    PathInstanceMetadataFile file(env_.get(), kType, kFileName);
    ASSERT_OK(file.Create(kUuid, { kUuid }));
  }
  ASSERT_TRUE(env_->FileExists(kFileName));

  // Test that we could open and parse it.
  {
    PathInstanceMetadataFile file(env_.get(), kType, kFileName);
    ASSERT_OK(file.LoadFromDisk());
    PathInstanceMetadataPB* md = file.metadata();
    ASSERT_EQ(kType, md->block_manager_type());
    const PathSetPB& path_set = md->path_set();
    ASSERT_EQ(kUuid, path_set.uuid());
    ASSERT_EQ(1, path_set.all_uuids_size());
    ASSERT_EQ(kUuid, path_set.all_uuids(0));
  }

  // Test that expecting a different type of block manager fails.
  {
    PathInstanceMetadataFile file(env_.get(), "other type", kFileName);
    PathInstanceMetadataPB pb;
    ASSERT_TRUE(file.LoadFromDisk().IsIOError());
  }
}

TEST_F(KuduTest, Locking) {
  string kType = "asdf";
  string kFileName = JoinPathSegments(GetTestDataDirectory(), "foo");
  string kUuid = "a_uuid";

  PathInstanceMetadataFile file(env_.get(), kType, kFileName);
  ASSERT_OK(file.Create(kUuid, { kUuid }));

  PathInstanceMetadataFile first(env_.get(), kType, kFileName);
  ASSERT_OK(first.LoadFromDisk());
  ASSERT_OK(first.Lock());

  ASSERT_DEATH({
    PathInstanceMetadataFile second(env_.get(), kType, kFileName);
    CHECK_OK(second.LoadFromDisk());
    CHECK_OK(second.Lock());
  }, "Could not lock");

  ASSERT_OK(first.Unlock());
  ASSERT_DEATH({
    PathInstanceMetadataFile second(env_.get(), kType, kFileName);
    CHECK_OK(second.LoadFromDisk());
    Status s = second.Lock();
    if (s.ok()) {
      LOG(FATAL) << "Lock successfully acquired";
    } else {
      LOG(FATAL) << "Could not lock: " << s.ToString();
    }
  }, "Lock successfully acquired");
}

static void RunCheckIntegrityTest(Env* env,
                                  const vector<PathSetPB>& path_sets,
                                  const string& expected_status_string) {
  vector<PathInstanceMetadataFile*> instances;
  ElementDeleter deleter(&instances);

  int i = 0;
  for (const PathSetPB& ps : path_sets) {
    gscoped_ptr<PathInstanceMetadataFile> instance(
        new PathInstanceMetadataFile(env, "asdf", Substitute("$0", i)));
    gscoped_ptr<PathInstanceMetadataPB> metadata(new PathInstanceMetadataPB());
    metadata->set_block_manager_type("asdf");
    metadata->set_filesystem_block_size_bytes(1);
    metadata->mutable_path_set()->CopyFrom(ps);
    instance->SetMetadataForTests(metadata.Pass());
    instances.push_back(instance.release());
    i++;
  }

  EXPECT_EQ(expected_status_string,
            PathInstanceMetadataFile::CheckIntegrity(instances).ToString());
}

TEST_F(KuduTest, CheckIntegrity) {
  vector<string> uuids = { "fee", "fi", "fo", "fum" };
  RepeatedPtrField<string> kAllUuids(uuids.begin(), uuids.end());

  // Initialize path_sets to be fully consistent.
  vector<PathSetPB> path_sets(kAllUuids.size());
  for (int i = 0; i < path_sets.size(); i++) {
    PathSetPB& ps = path_sets[i];
    ps.set_uuid(kAllUuids.Get(i));
    ps.mutable_all_uuids()->CopyFrom(kAllUuids);
  }

  {
    // Test consistent path sets.
    EXPECT_NO_FATAL_FAILURE(RunCheckIntegrityTest(env_.get(), path_sets, "OK"));
  }
  {
    // Test where two path sets claim the same UUID.
    vector<PathSetPB> path_sets_copy(path_sets);
    path_sets_copy[1].set_uuid(path_sets_copy[0].uuid());
    EXPECT_NO_FATAL_FAILURE(RunCheckIntegrityTest(
        env_.get(), path_sets_copy,
        "IO error: File 1 claimed uuid fee already claimed by file 0"));
  }
  {
    // Test where the path sets have duplicate UUIDs.
    vector<PathSetPB> path_sets_copy(path_sets);
    for (PathSetPB& ps : path_sets_copy) {
      ps.add_all_uuids("fee");
    }
    EXPECT_NO_FATAL_FAILURE(RunCheckIntegrityTest(
        env_.get(), path_sets_copy,
        "IO error: File 0 has duplicate uuids: fee,fi,fo,fum,fee"));
  }
  {
    // Test where a path set claims a UUID that isn't in all_uuids.
    vector<PathSetPB> path_sets_copy(path_sets);
    path_sets_copy[1].set_uuid("something_else");
    EXPECT_NO_FATAL_FAILURE(RunCheckIntegrityTest(
        env_.get(), path_sets_copy,
        "IO error: File 1 claimed uuid something_else which is not in "
        "all_uuids (fee,fi,fo,fum)"));
  }
  {
    // Test where a path set claims a different all_uuids.
    vector<PathSetPB> path_sets_copy(path_sets);
    path_sets_copy[1].add_all_uuids("another_uuid");
    EXPECT_NO_FATAL_FAILURE(RunCheckIntegrityTest(
        env_.get(), path_sets_copy,
        "IO error: File 1 claimed all_uuids fee,fi,fo,fum,another_uuid but "
        "file 0 claimed all_uuids fee,fi,fo,fum"));
  }
}

} // namespace fs
} // namespace kudu
