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

#pragma once

#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "yb/util/monotime.h"
#include "yb/util/result.h"

namespace yb {
namespace tserver {

class TabletServerIf;

struct YsqlCallHomeQuery {
  std::string_view key;
  std::string_view query;
};

struct YsqlClusterQueries {
  // Cluster-level queries: run once from any DB, hit shared/global catalogs only.
  static constexpr YsqlCallHomeQuery kClusterLevel[] = {
      {"databases", R"(
        SELECT COUNT(*) AS count,
               pg_encoding_to_char(encoding) AS encoding,
               datcollate, datctype, datconnlimit
        FROM pg_database
        WHERE datistemplate = false
        GROUP BY encoding, datcollate, datctype, datconnlimit
      )"},

      {"roles", R"(
        SELECT rolsuper, rolcreatedb, rolcreaterole, rolcanlogin,
               rolreplication, rolbypassrls,
               COUNT(*) AS count
        FROM pg_roles
        GROUP BY rolsuper, rolcreatedb, rolcreaterole, rolcanlogin,
                 rolreplication, rolbypassrls
      )"},

      // Table counts are per-DB in `tablespace_tables` since pg_class is per-database.
      // First custom tablespace is numbered custom_3 (pg_default and pg_global take 1 and 2).
      {"tablespaces", R"(
        SELECT CASE WHEN spcname IN ('pg_default', 'pg_global') THEN spcname
                    ELSE 'custom_' || ROW_NUMBER() OVER (ORDER BY oid)
               END AS ts_type,
               spcoptions
        FROM pg_tablespace
      )"},

      // Shared catalog. Usually empty (needs superuser + replication commands enabled).
      {"subscriptions", R"(
        SELECT subenabled, COUNT(*) AS count
        FROM pg_subscription
        GROUP BY subenabled
      )"},
  };

