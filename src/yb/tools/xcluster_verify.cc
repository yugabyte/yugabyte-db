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

#include "yb/tools/xcluster_verify.h"

#include <boost/algorithm/string.hpp>

#include "yb/common/ql_type.h"
#include "yb/common/schema.h"

#include "yb/tserver/tserver_error.h"

#include "yb/util/format.h"
#include "yb/util/status_format.h"

namespace yb::tools {
namespace {

bool ContainsIgnoreCase(const std::string& haystack, const char* needle) {
  return boost::algorithm::ifind_first(haystack, needle);
}

}  // namespace

std::string SchemaFingerprint::ToString() const {
  std::string out = "[";
  for (size_t i = 0; i < columns.size(); ++i) {
    if (i > 0) {
      out += ", ";
    }
    const auto& col = columns[i];
    out += Format(
        "($0,$1,key=$2,hash=$3,null=$4,static=$5,sort=$6)", col.id, col.type, col.is_key,
        col.is_hash_key, col.is_nullable, col.is_static, col.sorting_type);
  }
  // Outside the column list, so a mismatch caused by one of them is distinguishable from identical
  // column lists differing for no visible reason.
  out += Format("] partitioning_version=$0 ttl=$1", partitioning_version, default_time_to_live);
  return out;
}

Result<SchemaFingerprint> BuildSchemaFingerprint(const client::YBSchema& schema) {
  // YBSchema::ColumnId reads Schema::col_ids_ behind nothing but a DCHECK, so an id-less schema
  // (YBSchemaBuilder assembles exactly that) walks off the end of an empty vector in a release
  // build. Rejected rather than read as positions: positions compare equal across a pair whose ids
  // differ, which is the mismatch this fingerprint exists to catch.
  SCHECK(
      client::internal::GetSchema(schema).has_column_ids(), InvalidArgument,
      "Cannot fingerprint a schema that carries no column ids");
  SchemaFingerprint fingerprint;
  fingerprint.partitioning_version = schema.table_properties().partitioning_version();
  fingerprint.default_time_to_live = schema.table_properties().DefaultTimeToLive();
  fingerprint.columns.reserve(schema.num_columns());
  for (size_t i = 0; i < schema.num_columns(); ++i) {
    auto col = schema.Column(i);
    ColumnFingerprint row;
    row.id = schema.ColumnId(i);
    row.type = col.type() ? col.type()->ToString() : "";
    row.is_key = col.is_key();
    row.is_hash_key = col.is_hash_key();
    row.is_nullable = col.is_nullable();
    row.is_static = col.is_static();
    row.sorting_type = col.sorting_type();
    fingerprint.columns.push_back(std::move(row));
  }
  return fingerprint;
}

XClusterVerifyResult ClassifyStatus(
    const Status& status, XClusterClassifyContext context) {
  // kMatch is a positive claim that two sides agree, which no single Status can support, so an OK
  // status is classified as the result that claims nothing rather than as agreement.
  DCHECK(!status.ok()) << "ClassifyStatus classifies failures; OK is not a verdict";
  if (status.ok()) {
    return XClusterVerifyResult::kError;
  }
  // Read before the timeout below. Rpc::Finished (rpc.cc) reports a deadline expiry by folding the
  // last error's text into a fresh TimedOut, so a table dropped mid-verify arrives as a TimedOut
  // whose inner NotFound code is gone and whose message is the only surviving evidence. Read as a
  // plain timeout it would be kTryAgain, which a driver retries unchanged, and a dropped table
  // never comes back. The cost is that a master failing for its own reasons, with a message that
  // happens to name something not found, is called a schema mismatch when it is infra.
  if (context == XClusterClassifyContext::kSchema) {
    if (status.IsNotFound()) {
      return XClusterVerifyResult::kSchemaMismatch;
    }
    // Without the file and line ToString prepends by default, which the match would otherwise read
    // as though it were part of the message.
    const auto message = status.ToString(/* include_file_and_line = */ false);
    if (ContainsIgnoreCase(message, "not found") ||
        ContainsIgnoreCase(message, "does not exist")) {
      return XClusterVerifyResult::kSchemaMismatch;
    }
  }
  // A replica that has not caught up to the read time reports TryAgain, which no generic Status
  // predicate distinguishes from an unrelated retryable failure, so the tserver code is what names
  // it (tablet_dump_helper.cc, ReadTimeNotReachedStatus).
  if (tserver::TabletServerError(status) == tserver::TabletServerErrorPB::READ_TIME_NOT_REACHED) {
    return XClusterVerifyResult::kTryAgain;
  }
  // kTryAgain rather than kError because the common cause is a replica still catching up, and a
  // driver told "infra" would stop retrying a slice that would succeed. A master or tserver that is
  // simply down times out identically, which is why kTryAgain does not bound its own retries.
  if (status.IsSnapshotTooOld() || status.IsTimedOut()) {
    return XClusterVerifyResult::kTryAgain;
  }
  return XClusterVerifyResult::kError;
}

}  // namespace yb::tools
