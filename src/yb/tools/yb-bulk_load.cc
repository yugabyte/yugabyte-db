// Copyright (c) YugaByte, Inc.

#include <sched.h>
#include <iostream>
#include <thread>
#include <boost/algorithm/string.hpp>

#include <gflags/gflags.h>
#include <glog/logging.h>

#include "rocksdb/db.h"
#include "yb/client/client.h"
#include "yb/common/hybrid_time.h"
#include "yb/common/schema.h"
#include "yb/common/yql_protocol.pb.h"
#include "yb/docdb/docdb.h"
#include "yb/docdb/docdb_test_util.h"
#include "yb/docdb/doc_operation.h"
#include "yb/tools/bulk_load_docdb_util.h"
#include "yb/tools/bulk_load_utils.h"
#include "yb/tools/yb-generate_partitions.h"
#include "yb/util/status.h"
#include "yb/util/stopwatch.h"
#include "yb/util/size_literals.h"
#include "yb/util/flags.h"
#include "yb/util/logging.h"

using std::stoi;
using std::stol;
using std::stold;
using std::string;
using std::shared_ptr;
using std::unique_ptr;
using std::vector;
using yb::client::YBClient;
using yb::client::YBClientBuilder;
using yb::client::YBTable;
using yb::client::YBTableName;
using yb::operator"" _GB;

DEFINE_string(master_addresses, "", "Comma-separated list of YB Master server addresses");
DEFINE_string(table_name, "", "Name of the table to generate partitions for");
DEFINE_string(namespace_name, "", "Namespace of the table");
DEFINE_string(base_dir, "", "Base directory where we will store all the SSTable files");
DEFINE_int64(memtable_size_bytes, 1_GB, "Amount of bytes to use for the rocksdb memtable");
DEFINE_int32(row_batch_size, 1000, "The number of rows to batch together in each rocksdb write");