  // DB-level queries: run for each user database against per-DB catalogs.
  static constexpr YsqlCallHomeQuery kDbLevel[] = {
      {"yb_colocated", R"(
        SELECT yb_is_database_colocated() AS colocated
      )"},

      {"extensions", R"(
        SELECT extname AS name, extversion AS version
        FROM pg_extension
        WHERE extname != 'plpgsql'
      )"},

      {"tables", R"(
        SELECT COUNT(*) AS count FROM pg_stat_user_tables
      )"},

      // ts_type numbering matches the cluster-level `tablespaces` query (same OID order).
      {"tablespace_tables", R"(
        WITH ts AS (
          SELECT oid, spcname,
                 CASE WHEN spcname IN ('pg_default', 'pg_global') THEN spcname
                      ELSE 'custom_' || ROW_NUMBER() OVER (ORDER BY oid)
                 END AS ts_type
          FROM pg_tablespace)
        SELECT ts.ts_type, COUNT(*) AS table_count
        FROM pg_class c
        JOIN pg_namespace n ON c.relnamespace = n.oid
        JOIN ts ON ts.oid = c.reltablespace
                OR (c.reltablespace = 0 AND ts.spcname = 'pg_default')
        WHERE c.relkind IN ('r', 'p')
          AND n.nspname !~ '^pg_' AND n.nspname <> 'information_schema'
        GROUP BY ts.ts_type
      )"},

      {"indexes", R"(
        SELECT COUNT(*) AS count FROM pg_stat_user_indexes
      )"},

      {"constraints", R"(
        SELECT contype, COUNT(*) AS count
        FROM pg_constraint c
        JOIN pg_namespace n ON c.connamespace = n.oid
        WHERE n.nspname !~ '^pg_' AND n.nspname <> 'information_schema'
        GROUP BY contype
      )"},

      {"schemas", R"(
        SELECT COUNT(*) AS count
        FROM pg_namespace
        WHERE nspname !~ '^pg_' AND nspname <> 'information_schema'
      )"},

      {"triggers", R"(
        SELECT tgtype, COUNT(*) AS count
        FROM pg_trigger
        WHERE NOT tgisinternal
        GROUP BY tgtype
      )"},

      // Excludes extension-owned functions via pg_depend to avoid counting library functions
      // (e.g. pgcrypto, pg_stat_statements) as user code.
      {"functions", R"(
        SELECT l.lanname AS language,
               p.prokind AS kind,
               p.provolatile AS volatility,
               COUNT(*) AS count
        FROM pg_proc p
        JOIN pg_namespace n ON p.pronamespace = n.oid
        JOIN pg_language l ON p.prolang = l.oid
        WHERE n.nspname !~ '^pg_' AND n.nspname <> 'information_schema'
          AND NOT EXISTS (
            SELECT 1 FROM pg_depend d
            WHERE d.classid = 'pg_proc'::regclass
              AND d.objid = p.oid
              AND d.deptype = 'e')
        GROUP BY l.lanname, p.prokind, p.provolatile
        ORDER BY count DESC
      )"},

      // relkind: r=table, i=index, S=sequence, v=view, p=partitioned, m=matview, c=composite,
      // f=foreign table.
      {"classes", R"(
        SELECT relkind, COUNT(*) AS count
        FROM pg_class c
        JOIN pg_namespace n ON c.relnamespace = n.oid
        WHERE n.nspname !~ '^pg_' AND n.nspname <> 'information_schema'
        GROUP BY relkind
      )"},

      {"attributes", R"(
        SELECT a.atttypid::regtype AS type, COUNT(*) AS count
        FROM pg_attribute a
        JOIN pg_class c ON a.attrelid = c.oid
        JOIN pg_namespace n ON c.relnamespace = n.oid
        WHERE a.attnum > 0
          AND NOT a.attisdropped
          AND n.nspname !~ '^pg_' AND n.nspname <> 'information_schema'
          AND c.relkind IN ('r', 'p')
        GROUP BY a.atttypid::regtype
        ORDER BY count DESC
      )"},

      // Returns one row of NULLs if the database has no user tables.
      {"column_stats", R"(
        SELECT MIN(col_count) AS min_cols,
               MAX(col_count) AS max_cols,
               AVG(col_count)::numeric(10,1) AS mean_cols,
               PERCENTILE_CONT(0.5) WITHIN GROUP (ORDER BY col_count) AS median_cols
        FROM (
          SELECT c.oid, COUNT(*) AS col_count
          FROM pg_attribute a
          JOIN pg_class c ON a.attrelid = c.oid
          JOIN pg_namespace n ON c.relnamespace = n.oid
          WHERE a.attnum > 0 AND NOT a.attisdropped
            AND n.nspname !~ '^pg_' AND n.nspname <> 'information_schema'
            AND c.relkind IN ('r', 'p')
          GROUP BY c.oid
        ) sub
      )"},

      // Excludes extension-owned types (e.g. array types created by pgcrypto/pg_stat_statements).
      {"types", R"(
        SELECT typtype, COUNT(*) AS count
        FROM pg_type t
        JOIN pg_namespace n ON t.typnamespace = n.oid
        WHERE n.nspname !~ '^pg_' AND n.nspname <> 'information_schema'
          AND NOT EXISTS (
            SELECT 1 FROM pg_depend d
            WHERE d.classid = 'pg_type'::regclass
              AND d.objid = t.oid
              AND d.deptype = 'e')
        GROUP BY typtype
      )"},

      {"index_details", R"(
        SELECT indisunique, indisprimary, amname AS access_method, COUNT(*) AS count
        FROM pg_index i
        JOIN pg_class c ON i.indexrelid = c.oid
        JOIN pg_am a ON c.relam = a.oid
        JOIN pg_namespace n ON c.relnamespace = n.oid
        WHERE n.nspname !~ '^pg_' AND n.nspname <> 'information_schema'
        GROUP BY indisunique, indisprimary, amname
      )"},

      {"inheritance", R"(
        SELECT COUNT(*) AS count
        FROM pg_inherits i
        JOIN pg_class c ON i.inhrelid = c.oid
        JOIN pg_namespace n ON c.relnamespace = n.oid
        WHERE n.nspname !~ '^pg_' AND n.nspname <> 'information_schema'
      )"},

      {"defaults", R"(
        SELECT CASE
                 WHEN def ~ 'nextval\(' THEN 'sequence/serial'
                 WHEN def LIKE '''%' THEN 'literal constant'
                 WHEN def ~ '^[0-9]' THEN 'numeric constant'
                 WHEN def IN ('true', 'false') THEN 'boolean constant'
                 WHEN def ~ 'now\(' OR def ~ '^CURRENT_'
                      OR def ~ 'clock_timestamp\(' THEN 'timestamp function'
                 WHEN def ~ 'gen_random_uuid\('
                      OR def ~ 'uuid_generate_' THEN 'uuid generation'
                 ELSE 'other/expression'
               END AS default_type,
               COUNT(*) AS count
        FROM (
          SELECT pg_get_expr(d.adbin, d.adrelid) AS def
          FROM pg_attrdef d
          JOIN pg_class c ON c.oid = d.adrelid
          WHERE c.relnamespace NOT IN (
            SELECT oid FROM pg_namespace
            WHERE nspname ~ '^pg_' OR nspname = 'information_schema')
        ) sub
        GROUP BY default_type
        ORDER BY count DESC
      )"},

      // Skip `_RETURN` rules - PostgreSQL adds one per view, not real user rules.
      {"rules", R"(
        SELECT COUNT(*) AS count
        FROM pg_rewrite r
        JOIN pg_class c ON r.ev_class = c.oid
        JOIN pg_namespace n ON c.relnamespace = n.oid
        WHERE n.nspname !~ '^pg_' AND n.nspname <> 'information_schema'
          AND r.rulename <> '_RETURN'
      )"},

      {"aggregates", R"(
        SELECT COUNT(*) AS count
        FROM pg_aggregate a
        JOIN pg_proc p ON a.aggfnoid = p.oid
        JOIN pg_namespace n ON p.pronamespace = n.oid
        WHERE n.nspname !~ '^pg_' AND n.nspname <> 'information_schema'
      )"},

      {"policies", R"(
        SELECT COUNT(*) AS count
        FROM pg_policy pol
        JOIN pg_class c ON pol.polrelid = c.oid
        JOIN pg_namespace n ON c.relnamespace = n.oid
        WHERE n.nspname !~ '^pg_' AND n.nspname <> 'information_schema'
      )"},

      {"extended_stats", R"(
        SELECT unnest(stxkind) AS stat_kind, COUNT(*) AS count
        FROM pg_statistic_ext
        GROUP BY stat_kind
      )"},

      {"publications", R"(
        SELECT puballtables, pubinsert, pubupdate, pubdelete, COUNT(*) AS count
        FROM pg_publication
        GROUP BY puballtables, pubinsert, pubupdate, pubdelete
      )"},

      {"publication_tables", R"(
        SELECT COUNT(*) AS count FROM pg_publication_rel
      )"},

      {"sequences", R"(
        SELECT s.seqcache AS cache_size, s.seqcycle AS is_cyclic, COUNT(*) AS count
        FROM pg_sequence s
        JOIN pg_class c ON c.oid = s.seqrelid
        JOIN pg_namespace n ON c.relnamespace = n.oid
        WHERE n.nspname !~ '^pg_' AND n.nspname <> 'information_schema'
        GROUP BY s.seqcache, s.seqcycle
      )"},

      // YB has limited collation support; usually returns empty.
      {"collations", R"(
        SELECT a.atttypid::regtype AS type, co.collname AS collation, COUNT(*) AS count
        FROM pg_attribute a
        JOIN pg_class c ON a.attrelid = c.oid
        JOIN pg_namespace n ON c.relnamespace = n.oid
        JOIN pg_collation co ON a.attcollation = co.oid
        WHERE a.attnum > 0 AND NOT a.attisdropped
          AND n.nspname !~ '^pg_' AND n.nspname <> 'information_schema'
          AND a.attcollation != 0
          AND co.collname != 'default'
        GROUP BY a.atttypid::regtype, co.collname
        ORDER BY count DESC
      )"},

      {"foreign_tables", R"(
        SELECT COUNT(*) AS count
        FROM pg_foreign_table ft
        JOIN pg_class c ON ft.ftrelid = c.oid
        JOIN pg_namespace n ON c.relnamespace = n.oid
        WHERE n.nspname !~ '^pg_' AND n.nspname <> 'information_schema'
      )"},

      {"table_storage_params", R"(
        SELECT unnest(reloptions) AS option, COUNT(*) AS count
        FROM pg_class c
        JOIN pg_namespace n ON c.relnamespace = n.oid
        WHERE reloptions IS NOT NULL
          AND n.nspname !~ '^pg_' AND n.nspname <> 'information_schema'
        GROUP BY option
      )"},

      {"db_settings", R"(
        SELECT split_part(unnest(setconfig), '=', 1) AS setting_name, COUNT(*) AS count
        FROM pg_db_role_setting
        WHERE setdatabase = (SELECT oid FROM pg_database WHERE datname = current_database())
          AND setrole = 0
        GROUP BY setting_name
        ORDER BY count DESC
      )"},
  };
};

struct YsqlNodeQueries {
  static constexpr YsqlCallHomeQuery kNodeLevel[] = {
      {"connections", R"(
        SELECT COUNT(*) AS total,
               COUNT(*) FILTER (WHERE state = 'active') AS active,
               COUNT(*) FILTER (WHERE state = 'idle') AS idle,
               COUNT(*) FILTER (WHERE state = 'idle in transaction') AS idle_in_transaction,
               (SELECT setting::int FROM pg_settings WHERE name = 'max_connections')
                 AS max_connections
        FROM pg_stat_activity
        WHERE datname IS NOT NULL
      )"},

      {"long_running_queries", R"(
        SELECT COUNT(*) AS count
        FROM pg_stat_activity
        WHERE state = 'active'
          AND now() - query_start > interval '30 seconds'
      )"},
  };
};

class YsqlCollectionThrottle {
 public:
  bool ShouldCollect();

 private:
  CoarseTimePoint last_collection_time_;
};

std::string BuildStatsJson(
    TabletServerIf* server, const std::vector<std::string>& databases,
    std::span<const YsqlCallHomeQuery> aggregate_queries,
    std::span<const YsqlCallHomeQuery> per_db_queries);

Result<std::string> CollectYsqlClusterStatsJson(TabletServerIf* server);

}  // namespace tserver
}  // namespace yb
