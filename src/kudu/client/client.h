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
#ifndef KUDU_CLIENT_CLIENT_H
#define KUDU_CLIENT_CLIENT_H

#include <stdint.h>
#include <string>
#include <vector>

#include "kudu/client/row_result.h"
#include "kudu/client/scan_batch.h"
#include "kudu/client/scan_predicate.h"
#include "kudu/client/schema.h"
#include "kudu/client/shared_ptr.h"
#ifdef KUDU_HEADERS_NO_STUBS
#include <gtest/gtest_prod.h>
#include "kudu/gutil/macros.h"
#include "kudu/gutil/port.h"
#else
#include "kudu/client/stubs.h"
#endif
#include "kudu/client/write_op.h"
#include "kudu/util/kudu_export.h"
#include "kudu/util/monotime.h"
#include "kudu/util/status.h"

namespace kudu {

class LinkedListTester;
class PartitionSchema;

namespace client {

class KuduLoggingCallback;
class KuduSession;
class KuduStatusCallback;
class KuduTable;
class KuduTableAlterer;
class KuduTableCreator;
class KuduTabletServer;
class KuduValue;
class KuduWriteOperation;

namespace internal {
class Batcher;
class GetTableSchemaRpc;
class LookupRpc;
class MetaCache;
class RemoteTablet;
class RemoteTabletServer;
class WriteRpc;
} // namespace internal

// Installs a callback for internal client logging. It is invoked for a
// log event of any severity, across any KuduClient instance.
//
// Only the first invocation has any effect; subsequent invocations are
// a no-op. The caller must ensure that 'cb' stays alive until
// UninstallLoggingCallback() is called.
//
// Before a callback is registered, all internal client log events are
// logged to stderr.
void KUDU_EXPORT InstallLoggingCallback(KuduLoggingCallback* cb);

// Removes a callback installed via InstallLoggingCallback().
//
// Only the first invocation has any effect; subsequent invocations are
// a no-op.
//
// Should be called before unloading the client library.
void KUDU_EXPORT UninstallLoggingCallback();

// Set the logging verbosity of the client library. By default, this is 0. Logs become
// progressively more verbose as the level is increased. Empirically, the highest
// verbosity level used in Kudu is 6, which includes very fine-grained tracing
// information. Most useful logging is enabled at level 1 or 2, with the higher levels
// used only in rare circumstances.
//
// Logs are emitted to stderr, or to the configured log callback at SEVERITY_INFO.
//
// This may be called safely at any point during usage of the library.
void KUDU_EXPORT SetVerboseLogLevel(int level);

// The Kudu client library uses signals internally in some cases. By default, it uses
// SIGUSR2. If your application makes use of SIGUSR2, this advanced API can help
// workaround conflicts.
Status KUDU_EXPORT SetInternalSignalNumber(int signum);

// Creates a new KuduClient with the desired options.
//
// Note that KuduClients are shared amongst multiple threads and, as such,
// are stored in shared pointers.
class KUDU_EXPORT KuduClientBuilder {
 public:
  KuduClientBuilder();
  ~KuduClientBuilder();

  KuduClientBuilder& clear_master_server_addrs();

  // Add RPC addresses of multiple masters.
  KuduClientBuilder& master_server_addrs(const std::vector<std::string>& addrs);

  // Add an RPC address of a master. At least one master is required.
  KuduClientBuilder& add_master_server_addr(const std::string& addr);

  // The default timeout used for administrative operations (e.g. CreateTable,
  // AlterTable, ...). Optional.
  //
  // If not provided, defaults to 10s.
  KuduClientBuilder& default_admin_operation_timeout(const MonoDelta& timeout);

  // The default timeout for individual RPCs. Optional.
  //
  // If not provided, defaults to 5s.
  KuduClientBuilder& default_rpc_timeout(const MonoDelta& timeout);

  // Creates the client.
  //
  // The return value may indicate an error in the create operation, or a
  // misuse of the builder; in the latter case, only the last error is
  // returned.
  Status Build(sp::shared_ptr<KuduClient>* client);
 private:
  class KUDU_NO_EXPORT Data;

  // Owned.
  Data* data_;

  DISALLOW_COPY_AND_ASSIGN(KuduClientBuilder);
};

// The KuduClient represents a connection to a cluster. From the user
// perspective, they should only need to create one of these in their
// application, likely a singleton -- but it's not a singleton in Kudu in any
// way. Different Client objects do not interact with each other -- no
// connection pooling, etc. Each KuduClient instance is sandboxed with no
// global cross-client state.
//
// In the implementation, the client holds various pieces of common
// infrastructure which is not table-specific:
//
// - RPC messenger: reactor threads and RPC connections are pooled here
// - Authentication: the client is initialized with some credentials, and
//   all accesses through it share those credentials.
// - Caches: caches of table schemas, tablet locations, tablet server IP
//   addresses, etc are shared per-client.
//
// In order to actually access data on the cluster, callers must first
// create a KuduSession object using NewSession(). A KuduClient may
// have several associated sessions.
//
// TODO: Cluster administration functions are likely to be in this class
// as well.
//
// This class is thread-safe.
class KUDU_EXPORT KuduClient : public sp::enable_shared_from_this<KuduClient> {
 public:
  ~KuduClient();

