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

#include "kudu/client/client.h"

#include <algorithm>
#include <boost/bind.hpp>
#include <set>
#include <unordered_map>
#include <vector>

#include "kudu/client/batcher.h"
#include "kudu/client/callbacks.h"
#include "kudu/client/client-internal.h"
#include "kudu/client/client_builder-internal.h"
#include "kudu/client/error-internal.h"
#include "kudu/client/error_collector.h"
#include "kudu/client/meta_cache.h"
#include "kudu/client/row_result.h"
#include "kudu/client/scan_predicate-internal.h"
#include "kudu/client/scanner-internal.h"
#include "kudu/client/schema-internal.h"
#include "kudu/client/session-internal.h"
#include "kudu/client/table-internal.h"
#include "kudu/client/table_alterer-internal.h"
#include "kudu/client/table_creator-internal.h"
#include "kudu/client/tablet_server-internal.h"
#include "kudu/client/write_op.h"
#include "kudu/common/common.pb.h"
#include "kudu/common/partition.h"
#include "kudu/common/row_operations.h"
#include "kudu/common/wire_protocol.h"
#include "kudu/gutil/map-util.h"
#include "kudu/gutil/strings/substitute.h"
#include "kudu/master/master.h" // TODO: remove this include - just needed for default port
#include "kudu/master/master.pb.h"
#include "kudu/master/master.proxy.h"
#include "kudu/rpc/messenger.h"
#include "kudu/util/init.h"
#include "kudu/util/logging.h"
#include "kudu/util/net/dns_resolver.h"

using kudu::master::AlterTableRequestPB;
using kudu::master::AlterTableRequestPB_Step;
using kudu::master::AlterTableResponsePB;
using kudu::master::CreateTableRequestPB;
using kudu::master::CreateTableResponsePB;
using kudu::master::DeleteTableRequestPB;
using kudu::master::DeleteTableResponsePB;
using kudu::master::GetTableSchemaRequestPB;
using kudu::master::GetTableSchemaResponsePB;
using kudu::master::ListTablesRequestPB;
using kudu::master::ListTablesResponsePB;
using kudu::master::ListTabletServersRequestPB;
using kudu::master::ListTabletServersResponsePB;
using kudu::master::ListTabletServersResponsePB_Entry;
using kudu::master::MasterServiceProxy;
using kudu::master::TabletLocationsPB;
using kudu::rpc::Messenger;
using kudu::rpc::MessengerBuilder;
using kudu::rpc::RpcController;
using kudu::tserver::ScanResponsePB;
using std::set;
using std::string;
using std::vector;

MAKE_ENUM_LIMITS(kudu::client::KuduSession::FlushMode,
                 kudu::client::KuduSession::AUTO_FLUSH_SYNC,
                 kudu::client::KuduSession::MANUAL_FLUSH);

MAKE_ENUM_LIMITS(kudu::client::KuduSession::ExternalConsistencyMode,
                 kudu::client::KuduSession::CLIENT_PROPAGATED,
                 kudu::client::KuduSession::COMMIT_WAIT);

MAKE_ENUM_LIMITS(kudu::client::KuduScanner::ReadMode,
                 kudu::client::KuduScanner::READ_LATEST,
                 kudu::client::KuduScanner::READ_AT_SNAPSHOT);

MAKE_ENUM_LIMITS(kudu::client::KuduScanner::OrderMode,
                 kudu::client::KuduScanner::UNORDERED,
                 kudu::client::KuduScanner::ORDERED);

