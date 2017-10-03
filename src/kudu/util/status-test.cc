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
// Some portions Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include <glog/logging.h>
#include <gtest/gtest.h>

#include <errno.h>
#include <vector>
#include "kudu/util/status.h"
#include "kudu/util/test_util.h"

using std::string;

namespace kudu {

TEST(StatusTest, TestPosixCode) {
  Status ok = Status::OK();
  ASSERT_EQ(0, ok.posix_code());
  Status file_error = Status::IOError("file error", Slice(), ENOTDIR);
  ASSERT_EQ(ENOTDIR, file_error.posix_code());
}

TEST(StatusTest, TestToString) {
  Status file_error = Status::IOError("file error", Slice(), ENOTDIR);
  ASSERT_EQ(string("IO error: file error (error 20)"), file_error.ToString());
}

TEST(StatusTest, TestClonePrepend) {
  Status file_error = Status::IOError("file error", "msg2", ENOTDIR);
  Status appended = file_error.CloneAndPrepend("Heading");
  ASSERT_EQ(string("IO error: Heading: file error: msg2 (error 20)"), appended.ToString());
}

TEST(StatusTest, TestCloneAppend) {
  Status remote_error = Status::RemoteError("Application error");
  Status appended = remote_error.CloneAndAppend(Status::NotFound("Unknown tablet").ToString());
  ASSERT_EQ(string("Remote error: Application error: Not found: Unknown tablet"),
            appended.ToString());
}

TEST(StatusTest, TestMemoryUsage) {
  ASSERT_EQ(0, Status::OK().memory_footprint_excluding_this());
  ASSERT_GT(Status::IOError(
      "file error", "some other thing", ENOTDIR).memory_footprint_excluding_this(), 0);
}

TEST(StatusTest, TestMoveConstructor) {
  // OK->OK move should do nothing.
  {
    Status src = Status::OK();
    Status dst = std::move(src);
    ASSERT_OK(src);
    ASSERT_OK(dst);
  }

  // Moving a not-OK status into a new one should make the moved status
  // "OK".
  {
    Status src = Status::NotFound("foo");
    Status dst = std::move(src);
    ASSERT_OK(src);
    ASSERT_EQ("Not found: foo", dst.ToString());
  }
}

TEST(StatusTest, TestMoveAssignment) {
  // OK->Bad move should clear the source status and also make the
  // destination status OK.
  {
    Status src = Status::OK();
    Status dst = Status::NotFound("orig dst");
    dst = std::move(src);
    ASSERT_OK(src);
    ASSERT_OK(dst);
  }

  // Bad->Bad move.
  {
    Status src = Status::NotFound("orig src");
    Status dst = Status::NotFound("orig dst");
    dst = std::move(src);
    ASSERT_OK(src);
    ASSERT_EQ("Not found: orig src", dst.ToString());
  }

  // Bad->OK move
  {
    Status src = Status::NotFound("orig src");
    Status dst = Status::OK();
    dst = std::move(src);
    ASSERT_OK(src);
    ASSERT_EQ("Not found: orig src", dst.ToString());
  }
}


}  // namespace kudu
