// Copyright (c) YugaByte, Inc.
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

#include <map>
#include <boost/algorithm/string.hpp>

#include "yb/common/common.pb.h"
#include "yb/tools/yb-generate_partitions.h"
#include "yb/util/date_time.h"
#include "yb/util/enums.h"
#include "yb/util/stol_utils.h"
#include "yb/util/status.h"
#include "yb/util/timestamp.h"

namespace yb {
namespace tools {

using client::YBSchema;
using client::YBTableName;
using google::protobuf::RepeatedPtrField;
using master::TabletLocationsPB;
using std::map;
using std::string;
using std::vector;

YBPartitionGenerator::YBPartitionGenerator(const YBTableName& table_name,
                                           const vector<string>& master_addresses) :
    table_name_(table_name),
    master_addresses_(master_addresses) {
}

Status YBPartitionGenerator::Init() {
  client::YBClientBuilder builder;
  for (const string& master_address : master_addresses_) {
    builder.add_master_server_addr(master_address);
  }
  RETURN_NOT_OK(builder.Build(&client_));
  RETURN_NOT_OK(client_->OpenTable(table_name_, &table_));
  RepeatedPtrField<TabletLocationsPB> tablets;
  RETURN_NOT_OK(client_->GetTablets(table_name_, /* max_tablets */ 0, &tablets));
  RETURN_NOT_OK(BuildTabletMap(tablets));
  return Status::OK();
}

Status YBPartitionGenerator::BuildTabletMap(const RepeatedPtrField<TabletLocationsPB>& tablets) {
  for (const TabletLocationsPB& tablet : tablets) {
    tablet_map_.emplace(tablet.partition().partition_key_start(), tablet);
  }
  return Status::OK();
}

Status YBPartitionGenerator::LookupTabletId(const string& row, string* tablet_id,
                                            string* partition_key) {
  CsvTokenizer tokenizer = Tokenize(row);
  return LookupTabletIdWithTokenizer(tokenizer, tablet_id, partition_key);
}

Status YBPartitionGenerator::LookupTabletIdWithTokenizer(const CsvTokenizer& tokenizer,
                                                         string* tablet_id, string* partition_key) {
  const Schema &schema = table_->InternalSchema();
  size_t ncolumns = std::distance(tokenizer.begin(), tokenizer.end());
  if (ncolumns < schema.num_hash_key_columns()) {
    return STATUS_SUBSTITUTE(InvalidArgument, "row doesn't have enough columns for primary "
        "key, found: $0 need atleast $1", ncolumns, schema.num_hash_key_columns());
  }

  std::unique_ptr<client::YBqlReadOp> yb_op(table_->NewQLRead());
  QLReadRequestPB* ql_read = yb_op->mutable_request();

  // Set the hash column values to compute the partition key.
  auto it = tokenizer.begin();
  for (int i = 0; i < schema.num_hash_key_columns(); i++, it++) {
    if (IsNull(*it)) {
      return STATUS_SUBSTITUTE(IllegalState, "Primary key cannot be null: $0", *it);
    }

    DataType column_type = schema.column(i).type_info()->type();
    int32_t int_val;
    int64_t long_val;
    auto* col_expr_pb = ql_read->add_hashed_column_values();

    switch(column_type) {
      case DataType::INT8:
        RETURN_NOT_OK(util::CheckedStoi(*it, &int_val));
        col_expr_pb->mutable_value()->set_int8_value(int_val);
        break;
      case DataType::INT16:
        RETURN_NOT_OK(util::CheckedStoi(*it, &int_val));
        col_expr_pb->mutable_value()->set_int16_value(int_val);
        break;
      case DataType::INT32:
        RETURN_NOT_OK(util::CheckedStoi(*it, &int_val));
        col_expr_pb->mutable_value()->set_int32_value(int_val);
        break;
      case DataType::INT64:
        RETURN_NOT_OK(util::CheckedStoll(*it, &long_val));
        col_expr_pb->mutable_value()->set_int64_value(long_val);
        break;
      case DataType::STRING:
        col_expr_pb->mutable_value()->set_string_value(*it);
        break;
      case DataType::TIMESTAMP: {
        Timestamp ts;
        RETURN_NOT_OK(TimestampFromString(*it, &ts));
        col_expr_pb->mutable_value()->set_timestamp_value(ts.ToInt64());
        break;
      }
      case DataType::BOOL: FALLTHROUGH_INTENDED;
      case DataType::FLOAT: FALLTHROUGH_INTENDED;
      case DataType::DOUBLE: FALLTHROUGH_INTENDED;
      case DataType::MAP: FALLTHROUGH_INTENDED;
      case DataType::SET: FALLTHROUGH_INTENDED;
      case DataType::LIST:
        LOG(FATAL) << "Invalid datatype for partition column: " << column_type;
      default:
        FATAL_INVALID_ENUM_VALUE(DataType, column_type);
    }
  }

  // Compute the hash function.
  RETURN_NOT_OK(yb_op->GetPartitionKey(partition_key));

  // Find the appropriate table.
  auto iter = tablet_map_.upper_bound(*partition_key);
  if (iter == tablet_map_.begin()) {
    return STATUS_SUBSTITUTE(IllegalState, "Couldn't find partition key $0 in tablet map",
                             *partition_key);
  }
  --iter;
  *tablet_id = iter->second.tablet_id();
  return Status::OK();
}

} // namespace tools
} // namespace yb