  // Creates a KuduTableCreator; it is the caller's responsibility to free it.
  KuduTableCreator* NewTableCreator();

  // set 'create_in_progress' to true if a CreateTable operation is in-progress
  Status IsCreateTableInProgress(const std::string& table_name,
                                 bool *create_in_progress);

  Status DeleteTable(const std::string& table_name);

  // Creates a KuduTableAlterer; it is the caller's responsibility to free it.
  KuduTableAlterer* NewTableAlterer(const std::string& table_name);

  // set 'alter_in_progress' to true if an AlterTable operation is in-progress
  Status IsAlterTableInProgress(const std::string& table_name,
                                bool *alter_in_progress);

  Status GetTableSchema(const std::string& table_name,
                        KuduSchema* schema);

  Status ListTabletServers(std::vector<KuduTabletServer*>* tablet_servers);

  // List only those tables whose names pass a substring match on 'filter'.
  //
  // 'tables' is appended to only on success.
  Status ListTables(std::vector<std::string>* tables,
                    const std::string& filter = "");

  // Check if the table given by 'table_name' exists.
  //
  // 'exists' is set only on success.
  Status TableExists(const std::string& table_name, bool* exists);

  // Open the table with the given name. If the table has not been opened before
  // in this client, this will do an RPC to ensure that the table exists and
  // look up its schema.
  //
  // TODO: should we offer an async version of this as well?
  // TODO: probably should have a configurable timeout in KuduClientBuilder?
  Status OpenTable(const std::string& table_name,
                   sp::shared_ptr<KuduTable>* table);

  // Create a new session for interacting with the cluster.
  // User is responsible for destroying the session object.
  // This is a fully local operation (no RPCs or blocking).
  sp::shared_ptr<KuduSession> NewSession();

  // Policy with which to choose amongst multiple replicas.
  enum ReplicaSelection {
    // Select the LEADER replica.
    LEADER_ONLY,

    // Select the closest replica to the client, or a random one if all
    // replicas are equidistant.
    CLOSEST_REPLICA,

    // Select the first replica in the list.
    FIRST_REPLICA
  };

  bool IsMultiMaster() const;

  const MonoDelta& default_admin_operation_timeout() const;
  const MonoDelta& default_rpc_timeout() const;

  // Value for the latest observed timestamp when none has been observed or set.
  static const uint64_t kNoTimestamp;

  // Returns highest HybridTime timestamp observed by the client.
  // The latest observed timestamp can be used to start a snapshot scan on a
  // table which is guaranteed to contain all data written or previously read by
  // this client. See KuduScanner for more details on timestamps.
  uint64_t GetLatestObservedTimestamp() const;

  // Sets the latest observed HybridTime timestamp, encoded in the HybridTime format.
  // This is only useful when forwarding timestamps between clients to enforce
  // external consistency when using KuduSession::CLIENT_PROPAGATED external consistency
  // mode.
  // To use this the user must obtain the HybridTime encoded timestamp from the first
  // client with KuduClient::GetLatestObservedTimestamp() and the set it in the new
  // client with this method.
  void SetLatestObservedTimestamp(uint64_t ht_timestamp);

 private:
  class KUDU_NO_EXPORT Data;

  friend class KuduClientBuilder;
  friend class KuduScanner;
  friend class KuduTable;
  friend class KuduTableAlterer;
  friend class KuduTableCreator;
  friend class internal::Batcher;
  friend class internal::GetTableSchemaRpc;
  friend class internal::LookupRpc;
  friend class internal::MetaCache;
  friend class internal::RemoteTablet;
  friend class internal::RemoteTabletServer;
  friend class internal::WriteRpc;

  FRIEND_TEST(ClientTest, TestGetTabletServerBlacklist);
  FRIEND_TEST(ClientTest, TestMasterDown);
  FRIEND_TEST(ClientTest, TestMasterLookupPermits);
  FRIEND_TEST(ClientTest, TestReplicatedMultiTabletTableFailover);
  FRIEND_TEST(ClientTest, TestReplicatedTabletWritesWithLeaderElection);
  FRIEND_TEST(ClientTest, TestScanFaultTolerance);
  FRIEND_TEST(ClientTest, TestScanTimeout);
  FRIEND_TEST(ClientTest, TestWriteWithDeadMaster);
  FRIEND_TEST(MasterFailoverTest, DISABLED_TestPauseAfterCreateTableIssued);

  KuduClient();

  // Owned.
  Data* data_;

  DISALLOW_COPY_AND_ASSIGN(KuduClient);
};

// Creates a new table with the desired options.
class KUDU_EXPORT KuduTableCreator {
 public:
  ~KuduTableCreator();

