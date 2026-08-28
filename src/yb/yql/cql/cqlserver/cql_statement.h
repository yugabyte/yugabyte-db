//--------------------------------------------------------------------------------------------------
// Copyright (c) YugabyteDB, Inc.
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
//
// This class defines a CQL statement. A CQL statement extends from a SQL statement. Handles the
// following use cases :
// - query ID and caching prepared statements in a list.
// - query ID and caching unprepared statements in a (separate) list.
//--------------------------------------------------------------------------------------------------

#pragma once

#include <list>
#include <memory>
#include <string>
#include <vector>

#include "yb/util/jsonwriter.h"
#include "yb/yql/cql/ql/statement.h"
#include "yb/yql/cql/ql/util/cql_message.h"

// HDR histogram C struct from the third-party HdrHistogram_c library (global scope, extern "C").
struct hdr_histogram;

namespace yb {
namespace cqlserver {

class CQLStatement;

// Tracks the distribution of YCQL statement execution latencies using an HDR histogram.
//
// The histogram configuration (resolution, max latency, bucket factor) and the JSON output format
// intentionally match YSQL's pg_stat_statements yb_latency_histogram column, so that the resulting
// jsonb is compatible with helpers such as yb_get_percentile.
class StmtLatencyHistogram {
 public:
  StmtLatencyHistogram();
  StmtLatencyHistogram(const StmtLatencyHistogram& other);
  StmtLatencyHistogram& operator=(const StmtLatencyHistogram& other);
  ~StmtLatencyHistogram();

  // Records a single execution latency (in milliseconds) into the histogram.
  void Record(double time_in_msec);

  // Clears all recorded latencies.
  void Reset();

  // Writes the histogram as a JSON array (YSQL yb_latency_histogram format) into jw.
  void WriteAsJsonArray(JsonWriter* jw) const;

  // Returns the histogram as a compact JSON array string (YSQL yb_latency_histogram format).
  std::string ToJsonArrayString() const;

 private:
  struct FreeDeleter {
    void operator()(hdr_histogram* h) const;
  };

  // Allocates and initializes an empty histogram using the shared configuration.
  void Allocate();

  std::unique_ptr<hdr_histogram, FreeDeleter> hist_;

  // Count of executions slower than the maximum tracked latency (overflow bucket).
  int64_t slow_executions_ = 0;
};

// A map of CQL query id to the prepared statement for caching the prepared statments. Shared_ptr
// is used so that a prepared statement can be aged out and removed from the cache without deleting
// it when it is being executed by another client in another thread.
using CQLStatementMap = std::unordered_map<ql::CQLMessage::QueryId, std::shared_ptr<CQLStatement>>;

// A LRU list of CQL statements and position in the list.
using CQLStatementList = std::list<std::shared_ptr<CQLStatement>>;
using CQLStatementListPos = CQLStatementList::iterator;

struct StmtCounters{
  explicit StmtCounters(
      const std::string& text, const std::string& keyspace_, bool is_prepared_ = false)
      : query(text), keyspace(keyspace_), is_prepared(is_prepared_) {}

  explicit StmtCounters(const std::shared_ptr<StmtCounters>& other) :
      num_calls(other->num_calls), total_time_in_msec(other->total_time_in_msec),
      min_time_in_msec(other->min_time_in_msec), max_time_in_msec(other->max_time_in_msec),
      sum_var_time_in_msec(other->sum_var_time_in_msec), query(other->query),
      keyspace(other->keyspace), is_prepared(other->is_prepared),
      latency_histogram(other->latency_histogram) {}

  void WriteAsJson(JsonWriter* jw, const ql::CQLMessage::QueryId& query_id) const;

  void ResetCounters();

  double GetStdDevTime() const;

  int64 num_calls = 0;              // Number of times executed.
  double total_time_in_msec = 0.;   // Total execution time, in msec.
  double min_time_in_msec = 0.;     // Minimum execution time in msec.
  double max_time_in_msec = 0.;     // Maximum execution time in msec.
  double sum_var_time_in_msec = 0.; // Sum of variances in execution time in msec.
  std::string query;                // Stores the query text.
  std::string keyspace;             // Stores the keyspace name.
  bool is_prepared = false;         // If the stmt is prepared (batch: if all children prepared).
  StmtLatencyHistogram latency_histogram;  // Distribution of execution latencies.
};

// A CQL statement that is prepared and cached.
class CQLStatement : public ql::Statement {
 public:
  CQLStatement(
      const std::string& keyspace, const std::string& query, CQLStatementListPos pos,
      const MemTrackerPtr& mem_tracker);
  ~CQLStatement();

  // Return the query id.
  ql::CQLMessage::QueryId query_id() const { return GetQueryId(keyspace_, text_); }

  // Get/set position of the statement in the LRU.
  CQLStatementListPos pos() const { return pos_; }
  void set_pos(CQLStatementListPos pos) const { pos_ = pos; }

  // Get schema version this statement used for preparation.
  Result<SchemaVersion> GetYBTableSchemaVersion() const {
    const ql::ParseTree& parser_tree = VERIFY_RESULT(GetParseTree());
    return parser_tree.GetYBTableSchemaVersion();
  }

  // Check if the used schema version is up to date with the Master.
  Result<bool> IsYBTableAltered(ql::QLEnv* ql_env) const;

  // Return the query id of a statement.
  static ql::CQLMessage::QueryId GetQueryId(const std::string& keyspace, const std::string& query);

  // Compute a batch-level query id from child query texts.
  // Builds the canonical batch text "child_1;child_2;...;child_N" (semicolons as separators,
  // not suffixes) and returns the query id (MD5 of keyspace + that text). For a single child,
  // the canonical text equals the child text itself. Writes the canonical text to *batch_text_out.
  static ql::CQLMessage::QueryId GetBatchQueryId(
      const std::string& keyspace, const std::vector<std::string>& child_query_texts,
      std::string* batch_text_out);

  std::shared_ptr<StmtCounters> GetWritableCounters() {
    return stmt_counters_;
  }

  void SetCounters(const std::shared_ptr<StmtCounters>& other) {
    stmt_counters_ = other;
  }

 private:
  // Position of the statement in the LRU.
  mutable CQLStatementListPos pos_;

  // Stores the metrics for a prepared statements.
  std::shared_ptr<StmtCounters> stmt_counters_;

  ScopedTrackedConsumption consumption_;
};

}  // namespace cqlserver
}  // namespace yb