namespace kudu {
namespace client {

using internal::Batcher;
using internal::ErrorCollector;
using internal::MetaCache;
using sp::shared_ptr;

static const int kHtTimestampBitsToShift = 12;
static const char* kProgName = "kudu_client";

// We need to reroute all logging to stderr when the client library is
// loaded. GoogleOnceInit() can do that, but there are multiple entry
// points into the client code, and it'd need to be called in each one.
// So instead, let's use a constructor function.
//
// Should this be restricted to just the exported client build? Probably
// not, as any application using the library probably wants stderr logging
// more than file logging.
__attribute__((constructor))
static void InitializeBasicLogging() {
  InitGoogleLoggingSafeBasic(kProgName);
}

// Adapts between the internal LogSeverity and the client's KuduLogSeverity.
static void LoggingAdapterCB(KuduLoggingCallback* user_cb,
                             LogSeverity severity,
                             const char* filename,
                             int line_number,
                             const struct ::tm* time,
                             const char* message,
                             size_t message_len) {
  KuduLogSeverity client_severity;
  switch (severity) {
    case kudu::SEVERITY_INFO:
      client_severity = SEVERITY_INFO;
      break;
    case kudu::SEVERITY_WARNING:
      client_severity = SEVERITY_WARNING;
      break;
    case kudu::SEVERITY_ERROR:
      client_severity = SEVERITY_ERROR;
      break;
    case kudu::SEVERITY_FATAL:
      client_severity = SEVERITY_FATAL;
      break;
    default:
      LOG(FATAL) << "Unknown Kudu log severity: " << severity;
  }
  user_cb->Run(client_severity, filename, line_number, time,
               message, message_len);
}

void InstallLoggingCallback(KuduLoggingCallback* cb) {
  RegisterLoggingCallback(Bind(&LoggingAdapterCB, Unretained(cb)));
}

void UninstallLoggingCallback() {
  UnregisterLoggingCallback();
}

void SetVerboseLogLevel(int level) {
  FLAGS_v = level;
}

Status SetInternalSignalNumber(int signum) {
  return SetStackTraceSignal(signum);
}

KuduClientBuilder::KuduClientBuilder()
  : data_(new KuduClientBuilder::Data()) {
}

KuduClientBuilder::~KuduClientBuilder() {
  delete data_;
}

KuduClientBuilder& KuduClientBuilder::clear_master_server_addrs() {
  data_->master_server_addrs_.clear();
  return *this;
}

KuduClientBuilder& KuduClientBuilder::master_server_addrs(const vector<string>& addrs) {
  for (const string& addr : addrs) {
    data_->master_server_addrs_.push_back(addr);
  }
  return *this;
}

KuduClientBuilder& KuduClientBuilder::add_master_server_addr(const string& addr) {
  data_->master_server_addrs_.push_back(addr);
  return *this;
}

KuduClientBuilder& KuduClientBuilder::default_admin_operation_timeout(const MonoDelta& timeout) {
  data_->default_admin_operation_timeout_ = timeout;
  return *this;
}

KuduClientBuilder& KuduClientBuilder::default_rpc_timeout(const MonoDelta& timeout) {
  data_->default_rpc_timeout_ = timeout;
  return *this;
}

Status KuduClientBuilder::Build(shared_ptr<KuduClient>* client) {
  RETURN_NOT_OK(CheckCPUFlags());

  shared_ptr<KuduClient> c(new KuduClient());

  // Init messenger.
  MessengerBuilder builder("client");
  RETURN_NOT_OK(builder.Build(&c->data_->messenger_));

  c->data_->master_server_addrs_ = data_->master_server_addrs_;
  c->data_->default_admin_operation_timeout_ = data_->default_admin_operation_timeout_;
  c->data_->default_rpc_timeout_ = data_->default_rpc_timeout_;

  // Let's allow for plenty of time for discovering the master the first
  // time around.
  MonoTime deadline = MonoTime::Now(MonoTime::FINE);
  deadline.AddDelta(c->default_admin_operation_timeout());
  RETURN_NOT_OK_PREPEND(c->data_->SetMasterServerProxy(c.get(), deadline),
                        "Could not locate the leader master");

  c->data_->meta_cache_.reset(new MetaCache(c.get()));
  c->data_->dns_resolver_.reset(new DnsResolver());

  // Init local host names used for locality decisions.
  RETURN_NOT_OK_PREPEND(c->data_->InitLocalHostNames(),
                        "Could not determine local host names");

  client->swap(c);
  return Status::OK();
}

KuduClient::KuduClient()
  : data_(new KuduClient::Data()) {
}

KuduClient::~KuduClient() {
  delete data_;
}

KuduTableCreator* KuduClient::NewTableCreator() {
  return new KuduTableCreator(this);
}

Status KuduClient::IsCreateTableInProgress(const string& table_name,
                                           bool *create_in_progress) {
  MonoTime deadline = MonoTime::Now(MonoTime::FINE);
  deadline.AddDelta(default_admin_operation_timeout());
  return data_->IsCreateTableInProgress(this, table_name, deadline, create_in_progress);
}

Status KuduClient::DeleteTable(const string& table_name) {
  MonoTime deadline = MonoTime::Now(MonoTime::FINE);
  deadline.AddDelta(default_admin_operation_timeout());
  return data_->DeleteTable(this, table_name, deadline);
}

KuduTableAlterer* KuduClient::NewTableAlterer(const string& name) {
  return new KuduTableAlterer(this, name);
}

Status KuduClient::IsAlterTableInProgress(const string& table_name,
                                          bool *alter_in_progress) {
  MonoTime deadline = MonoTime::Now(MonoTime::FINE);
  deadline.AddDelta(default_admin_operation_timeout());
  return data_->IsAlterTableInProgress(this, table_name, deadline, alter_in_progress);
}

Status KuduClient::GetTableSchema(const string& table_name,
                                  KuduSchema* schema) {
  MonoTime deadline = MonoTime::Now(MonoTime::FINE);
  deadline.AddDelta(default_admin_operation_timeout());
  string table_id_ignored;
  PartitionSchema partition_schema;
  return data_->GetTableSchema(this,
                               table_name,
                               deadline,
                               schema,
                               &partition_schema,
                               &table_id_ignored);
}

Status KuduClient::ListTabletServers(vector<KuduTabletServer*>* tablet_servers) {
  ListTabletServersRequestPB req;
  ListTabletServersResponsePB resp;

  MonoTime deadline = MonoTime::Now(MonoTime::FINE);
  deadline.AddDelta(default_admin_operation_timeout());
  Status s =
      data_->SyncLeaderMasterRpc<ListTabletServersRequestPB, ListTabletServersResponsePB>(
          deadline,
          this,
          req,
          &resp,
          nullptr,
          "ListTabletServers",
          &MasterServiceProxy::ListTabletServers);
  RETURN_NOT_OK(s);
  if (resp.has_error()) {
    return StatusFromPB(resp.error().status());
  }
  for (int i = 0; i < resp.servers_size(); i++) {
    const ListTabletServersResponsePB_Entry& e = resp.servers(i);
    auto ts = new KuduTabletServer();
    ts->data_ = new KuduTabletServer::Data(e.instance_id().permanent_uuid(),
                                           e.registration().rpc_addresses(0).host());
    tablet_servers->push_back(ts);
  }
  return Status::OK();
}

Status KuduClient::ListTables(vector<string>* tables,
                              const string& filter) {
  ListTablesRequestPB req;
  ListTablesResponsePB resp;

  if (!filter.empty()) {
    req.set_name_filter(filter);
  }
  MonoTime deadline = MonoTime::Now(MonoTime::FINE);
  deadline.AddDelta(default_admin_operation_timeout());
  Status s =
      data_->SyncLeaderMasterRpc<ListTablesRequestPB, ListTablesResponsePB>(
          deadline,
          this,
          req,
          &resp,
          nullptr,
          "ListTables",
          &MasterServiceProxy::ListTables);
  RETURN_NOT_OK(s);
  if (resp.has_error()) {
    return StatusFromPB(resp.error().status());
  }
  for (int i = 0; i < resp.tables_size(); i++) {
    tables->push_back(resp.tables(i).name());
  }
  return Status::OK();
}

Status KuduClient::TableExists(const string& table_name, bool* exists) {
  std::vector<std::string> tables;
  RETURN_NOT_OK(ListTables(&tables, table_name));
  for (const string& table : tables) {
    if (table == table_name) {
      *exists = true;
      return Status::OK();
    }
  }
  *exists = false;
  return Status::OK();
}

Status KuduClient::OpenTable(const string& table_name,
                             shared_ptr<KuduTable>* table) {
  KuduSchema schema;
  string table_id;
  PartitionSchema partition_schema;
  MonoTime deadline = MonoTime::Now(MonoTime::FINE);
  deadline.AddDelta(default_admin_operation_timeout());
  RETURN_NOT_OK(data_->GetTableSchema(this,
                                      table_name,
                                      deadline,
                                      &schema,
                                      &partition_schema,
                                      &table_id));

  // In the future, probably will look up the table in some map to reuse KuduTable
  // instances.
  shared_ptr<KuduTable> ret(new KuduTable(shared_from_this(), table_name, table_id,
                                          schema, partition_schema));
  RETURN_NOT_OK(ret->data_->Open());
  table->swap(ret);

  return Status::OK();
}

shared_ptr<KuduSession> KuduClient::NewSession() {
  shared_ptr<KuduSession> ret(new KuduSession(shared_from_this()));
  ret->data_->Init(ret);
  return ret;
}

bool KuduClient::IsMultiMaster() const {
  return data_->master_server_addrs_.size() > 1;
}

const MonoDelta& KuduClient::default_admin_operation_timeout() const {
  return data_->default_admin_operation_timeout_;
}

const MonoDelta& KuduClient::default_rpc_timeout() const {
  return data_->default_rpc_timeout_;
}

const uint64_t KuduClient::kNoTimestamp = 0;

uint64_t KuduClient::GetLatestObservedTimestamp() const {
  return data_->GetLatestObservedTimestamp();
}

void KuduClient::SetLatestObservedTimestamp(uint64_t ht_timestamp) {
  data_->UpdateLatestObservedTimestamp(ht_timestamp);
}

////////////////////////////////////////////////////////////
// KuduTableCreator
////////////////////////////////////////////////////////////

KuduTableCreator::KuduTableCreator(KuduClient* client)
  : data_(new KuduTableCreator::Data(client)) {
}

KuduTableCreator::~KuduTableCreator() {
  delete data_;
}

KuduTableCreator& KuduTableCreator::table_name(const string& name) {
  data_->table_name_ = name;
  return *this;
}

KuduTableCreator& KuduTableCreator::schema(const KuduSchema* schema) {
  data_->schema_ = schema;
  return *this;
}

KuduTableCreator& KuduTableCreator::add_hash_partitions(const std::vector<std::string>& columns,
                                                        int32_t num_buckets) {
  return add_hash_partitions(columns, num_buckets, 0);
}

KuduTableCreator& KuduTableCreator::add_hash_partitions(const std::vector<std::string>& columns,
                                                        int32_t num_buckets, int32_t seed) {
  PartitionSchemaPB::HashBucketSchemaPB* bucket_schema =
    data_->partition_schema_.add_hash_bucket_schemas();
  for (const string& col_name : columns) {
    bucket_schema->add_columns()->set_name(col_name);
  }
  bucket_schema->set_num_buckets(num_buckets);
  bucket_schema->set_seed(seed);
  return *this;
}

KuduTableCreator& KuduTableCreator::set_range_partition_columns(
    const std::vector<std::string>& columns) {
  PartitionSchemaPB::RangeSchemaPB* range_schema =
    data_->partition_schema_.mutable_range_schema();
  range_schema->Clear();
  for (const string& col_name : columns) {
    range_schema->add_columns()->set_name(col_name);
  }

  return *this;
}

KuduTableCreator& KuduTableCreator::split_rows(const vector<const KuduPartialRow*>& rows) {
  data_->split_rows_ = rows;
  return *this;
}

KuduTableCreator& KuduTableCreator::num_replicas(int num_replicas) {
  data_->num_replicas_ = num_replicas;
  return *this;
}

KuduTableCreator& KuduTableCreator::timeout(const MonoDelta& timeout) {
  data_->timeout_ = timeout;
  return *this;
}

KuduTableCreator& KuduTableCreator::wait(bool wait) {
  data_->wait_ = wait;
  return *this;
}

Status KuduTableCreator::Create() {
  if (!data_->table_name_.length()) {
    return Status::InvalidArgument("Missing table name");
  }
  if (!data_->schema_) {
    return Status::InvalidArgument("Missing schema");
  }

  // Build request.
  CreateTableRequestPB req;
  req.set_name(data_->table_name_);
  if (data_->num_replicas_ >= 1) {
    req.set_num_replicas(data_->num_replicas_);
  }
  RETURN_NOT_OK_PREPEND(SchemaToPB(*data_->schema_->schema_, req.mutable_schema()),
                        "Invalid schema");

  RowOperationsPBEncoder encoder(req.mutable_split_rows());

  for (const KuduPartialRow* row : data_->split_rows_) {
    encoder.Add(RowOperationsPB::SPLIT_ROW, *row);
  }
  req.mutable_partition_schema()->CopyFrom(data_->partition_schema_);

  MonoTime deadline = MonoTime::Now(MonoTime::FINE);
  if (data_->timeout_.Initialized()) {
    deadline.AddDelta(data_->timeout_);
  } else {
    deadline.AddDelta(data_->client_->default_admin_operation_timeout());
  }

  RETURN_NOT_OK_PREPEND(data_->client_->data_->CreateTable(data_->client_,
                                                           req,
                                                           *data_->schema_,
                                                           deadline),
                        strings::Substitute("Error creating table $0 on the master",
                                            data_->table_name_));

  // Spin until the table is fully created, if requested.
  if (data_->wait_) {
    RETURN_NOT_OK(data_->client_->data_->WaitForCreateTableToFinish(data_->client_,
                                                                    data_->table_name_,
                                                                    deadline));
  }

  return Status::OK();
}

////////////////////////////////////////////////////////////
// KuduTable
////////////////////////////////////////////////////////////

KuduTable::KuduTable(const shared_ptr<KuduClient>& client,
                     const string& name,
                     const string& table_id,
                     const KuduSchema& schema,
                     const PartitionSchema& partition_schema)
  : data_(new KuduTable::Data(client, name, table_id, schema, partition_schema)) {
}

KuduTable::~KuduTable() {
  delete data_;
}

const string& KuduTable::name() const {
  return data_->name_;
}

const string& KuduTable::id() const {
  return data_->id_;
}

const KuduSchema& KuduTable::schema() const {
  return data_->schema_;
}

KuduInsert* KuduTable::NewInsert() {
  return new KuduInsert(shared_from_this());
}

KuduUpdate* KuduTable::NewUpdate() {
  return new KuduUpdate(shared_from_this());
}

KuduDelete* KuduTable::NewDelete() {
  return new KuduDelete(shared_from_this());
}

KuduClient* KuduTable::client() const {
  return data_->client_.get();
}

const PartitionSchema& KuduTable::partition_schema() const {
  return data_->partition_schema_;
}

KuduPredicate* KuduTable::NewComparisonPredicate(const Slice& col_name,
                                                 KuduPredicate::ComparisonOp op,
                                                 KuduValue* value) {
  StringPiece name_sp(reinterpret_cast<const char*>(col_name.data()), col_name.size());
  const Schema* s = data_->schema_.schema_;
  int col_idx = s->find_column(name_sp);
  if (col_idx == Schema::kColumnNotFound) {
    // Since this function doesn't return an error, instead we create a special
    // predicate that just returns the errors when we add it to the scanner.
    //
    // This makes the API more "fluent".
    delete value; // we always take ownership of 'value'.
    return new KuduPredicate(new ErrorPredicateData(
                                 Status::NotFound("column not found", col_name)));
  }

  return new KuduPredicate(new ComparisonPredicateData(s->column(col_idx), op, value));
}

////////////////////////////////////////////////////////////
// Error
////////////////////////////////////////////////////////////

const Status& KuduError::status() const {
  return data_->status_;
}

const KuduWriteOperation& KuduError::failed_op() const {
  return *data_->failed_op_;
}

KuduWriteOperation* KuduError::release_failed_op() {
  CHECK_NOTNULL(data_->failed_op_.get());
  return data_->failed_op_.release();
}

bool KuduError::was_possibly_successful() const {
  // TODO: implement me - right now be conservative.
  return true;
}

KuduError::KuduError(KuduWriteOperation* failed_op,
                     const Status& status)
  : data_(new KuduError::Data(gscoped_ptr<KuduWriteOperation>(failed_op),
                              status)) {
}

KuduError::~KuduError() {
  delete data_;
}

////////////////////////////////////////////////////////////
// KuduSession
////////////////////////////////////////////////////////////

KuduSession::KuduSession(const shared_ptr<KuduClient>& client)
  : data_(new KuduSession::Data(client)) {
}

KuduSession::~KuduSession() {
  WARN_NOT_OK(data_->Close(true), "Closed Session with pending operations.");
  delete data_;
}

Status KuduSession::Close() {
  return data_->Close(false);
}

Status KuduSession::SetFlushMode(FlushMode m) {
  if (m == AUTO_FLUSH_BACKGROUND) {
    return Status::NotSupported("AUTO_FLUSH_BACKGROUND has not been implemented in the"
        " c++ client (see KUDU-456).");
  }
  if (data_->batcher_->HasPendingOperations()) {
    // TODO: there may be a more reasonable behavior here.
    return Status::IllegalState("Cannot change flush mode when writes are buffered");
  }
  if (!tight_enum_test<FlushMode>(m)) {
    // Be paranoid in client code.
    return Status::InvalidArgument("Bad flush mode");
  }

  data_->flush_mode_ = m;
  return Status::OK();
}

Status KuduSession::SetExternalConsistencyMode(ExternalConsistencyMode m) {
  if (data_->batcher_->HasPendingOperations()) {
    // TODO: there may be a more reasonable behavior here.
    return Status::IllegalState("Cannot change external consistency mode when writes are "
        "buffered");
  }
  if (!tight_enum_test<ExternalConsistencyMode>(m)) {
    // Be paranoid in client code.
    return Status::InvalidArgument("Bad external consistency mode");
  }

  data_->external_consistency_mode_ = m;
  return Status::OK();
}

void KuduSession::SetTimeoutMillis(int millis) {
  CHECK_GE(millis, 0);
  data_->timeout_ms_ = millis;
  data_->batcher_->SetTimeoutMillis(millis);
}

Status KuduSession::Flush() {
  Synchronizer s;
  KuduStatusMemberCallback<Synchronizer> ksmcb(&s, &Synchronizer::StatusCB);
  FlushAsync(&ksmcb);
  return s.Wait();
}

void KuduSession::FlushAsync(KuduStatusCallback* user_callback) {
  CHECK_EQ(data_->flush_mode_, MANUAL_FLUSH) << "TODO: handle other flush modes";

  // Swap in a new batcher to start building the next batch.
  // Save off the old batcher.
  scoped_refptr<Batcher> old_batcher;
  {
    lock_guard<simple_spinlock> l(&data_->lock_);
    data_->NewBatcher(shared_from_this(), &old_batcher);
    InsertOrDie(&data_->flushed_batchers_, old_batcher.get());
  }

  // Send off any buffered data. Important to do this outside of the lock
  // since the callback may itself try to take the lock, in the case that
  // the batch fails "inline" on the same thread.
  old_batcher->FlushAsync(user_callback);
}

bool KuduSession::HasPendingOperations() const {
  lock_guard<simple_spinlock> l(&data_->lock_);
  if (data_->batcher_->HasPendingOperations()) {
    return true;
  }
  for (Batcher* b : data_->flushed_batchers_) {
    if (b->HasPendingOperations()) {
      return true;
    }
  }
  return false;
}

Status KuduSession::Apply(KuduWriteOperation* write_op) {
  if (!write_op->row().IsKeySet()) {
    Status status = Status::IllegalState("Key not specified", write_op->ToString());
    data_->error_collector_->AddError(gscoped_ptr<KuduError>(
        new KuduError(write_op, status)));
    return status;
  }

  Status s = data_->batcher_->Add(write_op);
  if (!PREDICT_FALSE(s.ok())) {
    data_->error_collector_->AddError(gscoped_ptr<KuduError>(
        new KuduError(write_op, s)));
    return s;
  }

  if (data_->flush_mode_ == AUTO_FLUSH_SYNC) {
    return Flush();
  }

  return Status::OK();
}

int KuduSession::CountBufferedOperations() const {
  lock_guard<simple_spinlock> l(&data_->lock_);
  CHECK_EQ(data_->flush_mode_, MANUAL_FLUSH);

  return data_->batcher_->CountBufferedOperations();
}

int KuduSession::CountPendingErrors() const {
  return data_->error_collector_->CountErrors();
}

void KuduSession::GetPendingErrors(vector<KuduError*>* errors, bool* overflowed) {
  data_->error_collector_->GetErrors(errors, overflowed);
}

KuduClient* KuduSession::client() const {
  return data_->client_.get();
}

////////////////////////////////////////////////////////////
// KuduTableAlterer
////////////////////////////////////////////////////////////
KuduTableAlterer::KuduTableAlterer(KuduClient* client, const string& name)
  : data_(new Data(client, name)) {
}

KuduTableAlterer::~KuduTableAlterer() {
  delete data_;
}

KuduTableAlterer* KuduTableAlterer::RenameTo(const string& new_name) {
  data_->rename_to_ = new_name;
  return this;
}

KuduColumnSpec* KuduTableAlterer::AddColumn(const string& name) {
  Data::Step s = {AlterTableRequestPB::ADD_COLUMN,
                  new KuduColumnSpec(name)};
  data_->steps_.push_back(s);
  return s.spec;
}

KuduColumnSpec* KuduTableAlterer::AlterColumn(const string& name) {
  Data::Step s = {AlterTableRequestPB::ALTER_COLUMN,
                  new KuduColumnSpec(name)};
  data_->steps_.push_back(s);
  return s.spec;
}

KuduTableAlterer* KuduTableAlterer::DropColumn(const string& name) {
  Data::Step s = {AlterTableRequestPB::DROP_COLUMN,
                  new KuduColumnSpec(name)};
  data_->steps_.push_back(s);
  return this;
}

KuduTableAlterer* KuduTableAlterer::timeout(const MonoDelta& timeout) {
  data_->timeout_ = timeout;
  return this;
}

KuduTableAlterer* KuduTableAlterer::wait(bool wait) {
  data_->wait_ = wait;
  return this;
}

Status KuduTableAlterer::Alter() {
  AlterTableRequestPB req;
  RETURN_NOT_OK(data_->ToRequest(&req));

  MonoDelta timeout = data_->timeout_.Initialized() ?
    data_->timeout_ :
    data_->client_->default_admin_operation_timeout();
  MonoTime deadline = MonoTime::Now(MonoTime::FINE);
  deadline.AddDelta(timeout);
  RETURN_NOT_OK(data_->client_->data_->AlterTable(data_->client_, req, deadline));
  if (data_->wait_) {
    string alter_name = data_->rename_to_.get_value_or(data_->table_name_);
    RETURN_NOT_OK(data_->client_->data_->WaitForAlterTableToFinish(
        data_->client_, alter_name, deadline));
  }

  return Status::OK();
}

////////////////////////////////////////////////////////////
// KuduScanner
////////////////////////////////////////////////////////////

KuduScanner::KuduScanner(KuduTable* table)
  : data_(new KuduScanner::Data(table)) {
}

KuduScanner::~KuduScanner() {
  Close();
  delete data_;
}

Status KuduScanner::SetProjectedColumns(const vector<string>& col_names) {
  return SetProjectedColumnNames(col_names);
}

Status KuduScanner::SetProjectedColumnNames(const vector<string>& col_names) {
  if (data_->open_) {
    return Status::IllegalState("Projection must be set before Open()");
  }

  const Schema* table_schema = data_->table_->schema().schema_;
  vector<int> col_indexes;
  col_indexes.reserve(col_names.size());
  for (const string& col_name : col_names) {
    int idx = table_schema->find_column(col_name);
    if (idx == Schema::kColumnNotFound) {
      return Status::NotFound(strings::Substitute("Column: \"$0\" was not found in the "
          "table schema.", col_name));
    }
    col_indexes.push_back(idx);
  }

  return SetProjectedColumnIndexes(col_indexes);
}

Status KuduScanner::SetProjectedColumnIndexes(const vector<int>& col_indexes) {
  if (data_->open_) {
    return Status::IllegalState("Projection must be set before Open()");
  }

  const Schema* table_schema = data_->table_->schema().schema_;
  vector<ColumnSchema> cols;
  cols.reserve(col_indexes.size());
  for (const int col_index : col_indexes) {
    if (col_index >= table_schema->columns().size()) {
      return Status::NotFound(strings::Substitute("Column: \"$0\" was not found in the "
          "table schema.", col_index));
    }
    cols.push_back(table_schema->column(col_index));
  }

  gscoped_ptr<Schema> s(new Schema());
  RETURN_NOT_OK(s->Reset(cols, 0));
  data_->SetProjectionSchema(data_->pool_.Add(s.release()));
  return Status::OK();
}

Status KuduScanner::SetBatchSizeBytes(uint32_t batch_size) {
  data_->has_batch_size_bytes_ = true;
  data_->batch_size_bytes_ = batch_size;
  return Status::OK();
}

Status KuduScanner::SetReadMode(ReadMode read_mode) {
  if (data_->open_) {
    return Status::IllegalState("Read mode must be set before Open()");
  }
  if (!tight_enum_test<ReadMode>(read_mode)) {
    return Status::InvalidArgument("Bad read mode");
  }
  data_->read_mode_ = read_mode;
  return Status::OK();
}

Status KuduScanner::SetOrderMode(OrderMode order_mode) {
  if (data_->open_) {
    return Status::IllegalState("Order mode must be set before Open()");
  }
  if (!tight_enum_test<OrderMode>(order_mode)) {
    return Status::InvalidArgument("Bad order mode");
  }
  data_->is_fault_tolerant_ = order_mode == ORDERED;
  return Status::OK();
}

Status KuduScanner::SetFaultTolerant() {
  if (data_->open_) {
    return Status::IllegalState("Fault-tolerance must be set before Open()");
  }
  RETURN_NOT_OK(SetReadMode(READ_AT_SNAPSHOT));
  data_->is_fault_tolerant_ = true;
  return Status::OK();
}

Status KuduScanner::SetSnapshotMicros(uint64_t snapshot_timestamp_micros) {
  if (data_->open_) {
    return Status::IllegalState("Snapshot timestamp must be set before Open()");
  }
  // Shift the HT timestamp bits to get well-formed HT timestamp with the logical
  // bits zeroed out.
  data_->snapshot_timestamp_ = snapshot_timestamp_micros << kHtTimestampBitsToShift;
  return Status::OK();
}

Status KuduScanner::SetSnapshotRaw(uint64_t snapshot_timestamp) {
  if (data_->open_) {
    return Status::IllegalState("Snapshot timestamp must be set before Open()");
  }
  data_->snapshot_timestamp_ = snapshot_timestamp;
  return Status::OK();
}

Status KuduScanner::SetSelection(KuduClient::ReplicaSelection selection) {
  if (data_->open_) {
    return Status::IllegalState("Replica selection must be set before Open()");
  }
  data_->selection_ = selection;
  return Status::OK();
}

Status KuduScanner::SetTimeoutMillis(int millis) {
  if (data_->open_) {
    return Status::IllegalState("Timeout must be set before Open()");
  }
  data_->timeout_ = MonoDelta::FromMilliseconds(millis);
  return Status::OK();
}

Status KuduScanner::AddConjunctPredicate(KuduPredicate* pred) {
  // Take ownership even if we return a bad status.
  data_->pool_.Add(pred);
  if (data_->open_) {
    return Status::IllegalState("Predicate must be set before Open()");
  }
  return pred->data_->AddToScanSpec(&data_->spec_);
}

Status KuduScanner::AddLowerBound(const KuduPartialRow& key) {
  gscoped_ptr<string> enc(new string());
  RETURN_NOT_OK(key.EncodeRowKey(enc.get()));
  RETURN_NOT_OK(AddLowerBoundRaw(Slice(*enc)));
  data_->pool_.Add(enc.release());
  return Status::OK();
}

Status KuduScanner::AddLowerBoundRaw(const Slice& key) {
  // Make a copy of the key.
  gscoped_ptr<EncodedKey> enc_key;
  RETURN_NOT_OK(EncodedKey::DecodeEncodedString(
                  *data_->table_->schema().schema_, &data_->arena_, key, &enc_key));
  data_->spec_.SetLowerBoundKey(enc_key.get());
  data_->pool_.Add(enc_key.release());
  return Status::OK();
}

Status KuduScanner::AddExclusiveUpperBound(const KuduPartialRow& key) {
  gscoped_ptr<string> enc(new string());
  RETURN_NOT_OK(key.EncodeRowKey(enc.get()));
  RETURN_NOT_OK(AddExclusiveUpperBoundRaw(Slice(*enc)));
  data_->pool_.Add(enc.release());
  return Status::OK();
}

Status KuduScanner::AddExclusiveUpperBoundRaw(const Slice& key) {
  // Make a copy of the key.
  gscoped_ptr<EncodedKey> enc_key;
  RETURN_NOT_OK(EncodedKey::DecodeEncodedString(
                  *data_->table_->schema().schema_, &data_->arena_, key, &enc_key));
  data_->spec_.SetExclusiveUpperBoundKey(enc_key.get());
  data_->pool_.Add(enc_key.release());
  return Status::OK();
}

Status KuduScanner::AddLowerBoundPartitionKeyRaw(const Slice& partition_key) {
  data_->spec_.SetLowerBoundPartitionKey(partition_key);
  return Status::OK();
}

Status KuduScanner::AddExclusiveUpperBoundPartitionKeyRaw(const Slice& partition_key) {
  data_->spec_.SetExclusiveUpperBoundPartitionKey(partition_key);
  return Status::OK();
}

Status KuduScanner::SetCacheBlocks(bool cache_blocks) {
  if (data_->open_) {
    return Status::IllegalState("Block caching must be set before Open()");
  }
  data_->spec_.set_cache_blocks(cache_blocks);
  return Status::OK();
}

KuduSchema KuduScanner::GetProjectionSchema() const {
  return data_->client_projection_;
}

namespace {
// Callback for the RPC sent by Close().
// We can't use the KuduScanner response and RPC controller members for this
// call, because the scanner object may be destructed while the call is still
// being processed.
struct CloseCallback {
  RpcController controller;
  ScanResponsePB response;
  string scanner_id;
  void Callback() {
    if (!controller.status().ok()) {
      LOG(WARNING) << "Couldn't close scanner " << scanner_id << ": "
                   << controller.status().ToString();
    }
    delete this;
  }
};
} // anonymous namespace

string KuduScanner::ToString() const {
  Slice start_key = data_->spec_.lower_bound_key() ?
    data_->spec_.lower_bound_key()->encoded_key() : Slice("INF");
  Slice end_key = data_->spec_.exclusive_upper_bound_key() ?
    data_->spec_.exclusive_upper_bound_key()->encoded_key() : Slice("INF");
  return strings::Substitute("$0: [$1,$2)", data_->table_->name(),
                             start_key.ToDebugString(), end_key.ToDebugString());
}

Status KuduScanner::Open() {
  CHECK(!data_->open_) << "Scanner already open";
  CHECK(data_->projection_ != nullptr) << "No projection provided";

  // Find the first tablet.
  data_->spec_encoder_.EncodeRangePredicates(&data_->spec_, false);

  VLOG(1) << "Beginning scan " << ToString();

  MonoTime deadline = MonoTime::Now(MonoTime::FINE);
  deadline.AddDelta(data_->timeout_);
  set<string> blacklist;

  bool is_simple_range_partitioned =
    data_->table_->partition_schema().IsSimplePKRangePartitioning(*data_->table_->schema().schema_);

  if (!is_simple_range_partitioned &&
      (data_->spec_.lower_bound_key() != nullptr ||
       data_->spec_.exclusive_upper_bound_key() != nullptr ||
       !data_->spec_.predicates().empty())) {
    KLOG_FIRST_N(WARNING, 1) << "Starting full table scan. In the future this scan may be "
                                "automatically optimized with partition pruning.";
  }

  if (is_simple_range_partitioned) {
    // If the table is simple range partitioned, then the partition key space is
    // isomorphic to the primary key space. We can potentially reduce the scan
    // length by only scanning the intersection of the primary key range and the
    // partition key range. This is a stop-gap until real partition pruning is
    // in place that will work across any partition type.
    Slice start_primary_key = data_->spec_.lower_bound_key() == nullptr ? Slice()
                            : data_->spec_.lower_bound_key()->encoded_key();
    Slice end_primary_key = data_->spec_.exclusive_upper_bound_key() == nullptr ? Slice()
                          : data_->spec_.exclusive_upper_bound_key()->encoded_key();
    Slice start_partition_key = data_->spec_.lower_bound_partition_key();
    Slice end_partition_key = data_->spec_.exclusive_upper_bound_partition_key();

    if ((!end_partition_key.empty() && start_primary_key.compare(end_partition_key) >= 0) ||
        (!end_primary_key.empty() && start_partition_key.compare(end_primary_key) >= 0)) {
      // The primary key range and the partition key range do not intersect;
      // the scan will be empty. Keep the existing partition key range.
    } else {
      // Assign the scan's partition key range to the intersection of the
      // primary key and partition key ranges.
      data_->spec_.SetLowerBoundPartitionKey(start_primary_key);
      data_->spec_.SetExclusiveUpperBoundPartitionKey(end_primary_key);
    }
  }

  RETURN_NOT_OK(data_->OpenTablet(data_->spec_.lower_bound_partition_key(), deadline, &blacklist));

  data_->open_ = true;
  return Status::OK();
}

Status KuduScanner::KeepAlive() {
  return data_->KeepAlive();
}

void KuduScanner::Close() {
  if (!data_->open_) return;
  CHECK(data_->proxy_);

  VLOG(1) << "Ending scan " << ToString();

  // Close the scanner on the server-side, if necessary.
  //
  // If the scan did not match any rows, the tserver will not assign a scanner ID.
  // This is reflected in the Open() response. In this case, there is no server-side state
  // to clean up.
  if (!data_->next_req_.scanner_id().empty()) {
    gscoped_ptr<CloseCallback> closer(new CloseCallback);
    closer->scanner_id = data_->next_req_.scanner_id();
    data_->PrepareRequest(KuduScanner::Data::CLOSE);
    data_->next_req_.set_close_scanner(true);
    closer->controller.set_timeout(data_->timeout_);
    data_->proxy_->ScanAsync(data_->next_req_, &closer->response, &closer->controller,
                             boost::bind(&CloseCallback::Callback, closer.get()));
    ignore_result(closer.release());
  }
  data_->proxy_.reset();
  data_->open_ = false;
  return;
}

bool KuduScanner::HasMoreRows() const {
  CHECK(data_->open_);
  return data_->data_in_open_ || // more data in hand
      data_->last_response_.has_more_results() || // more data in this tablet
      data_->MoreTablets(); // more tablets to scan, possibly with more data
}

Status KuduScanner::NextBatch(vector<KuduRowResult>* rows) {
  RETURN_NOT_OK(NextBatch(&data_->batch_for_old_api_));
  data_->batch_for_old_api_.data_->ExtractRows(rows);
  return Status::OK();
}

Status KuduScanner::NextBatch(KuduScanBatch* result) {
  // TODO: do some double-buffering here -- when we return this batch
  // we should already have fired off the RPC for the next batch, but
  // need to do some swapping of the response objects around to avoid
  // stomping on the memory the user is looking at.
  CHECK(data_->open_);
  CHECK(data_->proxy_);

  result->data_->Clear();

  if (data_->data_in_open_) {
    // We have data from a previous scan.
    VLOG(1) << "Extracting data from scan " << ToString();
    data_->data_in_open_ = false;
    return result->data_->Reset(&data_->controller_,
                                data_->projection_,
                                &data_->client_projection_,
                                make_gscoped_ptr(data_->last_response_.release_data()));
  } else if (data_->last_response_.has_more_results()) {
    // More data is available in this tablet.
    VLOG(1) << "Continuing scan " << ToString();

    // The user has specified a timeout 'data_->timeout_' which should
    // apply to the total time for each call to NextBatch(). However,
    // if this is a fault-tolerant scan, it's preferable to set a shorter
    // timeout (the "default RPC timeout" for each individual RPC call --
    // so that if the server is hung we have time to fail over and try a
    // different server.
    MonoTime now = MonoTime::Now(MonoTime::FINE);

    MonoTime batch_deadline = now;
    batch_deadline.AddDelta(data_->timeout_);

    MonoTime rpc_deadline;
    if (data_->is_fault_tolerant_) {
      rpc_deadline = now;
      rpc_deadline.AddDelta(data_->table_->client()->default_rpc_timeout());
      rpc_deadline = MonoTime::Earliest(batch_deadline, rpc_deadline);
    } else {
      rpc_deadline = batch_deadline;
    }

    data_->controller_.Reset();
    data_->controller_.set_deadline(rpc_deadline);
    data_->PrepareRequest(KuduScanner::Data::CONTINUE);
    Status rpc_status = data_->proxy_->Scan(data_->next_req_,
                                            &data_->last_response_,
                                            &data_->controller_);
    const Status server_status = data_->CheckForErrors();

    // Success case.
    if (rpc_status.ok() && server_status.ok()) {
      if (data_->last_response_.has_last_primary_key()) {
        data_->last_primary_key_ = data_->last_response_.last_primary_key();
      }
      data_->scan_attempts_ = 0;
      return result->data_->Reset(&data_->controller_,
                                  data_->projection_,
                                  &data_->client_projection_,
                                  make_gscoped_ptr(data_->last_response_.release_data()));
    }

    data_->scan_attempts_++;

    // Error handling.
    LOG(WARNING) << "Scan at tablet server " << data_->ts_->ToString() << " of tablet "
        << ToString() << " failed: "
        << (!rpc_status.ok() ? rpc_status.ToString() : server_status.ToString());
    set<string> blacklist;
    vector<internal::RemoteTabletServer*> candidates;
    RETURN_NOT_OK(data_->CanBeRetried(false, rpc_status, server_status, rpc_deadline,
                                      batch_deadline, candidates, &blacklist));

    LOG(WARNING) << "Attempting to retry scan of tablet " << ToString() << " elsewhere.";
    // Use the start partition key of the current tablet as the start partition key.
    const string& partition_key_start = data_->remote_->partition().partition_key_start();
    return data_->OpenTablet(partition_key_start, batch_deadline, &blacklist);
  } else if (data_->MoreTablets()) {
    // More data may be available in other tablets.
    // No need to close the current tablet; we scanned all the data so the
    // server closed it for us.
    VLOG(1) << "Scanning next tablet " << ToString();
    data_->last_primary_key_.clear();
    MonoTime deadline = MonoTime::Now(MonoTime::FINE);
    deadline.AddDelta(data_->timeout_);
    set<string> blacklist;
    RETURN_NOT_OK(data_->OpenTablet(data_->remote_->partition().partition_key_end(),
                                    deadline, &blacklist));
    // No rows written, the next invocation will pick them up.
    return Status::OK();
  } else {
    // No more data anywhere.
    return Status::OK();
  }
}

Status KuduScanner::GetCurrentServer(KuduTabletServer** server) {
  CHECK(data_->open_);
  internal::RemoteTabletServer* rts = data_->ts_;
  CHECK(rts);
  vector<HostPort> host_ports;
  rts->GetHostPorts(&host_ports);
  if (host_ports.empty()) {
    return Status::IllegalState(strings::Substitute("No HostPort found for RemoteTabletServer $0",
                                                    rts->ToString()));
  }
  *server = new KuduTabletServer();
  (*server)->data_ = new KuduTabletServer::Data(rts->permanent_uuid(),
                                                host_ports[0].host());
  return Status::OK();
}

////////////////////////////////////////////////////////////
// KuduTabletServer
////////////////////////////////////////////////////////////

KuduTabletServer::KuduTabletServer()
  : data_(nullptr) {
}

KuduTabletServer::~KuduTabletServer() {
  delete data_;
}

const string& KuduTabletServer::uuid() const {
  return data_->uuid_;
}

const string& KuduTabletServer::hostname() const {
  return data_->hostname_;
}

} // namespace client
} // namespace kudu