  // Sets the name to give the table. It is copied. Required.
  KuduTableCreator& table_name(const std::string& name);

  // Sets the schema with which to create the table. Must remain valid for
  // the lifetime of the builder. Required.
  KuduTableCreator& schema(const KuduSchema* schema);

  // Adds a set of hash partitions to the table.
  //
  // For each set of hash partitions added to the table, the total number of
  // table partitions is multiplied by the number of buckets. For example, if a
  // table is created with 3 split rows, and two hash partitions with 4 and 5
  // buckets respectively, the total number of table partitions will be 80
  // (4 range partitions * 4 hash buckets * 5 hash buckets).
  KuduTableCreator& add_hash_partitions(const std::vector<std::string>& columns,
                                        int32_t num_buckets);

  // Adds a set of hash partitions to the table.
  //
  // This constructor takes a seed value, which can be used to randomize the
  // mapping of rows to hash buckets. Setting the seed may provide some
  // amount of protection against denial of service attacks when the hashed
  // columns contain user provided values.
  KuduTableCreator& add_hash_partitions(const std::vector<std::string>& columns,
                                        int32_t num_buckets, int32_t seed);

  // Sets the columns on which the table will be range-partitioned.
  //
  // Every column must be a part of the table's primary key. If not set, the
  // table will be created with the primary-key columns as the range-partition
  // columns. If called with an empty vector, the table will be created without
  // range partitioning.
  //
  // Optional.
  KuduTableCreator& set_range_partition_columns(const std::vector<std::string>& columns);

  // Sets the rows on which to pre-split the table.
  // The table creator takes ownership of the rows.
  //
  // If any provided row is missing a value for any of the range partition
  // columns, the logical minimum value for that column type will be used by
  // default.
  //
  // If not provided, no range-based pre-splitting is performed.
  //
  // Optional.
  KuduTableCreator& split_rows(const std::vector<const KuduPartialRow*>& split_rows);

  // Sets the number of replicas for each tablet in the table.
  // This should be an odd number. Optional.
  //
  // If not provided (or if <= 0), falls back to the server-side default.
  KuduTableCreator& num_replicas(int n_replicas);

  // Set the timeout for the operation. This includes any waiting
  // after the create has been submitted (i.e if the create is slow
  // to be performed for a large table, it may time out and then
  // later be successful).
  KuduTableCreator& timeout(const MonoDelta& timeout);

  // Wait for the table to be fully created before returning.
  // Optional.
  //
  // If not provided, defaults to true.
  KuduTableCreator& wait(bool wait);

  // Creates the table.
  //
  // The return value may indicate an error in the create table operation,
  // or a misuse of the builder; in the latter case, only the last error is
  // returned.
  Status Create();
 private:
  class KUDU_NO_EXPORT Data;

  friend class KuduClient;

  explicit KuduTableCreator(KuduClient* client);

  // Owned.
  Data* data_;

  DISALLOW_COPY_AND_ASSIGN(KuduTableCreator);
};

// A KuduTable represents a table on a particular cluster. It holds the current
// schema of the table. Any given KuduTable instance belongs to a specific KuduClient
// instance.
//
// Upon construction, the table is looked up in the catalog (or catalog cache),
// and the schema fetched for introspection.
//
// This class is thread-safe.
class KUDU_EXPORT KuduTable : public sp::enable_shared_from_this<KuduTable> {
 public:
  ~KuduTable();

  const std::string& name() const;

  // Return the table's ID. This is an internal identifier which uniquely
  // identifies a table. If the table is deleted and recreated with the same
  // name, the ID will distinguish the old table from the new.
  const std::string& id() const;

  const KuduSchema& schema() const;

  // Create a new write operation for this table. It is the caller's
  // responsibility to free it, unless it is passed to KuduSession::Apply().
  KuduInsert* NewInsert();
  KuduUpdate* NewUpdate();
  KuduDelete* NewDelete();

  // Create a new comparison predicate which can be used for scanners
  // on this table.
  //
  // The type of 'value' must correspond to the type of the column to which
  // the predicate is to be applied. For example, if the given column is
  // any type of integer, the KuduValue should also be an integer, with its
  // value in the valid range for the column type. No attempt is made to cast
  // between floating point and integer values, or numeric and string values.
  //
  // The caller owns the result until it is passed into KuduScanner::AddConjunctPredicate().
  // The returned predicate takes ownership of 'value'.
  //
  // In the case of an error (e.g. an invalid column name), a non-NULL value
  // is still returned. The error will be returned when attempting to add this
  // predicate to a KuduScanner.
  KuduPredicate* NewComparisonPredicate(const Slice& col_name,
                                        KuduPredicate::ComparisonOp op,
                                        KuduValue* value);

  KuduClient* client() const;

  const PartitionSchema& partition_schema() const;

 private:
  class KUDU_NO_EXPORT Data;

  friend class KuduClient;

