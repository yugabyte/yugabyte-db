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

#pragma once

#include "yb/storage/frontier.h"

namespace yb::storage {

// A test implementation of UserFrontier, wrapper over simple int64_t value.
class TestUserFrontier : public UserFrontier {
 public:
  TestUserFrontier() : value_(0) {}
  explicit TestUserFrontier(uint64_t value) : value_(value) {}

  std::unique_ptr<UserFrontier> Clone() const override {
    return std::make_unique<TestUserFrontier>(*this);
  }

  void SetValue(uint64_t value) {
    value_ = value;
  }

  uint64_t Value() const {
    return value_;
  }

  std::string ToString() const override;

  void ToPB(google::protobuf::Any* pb) const override;

  bool Equals(const UserFrontier& rhs) const override {
    return value_ == down_cast<const TestUserFrontier&>(rhs).value_;
  }

  void Update(const UserFrontier& rhs, UpdateUserValueType type) override {
    auto rhs_value = down_cast<const TestUserFrontier&>(rhs).value_;
    switch (type) {
      case UpdateUserValueType::kLargest:
        value_ = std::max(value_, rhs_value);
        return;
      case UpdateUserValueType::kSmallest:
        value_ = std::min(value_, rhs_value);
        return;
    }
    FATAL_INVALID_ENUM_VALUE(UpdateUserValueType, type);
  }

  bool IsUpdateValid(const UserFrontier& rhs, UpdateUserValueType type) const override {
    auto rhs_value = down_cast<const TestUserFrontier&>(rhs).value_;
    switch (type) {
      case UpdateUserValueType::kLargest:
        return rhs_value >= value_;
      case UpdateUserValueType::kSmallest:
        return rhs_value <= value_;
    }
    FATAL_INVALID_ENUM_VALUE(UpdateUserValueType, type);
  }

  void FromOpIdPBDeprecated(const OpIdPB& op_id) override {}

  Status FromPB(const google::protobuf::Any& pb) override;

  Slice FilterAsSlice() override {
    return Slice();
  }

  void ResetFilter() override {}

  uint64_t GetHybridTimeAsUInt64() const override {
    return 0;
  }

 private:
  uint64_t value_ = 0;
};

class TestUserFrontiers : public storage::UserFrontiersBase<TestUserFrontier> {
 public:
  TestUserFrontiers(uint64_t min, uint64_t max) {
    Smallest().SetValue(min);
    Largest().SetValue(max);
  }

  UserFrontiersPtr Clone() const {
    return std::make_unique<TestUserFrontiers>(*this);
  }
};

} // namespace yb::storage
