// Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant
// of patent rights can be found in the PATENTS file in the same directory.
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

#pragma once

#include <stdint.h>

#include <limits>
#include <string>
#include <vector>

#include <boost/container/small_vector.hpp>

#include "yb/common/hybrid_time.h"

#include "yb/gutil/casts.h"

#include "yb/util/byte_buffer.h"
#include "yb/util/clone_ptr.h"
#include "yb/util/slice.h"
#include "yb/util/enums.h"

#include "yb/rocksdb/types.h"

namespace google { namespace protobuf {
class Any;
}
}

namespace yb {
class OpIdPB;
}

namespace rocksdb {
struct ColumnFamilyMetaData;
struct LevelMetaData;
struct SstFileMetaData;

class UserFrontier;

// Frontier should be copyable, but should still preserve its polymorphic nature. We cannot use
// shared_ptr here, because we are planning to modify the copied value. If we used shared_ptr and
// modified the copied value, the original value would also change.
typedef yb::clone_ptr<UserFrontier> UserFrontierPtr;

void UpdateUserFrontier(UserFrontierPtr* value, const UserFrontierPtr& update,
                        UpdateUserValueType type);
void UpdateUserFrontier(UserFrontierPtr* value, UserFrontierPtr&& update,
                        UpdateUserValueType type);

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

  virtual void FromOpIdPBDeprecated(const yb::OpIdPB& op_id) = 0;
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
  virtual std::unique_ptr<UserFrontiers> Clone() const = 0;
  std::string ToString() const;
  virtual const UserFrontier& Smallest() const = 0;
  virtual const UserFrontier& Largest() const = 0;

  virtual void MergeFrontiers(const UserFrontiers& rhs) = 0;

  virtual ~UserFrontiers() {}
};

template<class Frontier>
class UserFrontiersBase : public rocksdb::UserFrontiers {
 public:
  const rocksdb::UserFrontier& Smallest() const override { return smallest_; }
  const rocksdb::UserFrontier& Largest() const override { return largest_; }

  Frontier& Smallest() { return smallest_; }
  Frontier& Largest() { return largest_; }

  std::unique_ptr<rocksdb::UserFrontiers> Clone() const override {
    return std::make_unique<UserFrontiersBase>(*this);
  }

  void MergeFrontiers(const UserFrontiers& pre_rhs) override {
    const auto& rhs = down_cast<const UserFrontiersBase&>(pre_rhs);
    smallest_.Update(rhs.smallest_, rocksdb::UpdateUserValueType::kSmallest);
    largest_.Update(rhs.largest_, rocksdb::UpdateUserValueType::kLargest);
  }

 private:
  Frontier smallest_;
  Frontier largest_;
};

inline bool operator==(const UserFrontiers& lhs, const UserFrontiers& rhs) {
  return lhs.Smallest() == rhs.Smallest() && lhs.Largest() == rhs.Largest();
}

typedef uint32_t UserBoundaryTag;

struct UserBoundaryValueRef {
  UserBoundaryTag tag;
  Slice value;

  Slice AsSlice() const {
    return value;
  }
};

struct UserBoundaryValue {
  static constexpr size_t kBufferSize = 128;
  using Value = yb::ByteBuffer<kBufferSize>;

  UserBoundaryTag tag;
  Value value;

  UserBoundaryValue() = default;

  UserBoundaryValue(UserBoundaryTag tag_, const Slice& value_)
      : tag(tag_), value(value_) {
  }

  explicit UserBoundaryValue(const UserBoundaryValueRef& ref)
      : tag(ref.tag), value(ref.value) {
  }

  yb::Slice AsSlice() const {
    return value.AsSlice();
  }

  int CompareTo(const Slice& rhs) {
    return AsSlice().compare(rhs);
  }

  int CompareTo(const UserBoundaryValueRef& rhs) {
    return CompareTo(rhs.AsSlice());
  }

  int CompareTo(const UserBoundaryValue& rhs) {
    return CompareTo(rhs.AsSlice());
  }
};

typedef boost::container::small_vector_base<UserBoundaryValue> UserBoundaryValues;
typedef boost::container::small_vector_base<UserBoundaryValueRef> UserBoundaryValueRefs;

struct FileBoundaryValuesBase {
  SequenceNumber seqno; // Boundary sequence number in file.
  UserFrontierPtr user_frontier;
  // We expect that there will be just a few user values, so use small_vector for it.
  boost::container::small_vector<UserBoundaryValue, 10> user_values;

  std::string ToString() const;
};