  KuduTable(const sp::shared_ptr<KuduClient>& client,
            const std::string& name,
            const std::string& table_id,
            const KuduSchema& schema,
            const PartitionSchema& partition_schema);

  // Owned.
  Data* data_;

  DISALLOW_COPY_AND_ASSIGN(KuduTable);
};

// Alters an existing table based on the provided steps.
//
// Sample usage:
//   KuduTableAlterer* alterer = client->NewTableAlterer("table-name");
//   alterer->AddColumn("foo")->Type(KuduColumnSchema::INT32)->NotNull();
//   alterer->AlterColumn("bar")->Compression(KuduColumnStorageAttributes::LZ4);
//   Status s = alterer->Alter();
//   delete alterer;
class KUDU_EXPORT KuduTableAlterer {
 public:
  ~KuduTableAlterer();

  // Renames the table.
  KuduTableAlterer* RenameTo(const std::string& new_name);

  // Adds a new column to the table.
  //
  // When adding a column, you must specify the default value of the new
  // column using KuduColumnSpec::DefaultValue(...).
  KuduColumnSpec* AddColumn(const std::string& name);

  // Alter an existing column.
  KuduColumnSpec* AlterColumn(const std::string& name);

  // Drops an existing column from the table.
  KuduTableAlterer* DropColumn(const std::string& name);

  // Set the timeout for the operation. This includes any waiting
  // after the alter has been submitted (i.e if the alter is slow
  // to be performed on a large table, it may time out and then
  // later be successful).
  KuduTableAlterer* timeout(const MonoDelta& timeout);

  // Wait for the table to be fully altered before returning.
  //
  // If not provided, defaults to true.
  KuduTableAlterer* wait(bool wait);

  // Alters the table.
  //
  // The return value may indicate an error in the alter operation, or a
  // misuse of the builder (e.g. add_column() with default_value=NULL); in
  // the latter case, only the last error is returned.
  Status Alter();

 private:
  class KUDU_NO_EXPORT Data;
  friend class KuduClient;

  KuduTableAlterer(KuduClient* client,
                   const std::string& name);

  // Owned.
  Data* data_;

  DISALLOW_COPY_AND_ASSIGN(KuduTableAlterer);
};

// An error which occurred in a given operation. This tracks the operation
// which caused the error, along with whatever the actual error was.
class KUDU_EXPORT KuduError {
 public:
  ~KuduError();

  // Return the actual error which occurred.
  const Status& status() const;

  // Return the operation which failed.
  const KuduWriteOperation& failed_op() const;

  // Release the operation that failed. The caller takes ownership. Must only
  // be called once.
  KuduWriteOperation* release_failed_op();

  // In some cases, it's possible that the server did receive and successfully
  // perform the requested operation, but the client can't tell whether or not
  // it was successful. For example, if the call times out, the server may still
  // succeed in processing at a later time.
  //
  // This function returns true if there is some chance that the server did
  // process the operation, and false if it can guarantee that the operation
  // did not succeed.
  bool was_possibly_successful() const;

 private:
  class KUDU_NO_EXPORT Data;

  friend class internal::Batcher;
  friend class KuduSession;

  KuduError(KuduWriteOperation* failed_op, const Status& error);

  // Owned.
  Data* data_;

  DISALLOW_COPY_AND_ASSIGN(KuduError);
};


// A KuduSession belongs to a specific KuduClient, and represents a context in
// which all read/write data access should take place. Within a session,
// multiple operations may be accumulated and batched together for better
// efficiency. Settings like timeouts, priorities, and trace IDs are also set
// per session.
//
// A KuduSession's main purpose is for grouping together multiple data-access
// operations together into batches or transactions. It is important to note
// the distinction between these two:
//
// * A batch is a set of operations which are grouped together in order to
//   amortize fixed costs such as RPC call overhead and round trip times.
//   A batch DOES NOT imply any ACID-like guarantees. Within a batch, some
//   operations may succeed while others fail, and concurrent readers may see
//   partial results. If the client crashes mid-batch, it is possible that some
//   of the operations will be made durable while others were lost.
//
// * In contrast, a transaction is a set of operations which are treated as an
//   indivisible semantic unit, per the usual definitions of database transactions
//   and isolation levels.
//
// NOTE: Kudu does not currently support transactions! They are only mentioned
// in the above documentation to clarify that batches are not transactional and
// should only be used for efficiency.
//
// KuduSession is separate from KuduTable because a given batch or transaction
// may span multiple tables. This is particularly important in the future when
// we add ACID support, but even in the context of batching, we may be able to
// coalesce writes to different tables hosted on the same server into the same
// RPC.
//
// KuduSession is separate from KuduClient because, in a multi-threaded
// application, different threads may need to concurrently execute
// transactions. Similar to a JDBC "session", transaction boundaries will be
// delineated on a per-session basis -- in between a "BeginTransaction" and
// "Commit" call on a given session, all operations will be part of the same
// transaction. Meanwhile another concurrent Session object can safely run
// non-transactional work or other transactions without interfering.
//
// Additionally, there is a guarantee that writes from different sessions do not
// get batched together into the same RPCs -- this means that latency-sensitive
// clients can run through the same KuduClient object as throughput-oriented
// clients, perhaps by setting the latency-sensitive session's timeouts low and
// priorities high. Without the separation of batches, a latency-sensitive
// single-row insert might get batched along with 10MB worth of inserts from the
// batch writer, thus delaying the response significantly.
//
// Though we currently do not have transactional support, users will be forced
// to use a KuduSession to instantiate reads as well as writes.  This will make
// it more straight-forward to add RW transactions in the future without
// significant modifications to the API.
//
// Users who are familiar with the Hibernate ORM framework should find this
// concept of a Session familiar.
//
// This class is not thread-safe except where otherwise specified.
class KUDU_EXPORT KuduSession : public sp::enable_shared_from_this<KuduSession> {
 public:
  ~KuduSession();

