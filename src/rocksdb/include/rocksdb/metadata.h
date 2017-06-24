// Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant
// of patent rights can be found in the PATENTS file in the same directory.

#ifndef ROCKSDB_INCLUDE_ROCKSDB_METADATA_H
#define ROCKSDB_INCLUDE_ROCKSDB_METADATA_H

#include <stdint.h>

#include <limits>
#include <string>
#include <vector>

#include <boost/container/small_vector.hpp>

#include "yb/util/slice.h"

#include "rocksdb/types.h"

namespace rocksdb {
struct ColumnFamilyMetaData;
struct LevelMetaData;
struct SstFileMetaData;

// The metadata that describes a column family.
struct ColumnFamilyMetaData {
  ColumnFamilyMetaData() : size(0), name("") {}
  ColumnFamilyMetaData(const std::string& _name, uint64_t _size,
                       const std::vector<LevelMetaData>&& _levels) :
      size(_size), name(_name), levels(_levels) {}

  // The size of this column family in bytes, which is equal to the sum of
  // the file size of its "levels".
  uint64_t size;
  // The number of files in this column family.
  size_t file_count;
  // The name of the column family.
  std::string name;
  // The metadata of all levels in this column family.
  std::vector<LevelMetaData> levels;
};

// The metadata that describes a level.
struct LevelMetaData {
  LevelMetaData(int _level, uint64_t _size,
                const std::vector<SstFileMetaData>&& _files) :
      level(_level), size(_size),
      files(_files) {}

  // The level which this meta data describes.
  const int level;
  // The size of this level in bytes, which is equal to the sum of
  // the file size of its "files".
  const uint64_t size;
  // The metadata of all sst files in this level.
  const std::vector<SstFileMetaData> files;
};

typedef uint32_t UserBoundaryTag;

class UserBoundaryValue {
 public:
  virtual UserBoundaryTag Tag() = 0;
  virtual yb::Slice Encode() = 0;
  virtual int CompareTo(const UserBoundaryValue& rhs) = 0;
 protected:
  ~UserBoundaryValue() {}
};

typedef std::shared_ptr<UserBoundaryValue> UserBoundaryValuePtr;
typedef boost::container::small_vector_base<UserBoundaryValuePtr> UserBoundaryValues;

struct FileBoundaryValuesBase {
  SequenceNumber seqno; // Boundary sequence number in file.
  // We expect that there will be just a few user values, so use small_vector for it.
  boost::container::small_vector<UserBoundaryValuePtr, 10> user_values;
};

template<class KeyType>
struct FileBoundaryValues : FileBoundaryValuesBase {
  KeyType key; // Boundary key in the file.
};

inline UserBoundaryValuePtr UserValueWithTag(const UserBoundaryValues& values,
                                             UserBoundaryTag tag) {
  for (const auto& value : values) {
    if (value->Tag() == tag)
      return value;
  }
  return UserBoundaryValuePtr();
}

enum class UpdateUserValueType {
  SMALLEST = 1,
  LARGEST = -1,
};

inline void UpdateUserValue(UserBoundaryValues* values,
                            const UserBoundaryValuePtr& new_value,
                            UpdateUserValueType type) {
  int compare_sign = static_cast<int>(type);
  auto tag = new_value->Tag();
  for (auto& value : *values) {
    if (value->Tag() == tag) {
      if (value->CompareTo(*new_value) * compare_sign > 0) {
        value = new_value;
      }
      return;
    }
  }
  values->push_back(new_value);
}

// The metadata that describes a SST fileset.
struct SstFileMetaData {
  typedef FileBoundaryValues<std::string> BoundaryValues;

  SstFileMetaData() {}
  SstFileMetaData(const std::string& _file_name,
                  const std::string& _path,
                  uint64_t _total_size,
                  uint64_t _base_size,
                  const BoundaryValues& _smallest,
                  const BoundaryValues& _largest,
                  bool _being_compacted) :
    total_size(_total_size), base_size(_base_size), name(_file_name),
    db_path(_path), smallest(_smallest), largest(_largest),
    being_compacted(_being_compacted) {
  }

  // Total file(s) (metadata and data (aka s-block) files) size in bytes.
  uint64_t total_size;
  // Base file size in bytes.
  uint64_t base_size;
  // The name of the file.
  std::string name;
  // The full path where the file locates.
  std::string db_path;

  BoundaryValues smallest;
  BoundaryValues largest;
  OpId last_op_id;
  bool being_compacted;  // true if the file is currently being compacted.
};

// The full set of metadata associated with each SST file.
struct LiveFileMetaData : SstFileMetaData {
  std::string column_family_name;  // Name of the column family
  int level;               // Level at which this file resides.
};

}  // namespace rocksdb

#endif  // ROCKSDB_INCLUDE_ROCKSDB_METADATA_H
