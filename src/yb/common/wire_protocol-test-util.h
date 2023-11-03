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

#include <string>

#include "yb/common/ql_protocol.pb.h"
#include "yb/common/schema.h"

#include "yb/docdb/docdb_fwd.h"
#include "yb/docdb/docdb.pb.h"

#include "yb/util/yb_partition.h"

namespace yb {

using docdb::KeyValuePairPB;
using dockv::SubDocKey;
using dockv::DocKey;
using dockv::PrimitiveValue;

inline Schema GetSimpleTestSchema() {
  return Schema({
      ColumnSchema("key", DataType::INT32, ColumnKind::HASH),
      ColumnSchema("int_val", DataType::INT32),
      ColumnSchema("string_val", DataType::STRING, ColumnKind::VALUE, Nullable::kTrue)
  });
}

template <class WriteRequestPB, class Type>
QLWriteRequestPB* TestRow(int32_t key, Type type, WriteRequestPB* req) {
  auto wb = req->add_ql_write_batch();
  wb->set_schema_version(0);
  wb->set_type(type);

  std::string hash_key;
  YBPartition::AppendIntToKey<int32_t, uint32_t>(key, &hash_key);
  wb->set_hash_code(YBPartition::HashColumnCompoundValue(hash_key));
  wb->add_hashed_column_values()->mutable_value()->set_int32_value(key);
  return wb;
}

template <class WriteRequestPB>
QLWriteRequestPB* AddTestRowDelete(int32_t key, WriteRequestPB* req) {
  return TestRow(key, QLWriteRequestPB::QL_STMT_DELETE, req);
}

template <class WriteRequestPB>
QLWriteRequestPB* AddTestRowInsert(int32_t key, WriteRequestPB* req) {
  return TestRow(key, QLWriteRequestPB::QL_STMT_INSERT, req);
}

template <class Type, class WriteRequestPB>
QLWriteRequestPB* AddTestRow(int32_t key,
                             int32_t int_val,
                             Type type,
                             WriteRequestPB* req) {
  auto wb = TestRow(key, type, req);
  auto column_value = wb->add_column_values();
  column_value->set_column_id(kFirstColumnId + 1);
  column_value->mutable_expr()->mutable_value()->set_int32_value(int_val);
  return wb;
}

template <class Type, class WriteRequestPB>
void AddTestRow(int32_t key,
                int32_t int_val,
                const std::string& string_val,
                Type type,
                WriteRequestPB* req) {
  auto wb = AddTestRow(key, int_val, type, req);
  auto column_value = wb->add_column_values();
  column_value->set_column_id(kFirstColumnId + 2);
  column_value->mutable_expr()->mutable_value()->set_string_value(string_val);
}

template <class WriteRequestPB>
void AddTestRowInsert(int32_t key,
                      int32_t int_val,
                      WriteRequestPB* req) {
  AddTestRow(key, int_val, QLWriteRequestPB::QL_STMT_INSERT, req);
}

template <class WriteRequestPB>
void AddTestRowInsert(int32_t key,
                      int32_t int_val,
                      const std::string& string_val,
                      WriteRequestPB* req) {
  AddTestRow(key, int_val, string_val, QLWriteRequestPB::QL_STMT_INSERT, req);
}

template <class WriteRequestPB>
void AddTestRowUpdate(int32_t key,
                      int32_t int_val,
                      const std::string& string_val,
                      WriteRequestPB* req) {
  AddTestRow(key, int_val, string_val, QLWriteRequestPB::QL_STMT_UPDATE, req);
}

void AddKVToPB(int32_t key_val,
               int32_t int_val,
               const std::string& string_val,
               docdb::KeyValueWriteBatchPB* write_batch);

void AddKVToPB(int32_t key_val,
               int32_t int_val,
               const std::string& string_val,
               docdb::LWKeyValueWriteBatchPB* write_batch);

} // namespace yb
