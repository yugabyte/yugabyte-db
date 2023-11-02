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

#include "yb/master/master_types.pb.h"
#include "yb/util/cow_object.h"

namespace yb::master {

// This class is a base wrapper around the protos that get serialized in the data column of the
// sys_catalog. Subclasses of this will provide convenience getter/setter methods around the
// protos and instances of these will be wrapped around CowObjects and locks for access and
// modifications.
template <class DataEntryPB, SysRowEntryType entry_type>
struct Persistent {
  // Type declaration to be used in templated read/write methods. We are using typename
  // Class::data_type in templated methods for figuring out the type we need.
  typedef DataEntryPB data_type;

  // Subclasses of this need to provide a valid value of the entry type through
  // the template class argument.
  static SysRowEntryType type() { return entry_type; }

  // The proto that is persisted in the sys_catalog.
  DataEntryPB pb;
};

// This class is a base wrapper around accessors for the persistent proto data, through CowObject.
// The locks are taken on subclasses of this class, around the object returned from metadata().
template <class PersistentDataEntryPB>
class MetadataCowWrapper {
 public:
  // Type declaration for use in the Lock classes.
  typedef PersistentDataEntryPB CowState;
  typedef CowWriteLock<CowState> WriteLock;
  typedef CowReadLock<CowState> ReadLock;

  // This method should return the id to be written into the sys_catalog id column.
  virtual const std::string& id() const = 0;

  // Pretty printing.
  virtual std::string ToString() const {
    return Format("Object type = $0 (id = $1)", PersistentDataEntryPB::type(), id());
  }

  // Access the persistent metadata. Typically you should use
  // MetadataLock to gain access to this data.
  const CowObject<PersistentDataEntryPB>& metadata() const { return metadata_; }
  CowObject<PersistentDataEntryPB>* mutable_metadata() { return &metadata_; }

  ReadLock LockForRead() const { return ReadLock(&metadata()); }

  WriteLock LockForWrite() { return WriteLock(mutable_metadata()); }

  const auto& old_pb() const { return metadata_.state().pb; }

  const auto& new_pb() const { return metadata_.dirty().pb; }

  static auto type() { return CowState::type(); }

  virtual void Load(const decltype(PersistentDataEntryPB::pb)& metadata) {
    VLOG_WITH_FUNC(2) << "Loading " << type() << " data: " << metadata.DebugString();

    auto l = LockForWrite();
    l.mutable_data()->pb.CopyFrom(metadata);
    l.Commit();
  }

  virtual void Clear() {
    auto l = LockForWrite();
    l.mutable_data()->pb.Clear();
    l.Commit();
  }

 protected:
  virtual ~MetadataCowWrapper() = default;
  CowObject<PersistentDataEntryPB> metadata_;
};

// Singleton PersistentDataEntry which does not use the ID field.
template <class PersistentDataEntryPB>
class SingletonMetadataCowWrapper : public MetadataCowWrapper<PersistentDataEntryPB> {
 public:
  const std::string& id() const override {
    static const std::string fake_id;
    return fake_id;
  }
};

}  // namespace yb::master
