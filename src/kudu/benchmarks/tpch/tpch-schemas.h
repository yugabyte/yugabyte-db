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
// Inline functions to create the TPC-H schemas
#ifndef KUDU_BENCHMARKS_TPCH_SCHEMAS_H
#define KUDU_BENCHMARKS_TPCH_SCHEMAS_H

#include <string>
#include <vector>

#include "kudu/client/schema.h"
#include "kudu/util/status.h"

namespace kudu {
namespace tpch {

static const char* const kOrderKeyColName = "l_orderkey";
static const char* const kLineNumberColName = "l_linenumber";
static const char* const kPartKeyColName = "l_partkey";
static const char* const kSuppKeyColName = "l_suppkey";
static const char* const kQuantityColName = "l_quantity";
static const char* const kExtendedPriceColName = "l_extendedprice";
static const char* const kDiscountColName = "l_discount";
static const char* const kTaxColName = "l_tax";
static const char* const kReturnFlagColName = "l_returnflag";
static const char* const kLineStatusColName = "l_linestatus";
static const char* const kShipDateColName = "l_shipdate";
static const char* const kCommitDateColName = "l_commitdate";
static const char* const kReceiptDateColName = "l_receiptdate";
static const char* const kShipInstructColName = "l_shipinstruct";
static const char* const kShipModeColName = "l_shipmode";
static const char* const kCommentColName = "l_comment";

static const client::KuduColumnStorageAttributes::EncodingType kPlainEncoding =
  client::KuduColumnStorageAttributes::PLAIN_ENCODING;

static const client::KuduColumnSchema::DataType kInt64 =
    client::KuduColumnSchema::INT64;
static const client::KuduColumnSchema::DataType kInt32 =
    client::KuduColumnSchema::INT32;
static const client::KuduColumnSchema::DataType kString =
    client::KuduColumnSchema::STRING;
static const client::KuduColumnSchema::DataType kDouble =
    client::KuduColumnSchema::DOUBLE;

enum {
  kOrderKeyColIdx = 0,
  kLineNumberColIdx,
  kPartKeyColIdx,
  kSuppKeyColIdx,
  kQuantityColIdx,
  kExtendedPriceColIdx,
  kDiscountColIdx,
  kTaxColIdx,
  kReturnFlagColIdx,
  kLineStatusColIdx,
  kShipDateColIdx,
  kCommitDateColIdx,
  kReceiptDateColIdx,
  kShipInstructColIdx,
  kShipModeColIdx,
  kCommentColIdx
};

inline client::KuduSchema CreateLineItemSchema() {
  client::KuduSchemaBuilder b;
  client::KuduSchema s;

  b.AddColumn(kOrderKeyColName)->Type(kInt64)->NotNull();
  b.AddColumn(kLineNumberColName)->Type(kInt32)->NotNull();
  b.AddColumn(kPartKeyColName)->Type(kInt32)->NotNull();
  b.AddColumn(kSuppKeyColName)->Type(kInt32)->NotNull();
  b.AddColumn(kQuantityColName)->Type(kInt32)->NotNull(); // decimal?
  b.AddColumn(kExtendedPriceColName)->Type(kDouble)->NotNull();
  b.AddColumn(kDiscountColName)->Type(kDouble)->NotNull();
  b.AddColumn(kTaxColName)->Type(kDouble)->NotNull();
  b.AddColumn(kReturnFlagColName)->Type(kString)->NotNull()->Encoding(kPlainEncoding);
  b.AddColumn(kLineStatusColName)->Type(kString)->NotNull()->Encoding(kPlainEncoding);
  b.AddColumn(kShipDateColName)->Type(kString)->NotNull()->Encoding(kPlainEncoding);
  b.AddColumn(kCommitDateColName)->Type(kString)->NotNull()->Encoding(kPlainEncoding);
  b.AddColumn(kReceiptDateColName)->Type(kString)->NotNull()->Encoding(kPlainEncoding);
  b.AddColumn(kShipInstructColName)->Type(kString)->NotNull()->Encoding(kPlainEncoding);
  b.AddColumn(kShipModeColName)->Type(kString)->NotNull()->Encoding(kPlainEncoding);
  b.AddColumn(kCommentColName)->Type(kString)->NotNull()->Encoding(kPlainEncoding);

  b.SetPrimaryKey({ kOrderKeyColName, kLineNumberColName });

  CHECK_OK(b.Build(&s));
  return s;
}

inline std::vector<std::string> GetTpchQ1QueryColumns() {
  return { kShipDateColName,
           kReturnFlagColName,
           kLineStatusColName,
           kQuantityColName,
           kExtendedPriceColName,
           kDiscountColName,
           kTaxColName };
}

} // namespace tpch
} // namespace kudu
#endif
