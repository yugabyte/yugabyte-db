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

#ifndef YB_COMMON_WIRE_PROTOCOL_TEST_UTIL_H_
#define YB_COMMON_WIRE_PROTOCOL_TEST_UTIL_H_

#include "yb/common/wire_protocol.h"

#include <string>

#include "yb/common/partial_row.h"
#include "yb/common/row.h"
#include "yb/common/ql_protocol.pb.h"
#include "yb/docdb/docdb.pb.h"
#include "yb/docdb/doc_key.h"
#include "yb/util/yb_partition.h"

namespace yb {

using docdb::KeyValuePairPB;
using docdb::SubDocKey;
using docdb::DocKey;
using docdb::PrimitiveValue;
using docdb::ValueType;

inline Schema GetSimpleTestSchema() {
  return Schema({ ColumnSchema("key", INT32, false, true),
                  ColumnSchema("int_val", INT32),
                  ColumnSchema("string_val", STRING, true) },
                1);
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
                const string& string_val,
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
                      const string& string_val,
                      WriteRequestPB* req) {
  AddTestRow(key, int_val, string_val, QLWriteRequestPB::QL_STMT_INSERT, req);
}

template <class WriteRequestPB>
void AddTestRowUpdate(int32_t key,
                      int32_t int_val,
                      const string& string_val,
                      WriteRequestPB* req) {
  AddTestRow(key, int_val, string_val, QLWriteRequestPB::QL_STMT_UPDATE, req);
}

inline void AddKVToPB(int32_t key_val,
                      int32_t int_val,
                      const string& string_val,
                      docdb::KeyValueWriteBatchPB* write_batch) {
  const ColumnId int_val_col_id(kFirstColumnId + 1);
  const ColumnId string_val_col_id(kFirstColumnId + 2);

  auto add_kv_pair =
    [&](const SubDocKey &subdoc_key, const PrimitiveValue &primitive_value) {
        KeyValuePairPB *const kv = write_batch->add_write_pairs();
        kv->set_key(subdoc_key.Encode().ToStringBuffer());
        kv->set_value(primitive_value.ToValue());
    };

  std::string hash_key;
  YBPartition::AppendIntToKey<int32_t, uint32_t>(key_val, &hash_key);
  auto hash = YBPartition::HashColumnCompoundValue(hash_key);
  const DocKey doc_key(hash, {PrimitiveValue::Int32(key_val)}, {});
  add_kv_pair(SubDocKey(doc_key, PrimitiveValue(int_val_col_id)),
              PrimitiveValue::Int32(int_val));
  add_kv_pair(SubDocKey(doc_key, PrimitiveValue(string_val_col_id)),
             PrimitiveValue(string_val));
}

} // namespace yb

#endif // YB_COMMON_WIRE_PROTOCOL_TEST_UTIL_H_
