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
//--------------------------------------------------------------------------------------------------

#include "yb/yql/cql/cqlserver/cql_statement.h"

#include <openssl/md5.h>

#include <hdr/hdr_histogram.h>

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <sstream>

#include "yb/gutil/stringprintf.h"
#include "yb/gutil/strings/escaping.h"

DEFINE_RUNTIME_bool(cql_use_metadata_cache_for_schema_version_check, true,
                    "Use the internal Table Metadata Cache in TS to check the Table "
                    "Schema Version when processing the YCQL PREPARE query."
                    "If disabled - the Table Schema Version is requested from the Master.");
TAG_FLAG(cql_use_metadata_cache_for_schema_version_check, advanced);

using std::string;

namespace yb {
namespace cqlserver {

namespace {

// Histogram configuration constants, kept identical to YSQL's pg_stat_statements defaults so the
// resulting yb_latency_histogram jsonb is comparable across YSQL and YCQL.
constexpr double kYbHdrLatencyResMs = 0.1;
constexpr double kYbHdrMaxLatencyMs = 1677721.6;
constexpr int kYbHdrBucketFactor = 16;

// Shared, immutable configuration derived once for all YCQL statement histograms.
struct HdrConfig {
  hdr_histogram_bucket_config cfg;
  int64_t max_value;   // Latencies at or above this (in resolution units) go to the overflow bucket.
  size_t alloc_size;   // Bytes to allocate for a histogram (struct + inline counts array).
};

const HdrConfig& GetHdrConfig() {
  // Mirrors the bucket-config derivation in pg_stat_statements.c so both APIs use the same buckets.
  static const HdrConfig config = [] {
    HdrConfig c;
    const int64_t prelim_max_value = static_cast<int64_t>(kYbHdrMaxLatencyMs / kYbHdrLatencyResMs);
    yb_hdr_calculate_bucket_config(1, prelim_max_value - 1, kYbHdrBucketFactor, &c.cfg);

    const int derived_max_magnitude =
        c.cfg.sub_bucket_half_count_magnitude + c.cfg.bucket_count;
    c.max_value = static_cast<int64_t>(std::pow(2, derived_max_magnitude));
    if (prelim_max_value != c.max_value) {
      yb_hdr_calculate_bucket_config(1, c.max_value - 1, kYbHdrBucketFactor, &c.cfg);
    }
    c.alloc_size =
        sizeof(hdr_histogram) + static_cast<size_t>(c.cfg.counts_len) * sizeof(count_t);
    return c;
  }();
  return config;
}

}  // namespace

void StmtLatencyHistogram::FreeDeleter::operator()(hdr_histogram* h) const {
  free(h);
}

StmtLatencyHistogram::StmtLatencyHistogram() {
  Allocate();
}

StmtLatencyHistogram::StmtLatencyHistogram(const StmtLatencyHistogram& other)
    : slow_executions_(other.slow_executions_) {
  const auto& config = GetHdrConfig();
  hist_.reset(static_cast<hdr_histogram*>(malloc(config.alloc_size)));
  memcpy(hist_.get(), other.hist_.get(), config.alloc_size);
}

StmtLatencyHistogram& StmtLatencyHistogram::operator=(const StmtLatencyHistogram& other) {
  if (this != &other) {
    const auto& config = GetHdrConfig();
    memcpy(hist_.get(), other.hist_.get(), config.alloc_size);
    slow_executions_ = other.slow_executions_;
  }
  return *this;
}

StmtLatencyHistogram::~StmtLatencyHistogram() = default;

void StmtLatencyHistogram::Allocate() {
  const auto& config = GetHdrConfig();
  hist_.reset(static_cast<hdr_histogram*>(calloc(1, config.alloc_size)));
  auto cfg = config.cfg;
  hdr_init_preallocated(hist_.get(), &cfg);
}

void StmtLatencyHistogram::Record(double time_in_msec) {
  if (time_in_msec < 0) {
    time_in_msec = 0;
  }
  const auto& config = GetHdrConfig();
  const int64_t value = static_cast<int64_t>(time_in_msec / kYbHdrLatencyResMs);
  if (value < config.max_value) {
    hdr_record_value(hist_.get(), value);
  } else {
    ++slow_executions_;
  }
}

void StmtLatencyHistogram::Reset() {
  hdr_reset(hist_.get());
  slow_executions_ = 0;
}

void StmtLatencyHistogram::WriteAsJsonArray(JsonWriter* jw) const {
  const auto& config = GetHdrConfig();
  jw->StartArray();

  hdr_iter iter;
  hdr_iter_init(&iter, hist_.get());
  while (hdr_iter_next(&iter)) {
    if (iter.count > 0) {
      jw->StartObject();
      jw->String(StringPrintf(
          "[%.1f,%.1f)", iter.value_iterated_to * kYbHdrLatencyResMs,
          (iter.highest_equivalent_value + 1) * kYbHdrLatencyResMs));
      jw->Int64(iter.count);
      jw->EndObject();
    }
  }

  if (slow_executions_ > 0) {
    jw->StartObject();
    jw->String(StringPrintf("[%.1f,)", config.max_value * kYbHdrLatencyResMs));
    jw->Int64(slow_executions_);
    jw->EndObject();
  }

  jw->EndArray();
}

std::string StmtLatencyHistogram::ToJsonArrayString() const {
  std::stringstream ss;
  JsonWriter jw(&ss, JsonWriter::COMPACT);
  WriteAsJsonArray(&jw);
  return ss.str();
}

//------------------------------------------------------------------------------------------------
CQLStatement::CQLStatement(
    const string& keyspace, const string& query, const CQLStatementListPos pos,
    const MemTrackerPtr& mem_tracker)
    : Statement(keyspace, query), pos_(pos), consumption_(mem_tracker, DynamicMemoryUsage()) {}

CQLStatement::~CQLStatement() {
}

Result<bool> CQLStatement::IsYBTableAltered(ql::QLEnv* ql_env) const {
  const ql::ParseTree& parser_tree = VERIFY_RESULT(GetParseTree());
  const bool use_cache = FLAGS_cql_use_metadata_cache_for_schema_version_check;
  return parser_tree.IsYBTableAltered(ql_env, use_cache);
}

ql::CQLMessage::QueryId CQLStatement::GetQueryId(const string& keyspace, const string& query) {
  unsigned char md5[MD5_DIGEST_LENGTH];
  MD5_CTX md5ctx;
  MD5_Init(&md5ctx);
  MD5_Update(&md5ctx, to_uchar_ptr(keyspace.data()), keyspace.length());
  MD5_Update(&md5ctx, to_uchar_ptr(query.data()), query.length());
  MD5_Final(md5, &md5ctx);
  return ql::CQLMessage::QueryId(to_char_ptr(md5), sizeof(md5));
}

ql::CQLMessage::QueryId CQLStatement::GetBatchQueryId(
    const string& keyspace, const std::vector<string>& child_query_texts, string* batch_text_out) {
  DCHECK_NOTNULL(batch_text_out)->clear();
  // Semicolons separate child texts so that a single-child batch produces the
  // same query id as the equivalent non-batch statement.
  // Semicolon is safe as a separator: the CQL binary protocol strips trailing
  // semicolons before transmitting query text, so child texts never contain ';'.
  for (size_t i = 0; i < child_query_texts.size(); ++i) {
    if (i > 0)
      batch_text_out->push_back(';');
    batch_text_out->append(child_query_texts[i]);
  }
  return GetQueryId(keyspace, *batch_text_out);
}

void StmtCounters::WriteAsJson(
    JsonWriter *jw, const ql::CQLMessage::QueryId& query_id) const {
  jw->StartObject();
  jw->String("keyspace");
  jw->String(this->keyspace);

  jw->String("query_id");
  // Write only the 8 bytes of the query_id instead of 16.
  jw->Int64(ql::CQLMessage::QueryIdAsUint64(query_id));

  jw->String("query");
  jw->String(this->query);

  jw->String("is_prepared");
  jw->Bool(this->is_prepared);

  jw->String("calls");
  jw->Int64(this->num_calls);

  jw->String("total_time");
  jw->Double(this->total_time_in_msec);

  jw->String("min_time");
  jw->Double(this->min_time_in_msec);

  jw->String("max_time");
  jw->Double(this->max_time_in_msec);

  jw->String("mean_time");
  jw->Double(this->total_time_in_msec/this->num_calls);

  // Note we are calculating the population variance here, not the
  // sample variance, as we have data for the whole population, so
  // Bessel's correction is not used, and we don't divide by
  // this->num_calls-1.
  const double stddev_time = GetStdDevTime();

  jw->String("stddev_time");
  jw->Double(stddev_time);

  jw->String("yb_latency_histogram");
  this->latency_histogram.WriteAsJsonArray(jw);
  jw->EndObject();
}

void StmtCounters::ResetCounters() {
  this->num_calls = 0;
  this->total_time_in_msec = 0.;
  this->min_time_in_msec = 0.;
  this->max_time_in_msec = 0.;
  this->sum_var_time_in_msec = 0.;
  this->latency_histogram.Reset();
}

double StmtCounters::GetStdDevTime() const {
  return this->num_calls == 0 ? 0. :
      sqrt(this->sum_var_time_in_msec / this->num_calls);
}

}  // namespace cqlserver
}  // namespace yb
