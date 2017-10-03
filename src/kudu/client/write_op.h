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
#ifndef KUDU_CLIENT_WRITE_OP_H
#define KUDU_CLIENT_WRITE_OP_H

#include <string>

#include "kudu/client/shared_ptr.h"
#include "kudu/common/partial_row.h"
#include "kudu/util/kudu_export.h"

namespace kudu {

class EncodedKey;

namespace client {

namespace internal {
class Batcher;
class WriteRpc;
} // namespace internal

class KuduTable;

// A write operation operates on a single table and partial row.
// The KuduWriteOperation class itself allows the batcher to get to the
// generic information that it needs to process all write operations.
//
// On its own, the class does not represent any specific change and thus cannot
// be constructed independently.
//
// KuduWriteOperation also holds shared ownership of its KuduTable to allow client's
// scope to end while the KuduWriteOperation is still alive.
class KUDU_EXPORT KuduWriteOperation {
 public:
  enum Type {
    INSERT = 1,
    UPDATE = 2,
    DELETE = 3,
  };
  virtual ~KuduWriteOperation();

  // See KuduPartialRow API for field setters, etc.
  const KuduPartialRow& row() const { return row_; }
  KuduPartialRow* mutable_row() { return &row_; }

  virtual std::string ToString() const = 0;
 protected:
  explicit KuduWriteOperation(const sp::shared_ptr<KuduTable>& table);
  virtual Type type() const = 0;

  sp::shared_ptr<KuduTable> const table_;
  KuduPartialRow row_;

 private:
  friend class internal::Batcher;
  friend class internal::WriteRpc;

  // Create and encode the key for this write (key must be set)
  //
  // Caller takes ownership of the allocated memory.
  EncodedKey* CreateKey() const;

  const KuduTable* table() const { return table_.get(); }

  // Return the number of bytes required to buffer this operation,
  // including direct and indirect data.
  int64_t SizeInBuffer() const;

  DISALLOW_COPY_AND_ASSIGN(KuduWriteOperation);
};

// A single row insert to be sent to the cluster.
// Row operation is defined by what's in the PartialRow instance here.
// Use mutable_row() to change the row being inserted
// An insert requires all key columns from the table schema to be defined.
class KUDU_EXPORT KuduInsert : public KuduWriteOperation {
 public:
  virtual ~KuduInsert();

  virtual std::string ToString() const OVERRIDE { return "INSERT " + row_.ToString(); }

 protected:
  virtual Type type() const OVERRIDE {
    return INSERT;
  }

 private:
  friend class KuduTable;
  explicit KuduInsert(const sp::shared_ptr<KuduTable>& table);
};


// A single row update to be sent to the cluster.
// Row operation is defined by what's in the PartialRow instance here.
// Use mutable_row() to change the row being updated.
// An update requires the key columns and at least one other column
// in the schema to be defined.
class KUDU_EXPORT KuduUpdate : public KuduWriteOperation {
 public:
  virtual ~KuduUpdate();

  virtual std::string ToString() const OVERRIDE { return "UPDATE " + row_.ToString(); }

 protected:
  virtual Type type() const OVERRIDE {
    return UPDATE;
  }

 private:
  friend class KuduTable;
  explicit KuduUpdate(const sp::shared_ptr<KuduTable>& table);
};


// A single row delete to be sent to the cluster.
// Row operation is defined by what's in the PartialRow instance here.
// Use mutable_row() to change the row being deleted
// A delete requires just the key columns to be defined.
class KUDU_EXPORT KuduDelete : public KuduWriteOperation {
 public:
  virtual ~KuduDelete();

  virtual std::string ToString() const OVERRIDE { return "DELETE " + row_.ToString(); }

 protected:
  virtual Type type() const OVERRIDE {
    return DELETE;
  }

 private:
  friend class KuduTable;
  explicit KuduDelete(const sp::shared_ptr<KuduTable>& table);
};

} // namespace client
} // namespace kudu

#endif
