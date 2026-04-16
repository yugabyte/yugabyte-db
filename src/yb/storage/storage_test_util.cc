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

#include "yb/storage/storage_test_util.h"

#include <google/protobuf/any.pb.h>

#include "yb/common/opid.pb.h"

namespace yb::storage {

void TestUserFrontier::ToPB(google::protobuf::Any* pb) const {
  OpIdPB value; // for test purpose we use available PB that could store int value.
  value.set_index(value_);
  pb->PackFrom(value);
}

Status TestUserFrontier::FromPB(const google::protobuf::Any& pb) {
  OpIdPB value; // for test purpose we use available PB that could store int value.
  pb.UnpackTo(&value);
  value_ = value.index();
  return Status::OK();
}

std::string TestUserFrontier::ToString() const {
  return YB_CLASS_TO_STRING(value);
}

} // namespace yb::storage
