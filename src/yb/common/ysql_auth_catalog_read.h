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

#pragma once

#include <string_view>

#include "yb/common/common_types.pb.h"
#include "yb/common/entity_ids.h"
#include "yb/common/read_hybrid_time.h"

namespace yb {

namespace auth_catalog_read_internal {

struct Relation {
  uint32_t oid;
  uint32_t indexed_table_oid;
  TableId id;
};

inline const Relation* FindRelation(std::string_view table_id) {
  // OIDs from src/postgres/src/include/catalog/pg_{authid,database,auth_members,
  // db_role_setting,yb_catalog_version,yb_logical_client_version}.h. Exact IDs also exclude
  // other databases and the prior-major-version catalogs.
  static const Relation relations[] = {
      {1260, 0, GetPgsqlTableId(kTemplate1Oid, 1260)},
      {2676, 1260, GetPgsqlTableId(kTemplate1Oid, 2676)},
      {2677, 1260, GetPgsqlTableId(kTemplate1Oid, 2677)},
      {1262, 0, GetPgsqlTableId(kTemplate1Oid, 1262)},
      {2671, 1262, GetPgsqlTableId(kTemplate1Oid, 2671)},
      {2672, 1262, GetPgsqlTableId(kTemplate1Oid, 2672)},
      {1261, 0, GetPgsqlTableId(kTemplate1Oid, 1261)},
      {2694, 1261, GetPgsqlTableId(kTemplate1Oid, 2694)},
      {2695, 1261, GetPgsqlTableId(kTemplate1Oid, 2695)},
      {2964, 0, GetPgsqlTableId(kTemplate1Oid, 2964)},
      {2965, 2964, GetPgsqlTableId(kTemplate1Oid, 2965)},
      {8010, 0, GetPgsqlTableId(kTemplate1Oid, 8010)},
      {8012, 8010, GetPgsqlTableId(kTemplate1Oid, 8012)},
      {8073, 0, GetPgsqlTableId(kTemplate1Oid, 8073)},
      {8075, 8073, GetPgsqlTableId(kTemplate1Oid, 8075)},
  };
  for (const auto& relation : relations) {
    if (relation.id == table_id) {
      return &relation;
    }
  }
  return nullptr;
}

}  // namespace auth_catalog_read_internal

// Deliberately limited to the full-table/index scans used by authentication prefetch. Both
// protobuf and lightweight messages use this validator, so producer and serving gates agree.
template <class Request>
bool IsYsqlAuthCatalogRead(const Request& req) {
  const auto* relation = auth_catalog_read_internal::FindRelation(req.table_id());
  if (!relation || (req.has_client() && req.client() != YQL_CLIENT_PGSQL) ||
      req.has_row_mark_type() || req.has_wait_policy() ||
      req.has_sampling_state() || req.sample_blocks_size() || req.is_for_backfill() ||
      req.has_backfill_spec() || req.has_vector_idx_options() ||
      req.has_get_tablet_key_ranges_request() || req.has_ysql_catalog_version() ||
      req.has_ysql_db_catalog_version() || req.has_ysql_db_oid() ||
      req.skip_intents_read() || req.read_at_in_txn_limit() || req.is_aggregate() ||
      req.distinct() || req.prefix_length() || req.has_hash_code() || req.has_max_hash_code() ||
      req.has_ybctid_column_value() || req.partition_column_values_size() ||
      req.range_column_values_size() || req.batch_arguments_size() || req.has_where_expr() ||
      req.has_condition_expr() || req.where_clauses_size() || req.has_lower_bound() ||
      req.has_upper_bound() || req.has_deprecated_max_partition_key()) {
    return false;
  }
  // No server-side expression evaluation (including PG expressions) in this scope.
  for (const auto& target : req.targets()) {
    if (!target.has_column_id()) {
      return false;
    }
  }
  if (req.has_paging_state()) {
    const auto& paging = req.paging_state();
    if ((!paging.table_id().empty() &&
         std::string_view(paging.table_id()) != std::string_view(req.table_id())) ||
        paging.has_distance() || paging.has_main_key() || paging.has_next_tablet_bound()) {
      return false;
    }
  }
  if (req.has_index_request()) {
    const auto& index = req.index_request();
    const auto* index_relation = auth_catalog_read_internal::FindRelation(index.table_id());
    if (relation->indexed_table_oid || !index_relation ||
        index_relation->indexed_table_oid != relation->oid || !IsYsqlAuthCatalogRead(index)) {
      return false;
    }
  }
  if (!req.partition_key().empty()) {
    // Range scans route later pages using the innermost scan's continuation key.
    const auto& scan = req.has_index_request() ? req.index_request() : req;
    if (!scan.has_paging_state() ||
        std::string_view(req.partition_key()) !=
            std::string_view(scan.paging_state().next_partition_key())) {
      return false;
    }
  }
  return true;
}

// Paging must not introduce a second snapshot, even inside a nested index request.
template <class Request>
bool YsqlAuthCatalogPagingMatchesReadTime(const Request& req, HybridTime read_time) {
  if (req.has_paging_state() && req.paging_state().has_read_time() &&
      ReadHybridTime::FromPB(req.paging_state().read_time()) !=
          ReadHybridTime::SingleTime(read_time)) {
    return false;
  }
  return !req.has_index_request() ||
         YsqlAuthCatalogPagingMatchesReadTime(req.index_request(), read_time);
}

}  // namespace yb
