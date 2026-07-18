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

#include "yb/storage/storage_fwd.h"

#include "yb/util/enums.h"

namespace google::protobuf {
class Any;
}

namespace yb {
class OpIdPB;
}

namespace yb::storage {

YB_DEFINE_ENUM(UpdateUserValueType, ((kSmallest, 1))((kLargest, -1)));

// When writing a batch of RocksDB records, the user could specify "frontier" values of that batch,
// such as smallest/largest Raft OpId or smallest/largest HybridTime of records in that batch. We
// maintain these values for each SSTable file and whole DB. This class defines an abstract
// interface for a single user frontier, i.e. only smallest values or only largest values, but all
// types of these values together as a tuple (e.g. OpId / hybrid time / etc.) See
// consensus_frontier.h for a concrete example.
class UserFrontier {
 public:
  virtual std::unique_ptr<UserFrontier> Clone() const = 0;
  virtual std::string ToString() const = 0;
  virtual void ToPB(google::protobuf::Any* pb) const = 0;
  virtual bool Equals(const UserFrontier& rhs) const = 0;

  // Updates the user frontier with the new values from rhs.
  virtual void Update(const UserFrontier& rhs, UpdateUserValueType type) = 0;

  // Checks if the given update is valid, i.e. that it does not move the fields of the frontier
  // (such as OpId / hybrid time) in the direction opposite to that indicated by
  // UpdateUserValueType. A "largest" update should only increase fields, and a "smallest" should
  // only decrease them. Fields that are not set in rhs are not checked.
  virtual bool IsUpdateValid(const UserFrontier& rhs, UpdateUserValueType type) const = 0;

  // Should return value that will be passed to iterator replacer.
  virtual Slice FilterAsSlice() = 0;

  virtual void ResetFilter() = 0;

  // Returns true if this frontier dominates another frontier, i.e. if we update this frontier
  // with the values from the other one in the direction specified by update_type, nothing will
  // change. This is used to check invariants.
  bool Dominates(const UserFrontier& rhs, UpdateUserValueType update_type) const;

  virtual void FromOpIdPBDeprecated(const OpIdPB& op_id) = 0;
  virtual yb::Status FromPB(const google::protobuf::Any& pb) = 0;

  virtual uint64_t GetHybridTimeAsUInt64() const = 0;

  virtual ~UserFrontier() {}

  static void Update(const UserFrontier* rhs, UpdateUserValueType type, UserFrontierPtr* out);
};

inline bool operator==(const UserFrontier& lhs, const UserFrontier& rhs) {
  return lhs.Equals(rhs);
}

inline bool operator!=(const UserFrontier& lhs, const UserFrontier& rhs) {
  return !lhs.Equals(rhs);
}

inline std::ostream& operator<<(std::ostream& out, const UserFrontier& frontier) {
  return out << frontier.ToString();
}

// Abstract interface to a pair of user defined frontiers - smallest and largest.
class UserFrontiers {
 public:
  virtual UserFrontiersPtr Clone() const = 0;
  std::string ToString() const;
  virtual const UserFrontier& Smallest() const = 0;
  virtual const UserFrontier& Largest() const = 0;

  virtual UserFrontier& Smallest() = 0;
  virtual UserFrontier& Largest() = 0;

  virtual void MergeFrontiers(const UserFrontiers& rhs) = 0;

  virtual ~UserFrontiers() {}
};

void UpdateFrontiers(UserFrontiersPtr& frontiers, const UserFrontiers& update);

template<class Frontier>
class UserFrontiersBase : public UserFrontiers {
 public:
  const Frontier& Smallest() const override { return smallest_; }
  const Frontier& Largest() const override { return largest_; }

  Frontier& Smallest() override { return smallest_; }
  Frontier& Largest() override { return largest_; }

  UserFrontiersPtr Clone() const override {
    return std::make_unique<UserFrontiersBase>(*this);
  }

  void MergeFrontiers(const UserFrontiers& pre_rhs) override {
    const auto& rhs = down_cast<const UserFrontiersBase&>(pre_rhs);
    smallest_.Update(rhs.smallest_, UpdateUserValueType::kSmallest);
    largest_.Update(rhs.largest_, UpdateUserValueType::kLargest);
  }

 private:
  Frontier smallest_;
  Frontier largest_;
};

inline bool operator==(const UserFrontiers& lhs, const UserFrontiers& rhs) {
  return lhs.Smallest() == rhs.Smallest() && lhs.Largest() == rhs.Largest();
}

void UpdateUserFrontier(
    UserFrontierPtr* value, const UserFrontierPtr& update, UpdateUserValueType type);
void UpdateUserFrontier(UserFrontierPtr* value, UserFrontierPtr&& update, UpdateUserValueType type);

} // namespace yb::storage