namespace yb {
namespace tools {

static CHECKED_STATUS InitYBBulkLoad(shared_ptr<YBClient>* client,
                                    shared_ptr<YBTable>* table,
                                    unique_ptr<YBPartitionGenerator>* partition_generator) {
  // Convert table_name to lowercase since we store table names in lowercase.
  string table_name_lower = boost::to_lower_copy(FLAGS_table_name);
  YBTableName table_name(FLAGS_namespace_name, table_name_lower);

  YBClientBuilder builder;
  builder.add_master_server_addr(FLAGS_master_addresses);

  RETURN_NOT_OK(builder.Build(client));
  RETURN_NOT_OK((*client)->OpenTable(table_name, table));
  partition_generator->reset(new YBPartitionGenerator(table_name, { FLAGS_master_addresses }));
  RETURN_NOT_OK((*partition_generator)->Init());
  return Status::OK();
}

static CHECKED_STATUS InitDBUtil(const string& tablet_id,
                                    unique_ptr<BulkLoadDocDBUtil>* db_fixture) {
  db_fixture->reset(new BulkLoadDocDBUtil(tablet_id, FLAGS_base_dir, FLAGS_memtable_size_bytes));
  RETURN_NOT_OK((*db_fixture)->InitRocksDBOptions());
  RETURN_NOT_OK((*db_fixture)->DisableCompactions()); // This opens rocksdb.
  return Status::OK();
}

static CHECKED_STATUS PopulateColumnValue(const string& column,
                                          const DataType data_type,
                                          YQLColumnValuePB* column_value) {
  auto yql_valuepb = column_value->mutable_expr()->mutable_value();
  int32_t int_val;
  int64_t long_val;
  double double_val;
  switch(data_type) {
    case DataType::INT8:
      RETURN_NOT_OK(CheckedStoi(column, &int_val));
      yql_valuepb->set_int8_value(int_val);
      break;
    case DataType::INT16:
      RETURN_NOT_OK(CheckedStoi(column, &int_val));
      yql_valuepb->set_int16_value(int_val);
      break;
    case DataType::INT32:
      RETURN_NOT_OK(CheckedStoi(column, &int_val));
      yql_valuepb->set_int32_value(stoi(column));
      break;
    case DataType::INT64:
      RETURN_NOT_OK(CheckedStol(column, &long_val));
      yql_valuepb->set_int64_value(stol(column));
      break;
    case DataType::FLOAT:
      RETURN_NOT_OK(CheckedStold(column, &double_val));
      yql_valuepb->set_float_value(double_val);
      break;
    case DataType::DOUBLE:
      RETURN_NOT_OK(CheckedStold(column, &double_val));
      yql_valuepb->set_double_value(double_val);
      break;
    case DataType::STRING:
      yql_valuepb->set_string_value(column);
      break;
    case DataType::TIMESTAMP: {
      Timestamp ts;
      RETURN_NOT_OK(TimestampFromString(column, &ts));
      yql_valuepb->set_timestamp_value(ts.ToInt64());
      break;
    }
    default:
      FATAL_INVALID_ENUM_VALUE(DataType, data_type);
  }
  return Status::OK();
}

static CHECKED_STATUS InsertRow(const string& row,
                                const Schema& schema,
                                BulkLoadDocDBUtil* const db_fixture,
                                docdb::DocWriteBatch* const doc_write_batch,
                                YBPartitionGenerator* const partition_generator) {
  // Get individual columns.
  CsvTokenizer tokenizer = Tokenize(row);
  size_t ncolumns = std::distance(tokenizer.begin(), tokenizer.end());
  if (ncolumns != schema.num_columns()) {
    return STATUS_SUBSTITUTE(IllegalState, "row '$0' has $1 columns, need exactly $2", row,
                             ncolumns, schema.num_columns());
  }

  YQLResponsePB resp;
  YQLWriteRequestPB req;
  req.set_type(YQLWriteRequestPB_YQLStmtType_YQL_STMT_INSERT);
  req.set_client(YQL_CLIENT_CQL);

  auto it = tokenizer.begin();
  // Process the hash keys first.
  for (int i = 0; i < schema.num_key_columns(); i++, it++) {
    if (IsNull(*it)) {
      return STATUS_SUBSTITUTE(IllegalState, "Primary key cannot be null: $0", *it);
    }

    YQLColumnValuePB* column_value = nullptr;
    if (schema.is_hash_key_column(i)) {
      column_value = req.add_hashed_column_values();
    } else {
      column_value = req.add_range_column_values();
    }

    column_value->set_column_id(kFirstColumnId + i);
    RETURN_NOT_OK(PopulateColumnValue(*it, schema.column(i).type_info()->type(),
                                      column_value));
  }

  // Finally process the regular columns.
  for (int i = schema.num_key_columns(); i < schema.num_columns(); i++, it++) {
    YQLColumnValuePB* column_value = req.add_column_values();
    column_value->set_column_id(kFirstColumnId + i);
    if (IsNull(*it)) {
      // Use empty value for null.
      column_value->mutable_expr()->mutable_value();
    } else {
      RETURN_NOT_OK(PopulateColumnValue(*it, schema.column(i).type_info()->type(),
                                        column_value));
    }
  }

  // Add the hash code to the operation.
  string tablet_id;
  string partition_key;
  RETURN_NOT_OK(partition_generator->LookupTabletIdWithTokenizer(tokenizer, &tablet_id,
                                                                 &partition_key));
  req.set_hash_code(PartitionSchema::DecodeMultiColumnHashValue(partition_key));

  // Finally apply the operation to the the doc_write_batch.
  docdb::YQLWriteOperation op(req, schema, &resp);
  RETURN_NOT_OK(op.Apply(doc_write_batch, db_fixture->rocksdb(), HybridTime::kInitialHybridTime));
  return Status::OK();
}

static CHECKED_STATUS RunBulkLoad() {
  shared_ptr<YBClient> client;
  shared_ptr<YBTable> table;
  unique_ptr<YBPartitionGenerator> partition_generator;
  RETURN_NOT_OK(InitYBBulkLoad(&client, &table, &partition_generator));

  string current_tablet_id;

  unique_ptr<BulkLoadDocDBUtil> db_fixture = nullptr;
  unique_ptr<docdb::DocWriteBatch> doc_write_batch;

  for (string line; std::getline(std::cin, line);) {
    // Trim the line.
    boost::algorithm::trim(line);

    // Get the key and value.
    std::size_t index = line.find("\t");
    if (index == std::string::npos) {
      return STATUS_SUBSTITUTE(IllegalState, "Invalid line: $0", line);
    }
    const string tablet_id = line.substr(0, index);
    const string row = line.substr(index + 1, line.size() - (index + 1));

    // Reinitialize rocksdb if needed.
    if (current_tablet_id.empty() || current_tablet_id != tablet_id) {
      if (db_fixture) {
        // Flush all of the data before opening a new rocksdb.
        RETURN_NOT_OK(db_fixture->WriteToRocksDBAndClear(
            doc_write_batch.get(), HybridTime::kInitialHybridTime, /* decode_dockey */ false));
        RETURN_NOT_OK(db_fixture->FlushRocksDB());
      }
      RETURN_NOT_OK(InitDBUtil(tablet_id, &db_fixture));
      doc_write_batch.reset(new docdb::DocWriteBatch(db_fixture->rocksdb()));
    }
    current_tablet_id = tablet_id;

    // Populate the row.
    RETURN_NOT_OK(InsertRow(row, table->InternalSchema(), db_fixture.get(), doc_write_batch.get(),
                            partition_generator.get()));

    // Flush the batch if necessary.
    if (doc_write_batch->size() >= FLAGS_row_batch_size) {
      RETURN_NOT_OK(db_fixture->WriteToRocksDBAndClear(
          doc_write_batch.get(), HybridTime::kInitialHybridTime, /* decode_dockey */ false));
    }
  }

  if (db_fixture) {
    // Flush the last tablet.
    RETURN_NOT_OK(db_fixture->WriteToRocksDBAndClear(
        doc_write_batch.get(), HybridTime::kInitialHybridTime, /* decode_dockey */ false));
    return db_fixture->FlushRocksDB();
  }
  return Status::OK();
}

} // namespace tools
} // namespace yb

int main(int argc, char** argv) {
  yb::ParseCommandLineFlags(&argc, &argv, true);
  yb::InitGoogleLoggingSafe(argv[0]);
  if (FLAGS_master_addresses.empty() || FLAGS_table_name.empty() || FLAGS_namespace_name.empty()
      || FLAGS_base_dir.empty()) {
    LOG (ERROR) << "Need to specify --master_addresses, --table_name, --namespace_name and "
        "--base_dir";
    return 1;
  }

  // Verify the bulk load path exists.
  if (!yb::Env::Default()->FileExists(FLAGS_base_dir)) {
    LOG (FATAL) << "Bulk load directory doesn't exist: " << FLAGS_base_dir;
  }

  yb::Status s = yb::tools::RunBulkLoad();
  if (!s.ok()) {
    LOG (FATAL) << "Error running bulk load: " << s.ToString();
  }
  return 0;
}
