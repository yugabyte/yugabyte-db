//  Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
//  This source code is licensed under the BSD-style license found in the
//  LICENSE file in the root directory of this source tree. An additional grant
//  of patent rights can be found in the PATENTS file in the same directory.
//
// The following only applies to changes made to this file as part of YugaByte development.
//
// Portions Copyright (c) YugaByte, Inc.
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
// Copyright (c) 2012 Facebook.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "yb/rocksdb/db.h"
#include "yb/rocksdb/db/filename.h"
#include "yb/rocksdb/env.h"

namespace rocksdb {

Status GetInfoLogList(DB* db, std::vector<std::string>* info_log_list) {
  uint64_t number = 0;
  FileType type;
  std::string path;

  if (!db) {
    return STATUS(InvalidArgument, "DB pointer is not valid");
  }

  const Options& options = db->GetOptions();
  if (!options.db_log_dir.empty()) {
    path = options.db_log_dir;
  } else {
    path = db->GetName();
  }
  InfoLogPrefix info_log_prefix(!options.db_log_dir.empty(), db->GetName());
  auto* env = options.env;
  std::vector<std::string> file_names;
  Status s = env->GetChildren(path, &file_names);

  if (!s.ok()) {
    return s;
  }

  for (auto f : file_names) {
    if (ParseFileName(f, &number, info_log_prefix.prefix, &type) &&
        (type == kInfoLogFile)) {
      info_log_list->push_back(f);
    }
  }
  return Status::OK();
}
}  // namespace rocksdb