  enum FlushMode {
    // Every write will be sent to the server in-band with the Apply()
    // call. No batching will occur. This is the default flush mode. In this
    // mode, the Flush() call never has any effect, since each Apply() call
    // has already flushed the buffer. This is the default flush mode.
    AUTO_FLUSH_SYNC,

    // Apply() calls will return immediately, but the writes will be sent in
    // the background, potentially batched together with other writes from
    // the same session. If there is not sufficient buffer space, then Apply()
    // may block for buffer space to be available.
    //
    // Because writes are applied in the background, any errors will be stored
    // in a session-local buffer. Call CountPendingErrors() or GetPendingErrors()
    // to retrieve them.
    // TODO: provide an API for the user to specify a callback to do their own
    // error reporting.
    // TODO: specify which threads the background activity runs on (probably the
    // messenger IO threads?)
    //
    // NOTE: This is not implemented yet, see KUDU-456.
    //
    // The Flush() call can be used to block until the buffer is empty.
    AUTO_FLUSH_BACKGROUND,

    // Apply() calls will return immediately, and the writes will not be
    // sent until the user calls Flush(). If the buffer runs past the
    // configured space limit, then Apply() will return an error.
    MANUAL_FLUSH
  };

  // Set the flush mode.
  // REQUIRES: there should be no pending writes -- call Flush() first to ensure.
  Status SetFlushMode(FlushMode m) WARN_UNUSED_RESULT;

  // The possible external consistency modes on which Kudu operates.
  enum ExternalConsistencyMode {
    // The response to any write will contain a timestamp. Any further calls from the same
    // client to other servers will update those servers with that timestamp. Following
    // write operations from the same client will be assigned timestamps that are strictly
    // higher, enforcing external consistency without having to wait or incur any latency
    // penalties.
    //
    // In order to maintain external consistency for writes between two different clients
    // in this mode, the user must forward the timestamp from the first client to the
    // second by using KuduClient::GetLatestObservedTimestamp() and
    // KuduClient::SetLatestObservedTimestamp().
    //
    // WARNING: Failure to propagate timestamp information through back-channels between
    // two different clients will negate any external consistency guarantee under this
    // mode.
    //
    // This is the default mode.
    CLIENT_PROPAGATED,

    // The server will guarantee that write operations from the same or from other client
    // are externally consistent, without the need to propagate timestamps across clients.
    // This is done by making write operations wait until there is certainty that all
    // follow up write operations (operations that start after the previous one finishes)
    // will be assigned a timestamp that is strictly higher, enforcing external consistency.
    //
    // WARNING: Depending on the clock synchronization state of TabletServers this may
    // imply considerable latency. Moreover operations in COMMIT_WAIT external consistency
    // mode will outright fail if TabletServer clocks are either unsynchronized or
    // synchronized but with a maximum error which surpasses a pre-configured threshold.
    COMMIT_WAIT
  };

  // Set the new external consistency mode for this session.
  Status SetExternalConsistencyMode(ExternalConsistencyMode m) WARN_UNUSED_RESULT;

  // Set the amount of buffer space used by this session for outbound writes.
  // The effect of the buffer size varies based on the flush mode of the
  // session:
  //
  // AUTO_FLUSH_SYNC:
  //   since no buffering is done, this has no effect
  // AUTO_FLUSH_BACKGROUND:
  //   if the buffer space is exhausted, then write calls will block until there
  //   is space available in the buffer.
  // MANUAL_FLUSH:
  //   if the buffer space is exhausted, then write calls will return an error.
  Status SetMutationBufferSpace(size_t size) WARN_UNUSED_RESULT;

  // Set the timeout for writes made in this session.
  void SetTimeoutMillis(int millis);

  // TODO: add "doAs" ability here for proxy servers to be able to act on behalf of
  // other users, assuming access rights.

