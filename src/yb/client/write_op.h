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
#ifndef YB_CLIENT_WRITE_OP_H
#define YB_CLIENT_WRITE_OP_H

#include <string>

#include "yb/client/shared_ptr.h"
#include "yb/common/partial_row.h"
#include "yb/util/yb_export.h"

namespace yb {

class EncodedKey;

namespace client {

namespace internal {
class Batcher;
class WriteRpc;
} // namespace internal

class YBTable;

// A write operation operates on a single table and partial row.
// The YBWriteOperation class itself allows the batcher to get to the
// generic information that it needs to process all write operations.
//
// On its own, the class does not represent any specific change and thus cannot
// be constructed independently.
//
// YBWriteOperation also holds shared ownership of its YBTable to allow client's
// scope to end while the YBWriteOperation is still alive.
class YB_EXPORT YBWriteOperation {
 public:
  enum Type {
    INSERT = 1,
    UPDATE = 2,
    DELETE = 3,
  };
  virtual ~YBWriteOperation();

  // See YBPartialRow API for field setters, etc.
  const YBPartialRow& row() const { return row_; }
  YBPartialRow* mutable_row() { return &row_; }

  virtual std::string ToString() const = 0;
 protected:
  explicit YBWriteOperation(const sp::shared_ptr<YBTable>& table);
  virtual Type type() const = 0;

  sp::shared_ptr<YBTable> const table_;
  YBPartialRow row_;

 private:
  friend class internal::Batcher;
  friend class internal::WriteRpc;

  // Create and encode the key for this write (key must be set)
  //
  // Caller takes ownership of the allocated memory.
  EncodedKey* CreateKey() const;

  const YBTable* table() const { return table_.get(); }

  // Return the number of bytes required to buffer this operation,
  // including direct and indirect data.
  int64_t SizeInBuffer() const;

  DISALLOW_COPY_AND_ASSIGN(YBWriteOperation);
};

// A single row insert to be sent to the cluster.
// Row operation is defined by what's in the PartialRow instance here.
// Use mutable_row() to change the row being inserted
// An insert requires all key columns from the table schema to be defined.
class YB_EXPORT YBInsert : public YBWriteOperation {
 public:
  virtual ~YBInsert();

  virtual std::string ToString() const OVERRIDE { return "INSERT " + row_.ToString(); }

 protected:
  virtual Type type() const OVERRIDE {
    return INSERT;
  }

 private:
  friend class YBTable;
  explicit YBInsert(const sp::shared_ptr<YBTable>& table);
};


// A single row update to be sent to the cluster.
// Row operation is defined by what's in the PartialRow instance here.
// Use mutable_row() to change the row being updated.
// An update requires the key columns and at least one other column
// in the schema to be defined.
class YB_EXPORT YBUpdate : public YBWriteOperation {
 public:
  virtual ~YBUpdate();

  virtual std::string ToString() const OVERRIDE { return "UPDATE " + row_.ToString(); }

 protected:
  virtual Type type() const OVERRIDE {
    return UPDATE;
  }

 private:
  friend class YBTable;
  explicit YBUpdate(const sp::shared_ptr<YBTable>& table);
};


// A single row delete to be sent to the cluster.
// Row operation is defined by what's in the PartialRow instance here.
// Use mutable_row() to change the row being deleted
// A delete requires just the key columns to be defined.
class YB_EXPORT YBDelete : public YBWriteOperation {
 public:
  virtual ~YBDelete();

  virtual std::string ToString() const OVERRIDE { return "DELETE " + row_.ToString(); }

 protected:
  virtual Type type() const OVERRIDE {
    return DELETE;
  }

 private:
  friend class YBTable;
  explicit YBDelete(const sp::shared_ptr<YBTable>& table);
};

} // namespace client
} // namespace yb

#endif
