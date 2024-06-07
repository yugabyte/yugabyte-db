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

#pragma once

namespace yb {
namespace rpc {

class PBToLWMessage {
 public:
  explicit PBToLWMessage(const google::protobuf::Message& source) : source_(source) {}

  template <class LW>
  operator std::shared_ptr<LW>() const {
    auto result = rpc::MakeSharedMessage<LW>();
    CHECK_OK(result->ParseFromSlice(source_.SerializeAsString()));
    return result;
  }
 private:
  const google::protobuf::Message& source_;
};

inline PBToLWMessage PBToLW(const google::protobuf::Message& source) {
  return PBToLWMessage(source);
}

} // namespace rpc
} // namespace yb