  // Apply the write operation. Transfers the write_op's ownership to the KuduSession.
  //
  // The behavior of this function depends on the current flush mode. Regardless
  // of flush mode, however, Apply may begin to perform processing in the background
  // for the call (e.g looking up the tablet, etc). Given that, an error may be
  // queued into the PendingErrors structure prior to flushing, even in MANUAL_FLUSH
  // mode.
  //
  // In case of any error, which may occur during flushing or because the write_op
  // is malformed, the write_op is stored in the session's error collector which
  // may be retrieved at any time.
  //
  // This is thread safe.
  Status Apply(KuduWriteOperation* write_op) WARN_UNUSED_RESULT;

  // Similar to the above, except never blocks. Even in the flush modes that
  // return immediately, 'cb' is triggered with the result. The callback may be
  // called by a reactor thread, or in some cases may be called inline by the
  // same thread which calls ApplyAsync(). 'cb' must remain valid until it called.
  //
  // TODO: not yet implemented.
  void ApplyAsync(KuduWriteOperation* write_op, KuduStatusCallback* cb);

  // Flush any pending writes.
  //
  // Returns a bad status if there are any pending errors after the rows have
  // been flushed. Callers should then use GetPendingErrors to determine which
  // specific operations failed.
  //
  // In AUTO_FLUSH_SYNC mode, this has no effect, since every Apply() call flushes
  // itself inline.
  //
  // In the case that the async version of this method is used, then the callback
  // will be called upon completion of the operations which were buffered since the
  // last flush. In other words, in the following sequence:
  //
  //    session->Insert(a);
  //    session->FlushAsync(callback_1);
  //    session->Insert(b);
  //    session->FlushAsync(callback_2);
  //
  // ... 'callback_2' will be triggered once 'b' has been inserted, regardless of whether
  // 'a' has completed or not.
  //
  // Note that this also means that, if FlushAsync is called twice in succession, with
  // no intervening operations, the second flush will return immediately. For example:
  //
  //    session->Insert(a);
  //    session->FlushAsync(callback_1); // called when 'a' is inserted
  //    session->FlushAsync(callback_2); // called immediately!
  //
  // Note that, as in all other async functions in Kudu, the callback may be called
  // either from an IO thread or the same thread which calls FlushAsync. The callback
  // should not block.
  //
  // For FlushAsync, 'cb' must remain valid until it is invoked.
  //
  // This function is thread-safe.
  Status Flush() WARN_UNUSED_RESULT;
  void FlushAsync(KuduStatusCallback* cb);

  // Close the session.
  // Returns an error if there are unflushed or in-flight operations.
  Status Close() WARN_UNUSED_RESULT;

  // Return true if there are operations which have not yet been delivered to the
  // cluster. This may include buffered operations (i.e those that have not yet been
  // flushed) as well as in-flight operations (i.e those that are in the process of
  // being sent to the servers).
  // TODO: maybe "incomplete" or "undelivered" is clearer?
  //
  // This function is thread-safe.
  bool HasPendingOperations() const;

  // Return the number of buffered operations. These are operations that have
  // not yet been flushed - i.e they are not en-route yet.
  //
  // Note that this is different than HasPendingOperations() above, which includes
  // operations which have been sent and not yet responded to.
  //
  // This is only relevant in MANUAL_FLUSH mode, where the result will not
  // decrease except for after a manual Flush, after which point it will be 0.
  // In the other flush modes, data is immediately put en-route to the destination,
  // so this will return 0.
  //
  // This function is thread-safe.
  int CountBufferedOperations() const;

  // Return the number of errors which are pending. Errors may accumulate when
  // using the AUTO_FLUSH_BACKGROUND mode.
  //
  // This function is thread-safe.
  int CountPendingErrors() const;

  // Return any errors from previous calls. If there were more errors
  // than could be held in the session's error storage, then sets *overflowed to true.
  //
  // Caller takes ownership of the returned errors.
  //
  // This function is thread-safe.
  void GetPendingErrors(std::vector<KuduError*>* errors, bool* overflowed);

  KuduClient* client() const;

 private:
  class KUDU_NO_EXPORT Data;

  friend class KuduClient;
  friend class internal::Batcher;
  explicit KuduSession(const sp::shared_ptr<KuduClient>& client);

  // Owned.
  Data* data_;

  DISALLOW_COPY_AND_ASSIGN(KuduSession);
};


// A single scanner. This class is not thread-safe, though different
// scanners on different threads may share a single KuduTable object.
class KUDU_EXPORT KuduScanner {
 public:
  // The possible read modes for scanners.
  enum ReadMode {
    // When READ_LATEST is specified the server will always return committed writes at
    // the time the request was received. This type of read does not return a snapshot
    // timestamp and is not repeatable.
    //
    // In ACID terms this corresponds to Isolation mode: "Read Committed"
    //
    // This is the default mode.
    READ_LATEST,