struct FileBoundaryValueRefs {
  SequenceNumber seqno; // Boundary sequence number in file.
  UserFrontierPtr user_frontier;
  // We expect that there will be just a few user values, so use small_vector for it.
  boost::container::small_vector<UserBoundaryValueRef, 10> user_values;

  std::string ToString() const;
};

template<class KeyType>
struct FileBoundaryValues : FileBoundaryValuesBase {
  KeyType key; // Boundary key in the file.
};

inline const UserBoundaryValue* TEST_UserValueWithTag(const UserBoundaryValues& values,
                                                      UserBoundaryTag tag) {
  for (const auto& value : values) {
    if (value.tag == tag)
      return &value;
  }
  return nullptr;
}

void UpdateUserValue(
    UserBoundaryValues* values, UserBoundaryTag tag, const Slice& new_value,
    UpdateUserValueType type);

void UpdateUserValue(
    UserBoundaryValues* values, const UserBoundaryValueRef& new_value, UpdateUserValueType type);

void UpdateUserValue(
    UserBoundaryValues* values, const UserBoundaryValue& new_value, UpdateUserValueType type);

void UpdateUserValues(
    const UserBoundaryValueRefs& source, UpdateUserValueType type, UserBoundaryValues* values);

void UpdateUserValues(
    const UserBoundaryValues& source, UpdateUserValueType type, UserBoundaryValues* values);

// The metadata that describes a SST fileset.
struct SstFileMetaData {
  typedef FileBoundaryValues<std::string> BoundaryValues;

  SstFileMetaData(uint64_t name_id_,
                  const std::string& _path,
                  uint64_t _total_size,
                  uint64_t _base_size,
                  uint64_t _uncompressed_size,
                  const BoundaryValues& _smallest,
                  const BoundaryValues& _largest,
                  bool _imported,
                  bool _being_compacted)
      : total_size(_total_size),
        base_size(_base_size),
        uncompressed_size(_uncompressed_size),
        name_id(name_id_),
        db_path(_path),
        smallest(_smallest),
        largest(_largest),
        imported(_imported),
        being_compacted(_being_compacted) {
  }

  // Total file(s) (metadata and data (aka s-block) files) size in bytes.
  uint64_t total_size = 0;
  // Base file size in bytes.
  uint64_t base_size = 0;
  // Total uncompressed size in bytes.
  uint64_t uncompressed_size = 0;
  // The name id of the file.
  uint64_t name_id;
  // The full path where the file locates.
  std::string db_path;

  BoundaryValues smallest;
  BoundaryValues largest;
  bool imported = false;
  bool being_compacted = false; // true if the file is currently being compacted.

  std::string Name() const;
  std::string BaseFilePath() const;
  std::string DataFilePath() const;
};

// The metadata that describes a level.
struct LevelMetaData {
  LevelMetaData(int _level, uint64_t _size,
                const std::vector<SstFileMetaData>&& _files)
      : level(_level),
        size(_size),
        files(_files) {}

  // The level which this meta data describes.
  const int level = 0;
  // The size of this level in bytes, which is equal to the sum of
  // the file size of its "files".
  const uint64_t size = 0;
  // The metadata of all sst files in this level.
  const std::vector<SstFileMetaData> files;
};

// The metadata that describes a column family.
struct ColumnFamilyMetaData {
  ColumnFamilyMetaData() : size(0), name("") {}

  // The size of this column family in bytes, which is equal to the sum of
  // the file size of its "levels".
  uint64_t size = 0;
  // The number of files in this column family.
  size_t file_count = 0;
  // The name of the column family.
  std::string name;
  // The metadata of all levels in this column family.
  std::vector<LevelMetaData> levels;
};

// The full set of metadata associated with each SST file.
struct LiveFileMetaData : SstFileMetaData {
  LiveFileMetaData(
      const std::string& _column_family_name,
      int _level,
      uint64_t _name_id,
      const std::string& _path,
      uint64_t _total_size,
      uint64_t _base_size,
      uint64_t _uncompressed_size,
      const BoundaryValues& _smallest,
      const BoundaryValues& _largest,
      bool _imported,
      bool _being_compacted)
      : SstFileMetaData(
            _name_id, _path, _total_size, _base_size, _uncompressed_size, _smallest, _largest,
            _imported, _being_compacted),
        column_family_name(_column_family_name),
        level(_level) {}

  std::string column_family_name;  // Name of the column family
  int level;                       // Level at which this file resides.

  std::string ToString() const;
};

}  // namespace rocksdb
