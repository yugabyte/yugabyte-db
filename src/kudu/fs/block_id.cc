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

#include "kudu/fs/block_id.h"

#include <glog/logging.h>
#include <string>
#include <vector>

#include "kudu/fs/fs.pb.h"
#include "kudu/gutil/strings/join.h"

using std::string;
using std::vector;

namespace kudu {

const uint64_t BlockId::kInvalidId = 0;

string BlockId::JoinStrings(const vector<BlockId>& blocks) {
  vector<string> strings;
  strings.reserve(blocks.size());
  for (const BlockId& block : blocks) {
    strings.push_back(block.ToString());
  }
  return ::JoinStrings(strings, ",");
}

void BlockId::CopyToPB(BlockIdPB *pb) const {
  pb->set_id(id_);
}

BlockId BlockId::FromPB(const BlockIdPB& pb) {
  DCHECK(pb.has_id());
  return BlockId(pb.id());
}

} // namespace kudu