    // When READ_AT_SNAPSHOT is specified the server will attempt to perform a read
    // at the provided timestamp. If no timestamp is provided the server will take the
    // current time as the snapshot timestamp. In this mode reads are repeatable, i.e.
    // all future reads at the same timestamp will yield the same data. This is
    // performed at the expense of waiting for in-flight transactions whose timestamp
    // is lower than the snapshot's timestamp to complete, so it might incur a latency
    // penalty.
    //
    // In ACID terms this, by itself, corresponds to Isolation mode "Repeatable
    // Read". If all writes to the scanned tablet are made externally consistent,
    // then this corresponds to Isolation mode "Strict-Serializable".
    //
    // Note: there currently "holes", which happen in rare edge conditions, by which writes
    // are sometimes not externally consistent even when action was taken to make them so.
    // In these cases Isolation may degenerate to mode "Read Committed". See KUDU-430.
    READ_AT_SNAPSHOT
  };

  // Whether the rows should be returned in order. This affects the fault-tolerance properties
  // of a scanner.
  enum OrderMode {
    // Rows will be returned in an arbitrary order determined by the tablet server.
    // This is efficient, but unordered scans are not fault-tolerant and cannot be resumed
    // in the case of tablet server failure.
    //
    // This is the default mode.
    UNORDERED,
    // Rows will be returned ordered by primary key. Sorting the rows imposes additional overhead
    // on the tablet server, but means that scans are fault-tolerant and will be resumed at
    // another tablet server in the case of failure.
    ORDERED
  };

  // Default scanner timeout.
  // This is set to 3x the default RPC timeout (see KuduClientBuilder::default_rpc_timeout()).
  enum { kScanTimeoutMillis = 15000 };

  // Initialize the scanner. The given 'table' object must remain valid
  // for the lifetime of this scanner object.
  // TODO: should table be a const pointer?
  explicit KuduScanner(KuduTable* table);
  ~KuduScanner();

  // Set the projection used for this scanner by passing the column names to read.
  //
  // This overrides any previous call to SetProjectedColumns.
  Status SetProjectedColumnNames(const std::vector<std::string>& col_names) WARN_UNUSED_RESULT;

  // Set the projection used for this scanner by passing the column indexes to read.
  //
  // This overrides any previous call to SetProjectedColumns/SetProjectedColumnIndexes.
  Status SetProjectedColumnIndexes(const std::vector<int>& col_indexes) WARN_UNUSED_RESULT;

  // DEPRECATED: See SetProjectedColumnNames
  Status SetProjectedColumns(const std::vector<std::string>& col_names) WARN_UNUSED_RESULT;

  // Add a predicate to this scanner.
  //
  // The predicates act as conjunctions -- i.e, they all must pass for
  // a row to be returned.
  //
  // The Scanner takes ownership of 'pred', even if a bad Status is returned.
  Status AddConjunctPredicate(KuduPredicate* pred) WARN_UNUSED_RESULT;

  // Add a lower bound (inclusive) primary key for the scan.
  // If any bound is already added, this bound is intersected with that one.
  //
  // The scanner does not take ownership of 'key'; the caller may free it afterward.
  Status AddLowerBound(const KuduPartialRow& key);

  // Like AddLowerBound(), but the encoded primary key is an opaque slice of data
  // obtained elsewhere.
  //
  // DEPRECATED: use AddLowerBound
  Status AddLowerBoundRaw(const Slice& key);

  // Add an upper bound (exclusive) primary key for the scan.
  // If any bound is already added, this bound is intersected with that one.
  //
  // The scanner makes a copy of 'key'; the caller may free it afterward.
  Status AddExclusiveUpperBound(const KuduPartialRow& key);

  // Like AddExclusiveUpperBound(), but the encoded primary key is an opaque slice of data
  // obtained elsewhere.
  //
  // DEPRECATED: use AddExclusiveUpperBound
  Status AddExclusiveUpperBoundRaw(const Slice& key);

  // Add a lower bound (inclusive) partition key for the scan.
  //
  // The scanner makes a copy of 'partition_key'; the caller may free it afterward.
  //
  // This method is unstable, and for internal use only.
  Status AddLowerBoundPartitionKeyRaw(const Slice& partition_key);

  // Add an upper bound (exclusive) partition key for the scan.
  //
  // The scanner makes a copy of 'partition_key'; the caller may free it afterward.
  //
  // This method is unstable, and for internal use only.
  Status AddExclusiveUpperBoundPartitionKeyRaw(const Slice& partition_key);

  // Set the block caching policy for this scanner. If true, scanned data blocks will be cached
  // in memory and made available for future scans. Default is true.
  Status SetCacheBlocks(bool cache_blocks);

  // Begin scanning.
  Status Open();

  // Keeps the current remote scanner alive on the Tablet server for an additional
  // time-to-live (set by a configuration flag on the tablet server).
  // This is useful if the interval in between NextBatch() calls is big enough that the
  // remote scanner might be garbage collected (default ttl is set to 60 secs.).
  // This does not invalidate any previously fetched results.
  // This returns a non-OK status if the scanner was already garbage collected or if
  // the TabletServer was unreachable, for any reason.
  //
  // NOTE: A non-OK status returned by this method should not be taken as indication that
  // the scan has failed. Subsequent calls to NextBatch() might still be successful,
  // particularly if SetFaultTolerant() was called.
  Status KeepAlive();

