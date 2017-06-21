// Copyright (c) YugaByte, Inc.

#include <map>
#include <boost/algorithm/string.hpp>

#include "yb/common/common.pb.h"
#include "yb/tools/yb-generate_partitions.h"
#include "yb/util/date_time.h"
#include "yb/util/status.h"
#include "yb/util/timestamp.h"

namespace yb {
namespace tools {

using client::YBSchema;
using client::YBTableName;
using google::protobuf::RepeatedPtrField;
using master::TabletLocationsPB;
using std::map;
using std::stoi;
using std::string;
using std::vector;

static constexpr const char* kNullString = "null";
static constexpr const char* kNullStringEscaped = "\\n";

namespace {

CHECKED_STATUS CheckNumber(const string& str) {
  for (int i = 0; i < str.size(); i++) {
    const char c = str.at(i);
    if ((c < '0' || c > '9') && (c != '-' || i != 0)) {
      return STATUS_SUBSTITUTE(InvalidArgument, "$0 isn't a valid number", str);
    }
  }
  return Status::OK();
}

bool IsNull(string str) {
  // Copy string for transformation.
  boost::to_lower(str);
  return str == kNullString || str == kNullStringEscaped;
}

} // Anonymous namespace

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
  const YBSchema& schema = table_->schema();
  vector<string> tokens;
  boost::split(tokens, row, boost::is_any_of(","));
  if (tokens.size() < schema.num_hash_key_columns()) {
    return STATUS_SUBSTITUTE(InvalidArgument, "row '$0' doesn't have enough tokens for primary "
        "key, need atleast $1", row, schema.num_hash_key_columns());
  }

  std::unique_ptr<client::YBqlReadOp> yb_op(table_->NewYQLRead());
  YBPartialRow* partial_row = yb_op->mutable_row();

  // Add empty hash column value to ensure GetPartitionKey knows we have hash column values. We
  // don't need to actually set the column values here.
  YQLReadRequestPB* yql_read = yb_op->mutable_request();
  yql_read->add_hashed_column_values();

  for (int i = 0; i < schema.num_hash_key_columns(); i++) {
    if (IsNull(tokens[i])) {
      return STATUS_SUBSTITUTE(IllegalState, "Primary key cannot be null: $0", tokens[i]);
    }

    switch(schema.Column(i).type()->type_info()->type()) {
      case DataType::INT8:
        RETURN_NOT_OK(CheckNumber(tokens[i]));
        RETURN_NOT_OK(partial_row->SetInt8(i, stoi(tokens[i])));
        break;
      case DataType::INT16:
        RETURN_NOT_OK(CheckNumber(tokens[i]));
        RETURN_NOT_OK(partial_row->SetInt16(i, stoi(tokens[i])));
        break;
      case DataType::INT32:
        RETURN_NOT_OK(CheckNumber(tokens[i]));
        RETURN_NOT_OK(partial_row->SetInt32(i, stoi(tokens[i])));
        break;
      case DataType::INT64:
        RETURN_NOT_OK(CheckNumber(tokens[i]));
        RETURN_NOT_OK(partial_row->SetInt64(i, stol(tokens[i])));
        break;
      case DataType::TIMESTAMP: {
        Timestamp ts;
        if (CheckNumber(tokens[i]).ok()) {
          ts = DateTime::TimestampFromInt(stol(tokens[i]));
        } else {
          RETURN_NOT_OK(DateTime::TimestampFromString(tokens[i], &ts));
        }
        RETURN_NOT_OK(partial_row->SetTimestamp(i, ts.ToInt64()));
        break;
      }
      case DataType::BOOL: FALLTHROUGH_INTENDED;
      case DataType::FLOAT: FALLTHROUGH_INTENDED;
      case DataType::DOUBLE: FALLTHROUGH_INTENDED;
      case DataType::MAP: FALLTHROUGH_INTENDED;
      case DataType::SET: FALLTHROUGH_INTENDED;
      case DataType::LIST:
        LOG(FATAL) << "Invalid datatype for partition column";
      default:
        LOG(FATAL) << "DataType not yet supported";
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
