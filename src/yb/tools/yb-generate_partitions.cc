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

#include "yb/tools/yb-generate_partitions.h"

#include <map>

#include "yb/client/client.h"
#include "yb/client/table.h"
#include "yb/client/yb_op.h"

#include "yb/common/common.pb.h"
#include "yb/common/ql_protocol.pb.h"
#include "yb/common/ql_value.h"
#include "yb/common/schema.h"

#include "yb/master/master_client.pb.h"

#include "yb/util/enums.h"
#include "yb/util/status.h"
#include "yb/util/status_format.h"
#include "yb/util/stol_utils.h"
#include "yb/util/timestamp.h"

namespace yb {
namespace tools {

using client::YBTableName;
using google::protobuf::RepeatedPtrField;
using master::TabletLocationsPB;
using std::string;
using std::vector;

YBPartitionGenerator::YBPartitionGenerator(const YBTableName& table_name,
                                           const vector<string>& master_addresses) :
    table_name_(table_name),
    master_addresses_(master_addresses) {
}

YBPartitionGenerator::~YBPartitionGenerator() {
}

Status YBPartitionGenerator::Init() {
  client::YBClientBuilder builder;
  for (const string& master_address : master_addresses_) {
    builder.add_master_server_addr(master_address);
  }
  client_ = VERIFY_RESULT(builder.Build());
  RETURN_NOT_OK(client_->OpenTable(table_name_, &table_));
  RepeatedPtrField<TabletLocationsPB> tablets;
  RETURN_NOT_OK(client_->GetTablets(
      table_name_, /* max_tablets */ 0, &tablets, /* partition_list_version =*/ nullptr));
  RETURN_NOT_OK(BuildTabletMap(tablets));
  return Status::OK();
}

Status YBPartitionGenerator::BuildTabletMap(const RepeatedPtrField<TabletLocationsPB>& tablets) {
  for (const TabletLocationsPB& tablet : tablets) {
    tablet_map_.emplace(tablet.partition().partition_key_start(), tablet);
  }
  return Status::OK();
}

Status YBPartitionGenerator::LookupTabletId(const string& row,
                                            string* tablet_id,
                                            string* partition_key) {
  return LookupTabletId(row, {}, tablet_id, partition_key);
}

Status YBPartitionGenerator::LookupTabletId(const string& row,
                                            const std::set<int>& skipped_cols,
                                            string* tablet_id,
                                            string* partition_key) {
  CsvTokenizer tokenizer = Tokenize(row);
  return LookupTabletIdWithTokenizer(tokenizer, skipped_cols, tablet_id, partition_key);
}

Status YBPartitionGenerator::LookupTabletIdWithTokenizer(const CsvTokenizer& tokenizer,
                                                         const std::set<int>& skipped_cols,
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
  int col_id = 0;
  for (size_t i = 0; i < schema.num_hash_key_columns(); col_id++, it++) {
    if (skipped_cols.find(col_id) != skipped_cols.end()) {
      continue;
    }
    if (IsNull(*it)) {
      return STATUS_SUBSTITUTE(IllegalState, "Primary key cannot be null: $0", *it);
    }

    DataType column_type = schema.column(i).type_info()->type;
    auto* value_pb = ql_read->add_hashed_column_values()->mutable_value();

    switch(column_type) {
      YB_SET_INT_VALUE(value_pb, *it, 8);
      YB_SET_INT_VALUE(value_pb, *it, 16);
      YB_SET_INT_VALUE(value_pb, *it, 32);
      YB_SET_INT_VALUE(value_pb, *it, 64);
      case DataType::STRING:
        value_pb->set_string_value(*it);
        break;
      case DataType::TIMESTAMP: {
        auto ts = TimestampFromString(*it);
        RETURN_NOT_OK(ts);
        value_pb->set_timestamp_value(ts->ToInt64());
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
    i++;  // Avoid incrementing if we are skipping the column.
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