  // Close the scanner.
  // This releases resources on the server.
  //
  // This call does not block, and will not ever fail, even if the server
  // cannot be contacted.
  //
  // NOTE: the scanner is reset to its initial state by this function.
  // You'll have to re-add any projection, predicates, etc if you want
  // to reuse this Scanner object.
  void Close();

  // Return true if there may be rows to be fetched from this scanner.
  //
  // Note: will be true provided there's at least one more tablet left to
  // scan, even if that tablet has no data (we'll only know once we scan it).
  // It will also be true after the initially opening the scanner before
  // NextBatch is called for the first time.
  bool HasMoreRows() const;

  // Clears 'rows' and populates it with the next batch of rows from the tablet server.
  // A call to NextBatch() invalidates all previously fetched results which might
  // now be pointing to garbage memory.
  //
  // DEPRECATED: Use NextBatch(KuduScanBatch*) instead.
  Status NextBatch(std::vector<KuduRowResult>* rows);

  // Fetches the next batch of results for this scanner.
  //
  // A single KuduScanBatch instance may be reused. Each subsequent call replaces the data
  // from the previous call, and invalidates any KuduScanBatch::RowPtr objects previously
  // obtained from the batch.
  Status NextBatch(KuduScanBatch* batch);

  // Get the KuduTabletServer that is currently handling the scan.
  // More concretely, this is the server that handled the most recent Open or NextBatch
  // RPC made by the server.
  Status GetCurrentServer(KuduTabletServer** server);

  // Set the hint for the size of the next batch in bytes.
  // If setting to 0 before calling Open(), it means that the first call
  // to the tablet server won't return data.
  Status SetBatchSizeBytes(uint32_t batch_size);

  // Sets the replica selection policy while scanning.
  //
  // TODO: kill this in favor of a consistency-level-based API
  Status SetSelection(KuduClient::ReplicaSelection selection) WARN_UNUSED_RESULT;

  // Sets the ReadMode. Default is READ_LATEST.
  Status SetReadMode(ReadMode read_mode) WARN_UNUSED_RESULT;

  // DEPRECATED: use SetFaultTolerant.
  Status SetOrderMode(OrderMode order_mode) WARN_UNUSED_RESULT;

  // Scans are by default non fault-tolerant, and scans will fail if scanning an
  // individual tablet fails (for example, if a tablet server crashes in the
  // middle of a tablet scan).
  //
  // If this method is called, the scan will be resumed at another tablet server
  // in the case of failure.
  //
  // Fault tolerant scans typically have lower throughput than non
  // fault-tolerant scans. Fault tolerant scans use READ_AT_SNAPSHOT mode,
  // if no snapshot timestamp is provided, the server will pick one.
  Status SetFaultTolerant() WARN_UNUSED_RESULT;

  // Sets the snapshot timestamp, in microseconds since the epoch, for scans in
  // READ_AT_SNAPSHOT mode.
  Status SetSnapshotMicros(uint64_t snapshot_timestamp_micros) WARN_UNUSED_RESULT;

  // Sets the snapshot timestamp in raw encoded form (i.e. as returned by a
  // previous call to a server), for scans in READ_AT_SNAPSHOT mode.
  Status SetSnapshotRaw(uint64_t snapshot_timestamp) WARN_UNUSED_RESULT;

  // Sets the maximum time that Open() and NextBatch() are allowed to take.
  Status SetTimeoutMillis(int millis);

  // Returns the schema of the projection being scanned.
  KuduSchema GetProjectionSchema() const;

  // Returns a string representation of this scan.
  std::string ToString() const;
 private:
  class KUDU_NO_EXPORT Data;

  FRIEND_TEST(ClientTest, TestScanCloseProxy);
  FRIEND_TEST(ClientTest, TestScanFaultTolerance);
  FRIEND_TEST(ClientTest, TestScanNoBlockCaching);
  FRIEND_TEST(ClientTest, TestScanTimeout);

  // Owned.
  Data* data_;

  DISALLOW_COPY_AND_ASSIGN(KuduScanner);
};

// In-memory representation of a remote tablet server.
class KUDU_EXPORT KuduTabletServer {
 public:
  ~KuduTabletServer();

  // Returns the UUID of this tablet server. Is globally unique and
  // guaranteed not to change for the lifetime of the tablet server.
  const std::string& uuid() const;

  // Returns the hostname of the first RPC address that this tablet server
  // is listening on.
  const std::string& hostname() const;

 private:
  class KUDU_NO_EXPORT Data;

  friend class KuduClient;
  friend class KuduScanner;

  KuduTabletServer();

  // Owned.
  Data* data_;

  DISALLOW_COPY_AND_ASSIGN(KuduTabletServer);
};

} // namespace client
} // namespace kudu
#endif
