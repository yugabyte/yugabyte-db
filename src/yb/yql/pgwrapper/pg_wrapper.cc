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

#include "yb/yql/pgwrapper/pg_wrapper.h"

#include <signal.h>
#include <sys/ipc.h>
#include <sys/shm.h>

#include <fstream>
#include <random>
#include <regex>
#include <string>
#include <thread>
#include <vector>

#include <boost/algorithm/string.hpp>

#include "yb/tserver/tablet_server_interface.h"

#include "yb/rpc/secure_stream.h"

#include "yb/util/debug/sanitizer_scopes.h"
#include "yb/util/env_util.h"
#include "yb/util/errno.h"
#include "yb/util/flags.h"
#include "yb/util/logging.h"
#include "yb/util/net/net_util.h"
#include "yb/util/path_util.h"
#include "yb/util/pg_util.h"
#include "yb/util/result.h"
#include "yb/util/scope_exit.h"
#include "yb/util/status.h"
#include "yb/util/status_format.h"
#include "yb/util/status_log.h"
#include "yb/util/stol_utils.h"
#include "yb/util/string_util.h"
#include "yb/util/subprocess.h"
#include "yb/util/thread.h"
#include "yb/util/to_stream.h"

#include "yb/yql/ysql_conn_mgr_wrapper/ysql_conn_mgr_stats.h"

#include "ybgate/ybgate_api.h"
#include "ybgate/ybgate_cpp_util.h"

DECLARE_bool(enable_ysql_conn_mgr);

DEPRECATE_FLAG(string, pg_proxy_bind_address, "02_2024");

DEFINE_UNKNOWN_string(postmaster_cgroup, "", "cgroup to add postmaster process to");
DEFINE_UNKNOWN_bool(pg_transactions_enabled, true,
            "True to enable transactions in YugaByte PostgreSQL API.");
DEFINE_NON_RUNTIME_string(yb_backend_oom_score_adj, "900",
              "oom_score_adj of postgres backends in linux environments");
DEFINE_NON_RUNTIME_string(yb_webserver_oom_score_adj, "900",
              "oom_score_adj of YSQL webserver in linux environments");
DEFINE_NON_RUNTIME_bool(yb_pg_terminate_child_backend, false,
            "Terminate other active server processes when a backend is killed");
DEFINE_UNKNOWN_bool(pg_verbose_error_log, false,
            "True to enable verbose logging of errors in PostgreSQL server");
DEFINE_UNKNOWN_int32(pgsql_proxy_webserver_port, 13000, "Webserver port for PGSQL");
DEFINE_NON_RUNTIME_bool(yb_enable_valgrind, false,
            "True to run postgres under Valgrind. Must compile with --no-tcmalloc");

DEFINE_test_flag(bool, pg_collation_enabled, true,
                 "True to enable collation support in YugaByte PostgreSQL.");
// Default to 5MB
DEFINE_UNKNOWN_string(
    pg_mem_tracker_tcmalloc_gc_release_bytes, std::to_string(5 * 1024 * 1024),
    "Overriding the gflag mem_tracker_tcmalloc_gc_release_bytes "
    "defined in mem_tracker.cc. The overriding value is specifically "
    "set for Postgres backends");

DEFINE_RUNTIME_string(pg_mem_tracker_update_consumption_interval_us, std::to_string(50 * 1000),
    "Interval that is used to update memory consumption from external source. "
    "For instance from tcmalloc statistics. This interval is for Postgres backends only");

DECLARE_string(metric_node_name);
TAG_FLAG(pg_transactions_enabled, advanced);
TAG_FLAG(pg_transactions_enabled, hidden);

DEFINE_UNKNOWN_bool(pg_stat_statements_enabled, true,
            "True to enable statement stats in PostgreSQL server");
TAG_FLAG(pg_stat_statements_enabled, advanced);
TAG_FLAG(pg_stat_statements_enabled, hidden);

// Top-level postgres configuration flags.
DEFINE_UNKNOWN_bool(ysql_enable_auth, false,
              "True to enforce password authentication for all connections");

// Catch-all postgres configuration flags.
DEFINE_UNKNOWN_string(ysql_pg_conf_csv, "",
              "CSV formatted line represented list of postgres setting assignments");
DEFINE_UNKNOWN_string(ysql_hba_conf_csv, "",
              "CSV formatted line represented list of postgres hba rules (in order)");
TAG_FLAG(ysql_hba_conf_csv, sensitive_info);
DEFINE_NON_RUNTIME_string(ysql_ident_conf_csv, "",
              "CSV formatted line represented list of postgres ident map rules (in order)");

DEFINE_UNKNOWN_string(ysql_pg_conf, "",
              "Deprecated, use the `ysql_pg_conf_csv` flag instead. " \
              "Comma separated list of postgres setting assignments");
DEFINE_UNKNOWN_string(ysql_hba_conf, "",
              "Deprecated, use `ysql_hba_conf_csv` flag instead. " \
              "Comma separated list of postgres hba rules (in order)");
TAG_FLAG(ysql_hba_conf, sensitive_info);
DECLARE_string(tmp_dir);

DEFINE_RUNTIME_PG_FLAG(string, timezone, "",
    "Overrides the default ysql timezone for displaying and interpreting timestamps. If no value "
    "is provided, Postgres will determine one based on the environment");

DEFINE_RUNTIME_PG_FLAG(string, datestyle,
    "ISO, MDY", "The ysql display format for date and time values");

DEFINE_NON_RUNTIME_PG_FLAG(int32, max_connections, 0,
    "Overrides the maximum number of concurrent ysql connections. If set to 0, Postgres will "
    "dynamically determine a platform-specific value");

DEFINE_RUNTIME_PG_FLAG(string, default_transaction_isolation,
    "read committed", "The ysql transaction isolation level");

DEFINE_RUNTIME_PG_FLAG(string, log_statement,
    "none", "Sets which types of ysql statements should be logged");

DEFINE_RUNTIME_PG_FLAG(string, log_min_messages,
    "warning", "Sets the lowest ysql message level to log");

DEFINE_RUNTIME_PG_FLAG(int32, log_min_duration_statement, -1,
    "Sets the duration of each completed ysql statement to be logged if the statement ran for at "
    "least the specified number of milliseconds. Zero prints all queries. -1 turns this feature "
    "off.");

DEFINE_RUNTIME_PG_FLAG(bool, yb_enable_memory_tracking, true,
    "Enables tracking of memory consumption of the PostgreSQL process. This enhances garbage "
    "collection behaviour and memory usage observability.");

DEFINE_RUNTIME_AUTO_PG_FLAG(bool, yb_enable_expression_pushdown, kLocalVolatile, false, true,
    "Push supported expressions from ysql down to DocDB for evaluation.");

DEFINE_RUNTIME_AUTO_PG_FLAG(bool, yb_enable_index_aggregate_pushdown, kLocalVolatile, false, true,
    "Push supported aggregates from ysql down to DocDB for evaluation. Affects IndexScan only.");

DEFINE_RUNTIME_AUTO_PG_FLAG(bool, yb_pushdown_strict_inequality, kLocalVolatile, false, true,
    "Push down strict inequality filters");

DEFINE_RUNTIME_AUTO_PG_FLAG(bool, yb_pushdown_is_not_null, kLocalVolatile, false, true,
    "Push down IS NOT NULL condition filters");

DEFINE_RUNTIME_AUTO_PG_FLAG(bool, yb_enable_hash_batch_in, kLocalVolatile, false, true,
    "Enable batching of hash in queries.");

DEFINE_RUNTIME_AUTO_PG_FLAG(bool, yb_bypass_cond_recheck, kLocalVolatile, false, true,
    "Bypass index condition recheck at the YSQL layer if the condition was pushed down.");

DEFINE_RUNTIME_AUTO_PG_FLAG(bool, yb_enable_pg_locks, kLocalVolatile, false, true,
    "Enable the pg_locks view. This view provides information about the locks held by "
    "active postgres sessions.");

DEFINE_RUNTIME_PG_FLAG(int32, yb_locks_min_txn_age, 1000,
    "Sets the minimum transaction age for results from pg_locks.");

DEFINE_RUNTIME_PG_FLAG(int32, yb_locks_max_transactions, 16,
    "Sets the maximum number of transactions for which to return rows in pg_locks.");

DEFINE_RUNTIME_PG_FLAG(int32, yb_locks_txn_locks_per_tablet, 200,
    "Sets the maximum number of rows to return per transaction per tablet in pg_locks.");

DEFINE_RUNTIME_PG_FLAG(int32, yb_index_state_flags_update_delay, 0,
    "Delay in milliseconds between stages of online index build. For testing purposes.");

DEFINE_RUNTIME_PG_FLAG(int32, yb_wait_for_backends_catalog_version_timeout, 5 * 60 * 1000, // 5 min
    "Timeout in milliseconds to wait for backends to reach desired catalog versions. The actual"
    " time spent may be longer than that by as much as master flag"
    " wait_for_ysql_backends_catalog_version_client_master_rpc_timeout_ms. Setting to zero or less"
    " results in no timeout. Currently used by concurrent CREATE INDEX.");

DEFINE_RUNTIME_PG_FLAG(int32, yb_bnl_batch_size, 1024,
    "Batch size of nested loop joins.");

DEFINE_RUNTIME_PG_FLAG(int32, yb_explicit_row_locking_batch_size, 1,
    "Batch size of explicit row locking.");

DEFINE_RUNTIME_PG_FLAG(string, yb_xcluster_consistency_level, "database",
    "Controls the consistency level of xCluster replicated databases. Valid values are "
    "\"database\" and \"tablet\".");

DEFINE_RUNTIME_PG_FLAG(string, yb_test_block_index_phase, "",
    "Block the given index phase from proceeding. Valid names are indisready, build,"
    " indisvalid and finish. For testing purposes.");

DEFINE_RUNTIME_AUTO_PG_FLAG(bool, yb_enable_sequence_pushdown, kLocalVolatile, false, true,
    "Allow nextval() to fetch the value range and advance the sequence value "
    "in a single operation");

DEFINE_RUNTIME_PG_FLAG(bool, yb_disable_wait_for_backends_catalog_version, false,
    "Disable waiting for backends to have up-to-date pg_catalog. This could cause correctness"
    " issues, which could be mitigated by setting high ysql_yb_index_state_flags_update_delay."
    " Although it is runtime-settable, the effects won't take place for any in-progress"
    " queries.");

DEFINE_RUNTIME_AUTO_PG_FLAG(bool, yb_enable_add_column_missing_default, kExternal, false, true,
                            "Enable using the default value for existing rows after an ADD COLUMN"
                            " ... DEFAULT operation");

DEFINE_RUNTIME_AUTO_PG_FLAG(bool, yb_enable_alter_table_rewrite, kLocalPersisted, false, true,
                            "Enable ALTER TABLE rewrite operations");

DEFINE_RUNTIME_PG_PREVIEW_FLAG(bool, yb_enable_optimizer_statistics, false,
    "Enables use of the PostgreSQL selectivity estimation which utilizes table statistics "
    "collected with ANALYZE. When disabled, a simpler heuristics based selectivity estimation is "
    "used.");

DEFINE_RUNTIME_PG_PREVIEW_FLAG(bool, yb_enable_base_scans_cost_model, false,
    "Enable cost model enhancements");

DEFINE_RUNTIME_PG_FLAG(uint64, yb_fetch_row_limit, 1024,
    "Maximum number of rows to fetch per scan.");

DEFINE_RUNTIME_PG_FLAG(uint64, yb_fetch_size_limit, 0,
    "Maximum size of a fetch response.");

DEFINE_NON_RUNTIME_bool(enable_ysql_conn_mgr_stats, true,
  "Enable stats collection from Ysql Connection Manager. These stats will be "
  "displayed at the endpoint '<ip_address_of_cluster>:13000/connections'");

DEFINE_RUNTIME_AUTO_PG_FLAG(bool, yb_enable_saop_pushdown, kLocalVolatile, false, true,
    "Push supported scalar array operations from ysql down to DocDB for evaluation.");

// TODO(#19211): Convert this to an auto-flag.
DEFINE_RUNTIME_PG_PREVIEW_FLAG(bool, yb_enable_replication_commands, false,
    "Enable logical replication commands for Publication and Replication Slots");

DEFINE_RUNTIME_PG_PREVIEW_FLAG(bool, yb_enable_replica_identity, false,
    "Enable replica identity command for Alter Table query");

DEFINE_RUNTIME_PG_FLAG(
    string, yb_default_replica_identity, "CHANGE",
    "The default replica identity to be assigned to user defined tables at the time of creation. "
    "The flag is case sensitive and can take four possible values, 'FULL', 'DEFAULT', 'NOTHING' "
    "and 'CHANGE'. If any value other than these is assigned to the flag, the replica identity "
    "CHANGE will be used as default at the time of table creation.");

DEFINE_RUNTIME_PG_PREVIEW_FLAG(int32, yb_parallel_range_rows, 0,
    "The number of rows to plan per parallel worker, zero disables the feature");

DEFINE_RUNTIME_PG_FLAG(uint32, yb_walsender_poll_sleep_duration_nonempty_ms, 1,  // 1 msec
    "Time in milliseconds for which Walsender waits before fetching the next batch of changes from "
    "the CDC service in case the last received response was non-empty.");

DEFINE_RUNTIME_PG_FLAG(uint32, yb_walsender_poll_sleep_duration_empty_ms, 1 * 1000,  // 1 sec
    "Time in milliseconds for which Walsender waits before fetching the next batch of changes from "
    "the CDC service in case the last received response was empty. The response can be empty in "
    "case there are no DMLs happening in the system.");

DEFINE_RUNTIME_PG_FLAG(int32, yb_toast_catcache_threshold, -1,
    "Size threshold in bytes for a catcache tuple to be compressed.");

DEFINE_RUNTIME_PG_FLAG(string, yb_read_after_commit_visibility, "strict",
  "Determines the behavior of read-after-commit-visibility guarantee.");

static bool ValidateXclusterConsistencyLevel(const char* flagname, const std::string& value) {
  if (value != "database" && value != "tablet") {
    fprintf(
        stderr, "Invalid value for --%s: %s, must be 'database' or 'tablet'\n", flagname,
        value.c_str());
    return false;
  }
  return true;
}

DEFINE_validator(ysql_yb_xcluster_consistency_level, &ValidateXclusterConsistencyLevel);

DEFINE_NON_RUNTIME_string(ysql_conn_mgr_warmup_db, "yugabyte",
    "Database for which warmup needs to be done.");

DEFINE_NON_RUNTIME_PG_FLAG(int32, yb_ash_circular_buffer_size, 16 * 1024,
    "Size (in KiBs) of ASH circular buffer that stores the samples");

DEFINE_RUNTIME_PG_FLAG(int32, yb_ash_sampling_interval_ms, 1000,
    "Time (in milliseconds) between two consecutive sampling events");
DEPRECATE_FLAG(int32, ysql_yb_ash_sampling_interval, "2024_03");

DEFINE_RUNTIME_PG_FLAG(int32, yb_ash_sample_size, 500,
    "Number of samples captured from each component per sampling event");

DEFINE_NON_RUNTIME_string(ysql_cron_database_name, "yugabyte",
    "Database in which pg_cron metadata is kept.");

DECLARE_bool(enable_pg_cron);

using gflags::CommandLineFlagInfo;
using std::string;
using std::vector;

using namespace std::literals;  // NOLINT

namespace yb {
namespace pgwrapper {

namespace {

Status WriteConfigFile(const string& path, const vector<string>& lines) {
  std::ofstream conf_file;
  conf_file.open(path, std::ios_base::out | std::ios_base::trunc);
  if (!conf_file) {
    return STATUS_FORMAT(
        IOError,
        "Failed to write ysql config file '%s': errno=$0: $1",
        path,
        errno,
        ErrnoToString(errno));
  }

  conf_file << "# This is an autogenerated file, do not edit manually!" << std::endl;
  for (const auto& line : lines) {
    conf_file << line << std::endl;
  }

  conf_file.close();

  return Status::OK();
}

void ReadCommaSeparatedValues(const string& src, vector<string>* lines) {
  vector<string> new_lines;
  boost::split(new_lines, src, boost::is_any_of(","));
  lines->insert(lines->end(), new_lines.begin(), new_lines.end());
}

void MergeSharedPreloadLibraries(const string& src, vector<string>* defaults) {
  string copy = boost::replace_all_copy(src, " ", "");
  copy = boost::erase_first_copy(copy, "shared_preload_libraries");
  // According to the documentation in postgresql.conf file,
  // the '=' is optional hence it needs to be handled separately.
  copy = boost::erase_first_copy(copy, "=");
  copy = boost::trim_copy_if(copy, boost::is_any_of("'\""));
  vector<string> new_items;
  boost::split(new_items, copy, boost::is_any_of(","));
  // Remove empty elements, makes it safe to use with empty user
  // provided shared_preload_libraries, for example,
  // if the value was provided via environment variable, example:
  //
  //   --ysql_pg_conf="shared_preload_libraries='$UNSET_VALUE'"
  //
  // Alternative example:
  //
  //   --ysql_pg_conf="shared_preload_libraries='$LIB1,$LIB2,$LIB3'"
  // where any of the libs could be undefined.
  new_items.erase(
    std::remove_if(new_items.begin(),
      new_items.end(),
      [](const std::string& s){return s.empty();}),
      new_items.end());
  defaults->insert(defaults->end(), new_items.begin(), new_items.end());
}

Status ReadCSVValues(const string& csv, vector<string>* lines) {
  // Function reads CSV string in the following format:
  // - fields are divided with comma (,)
  // - fields with comma (,) or double-quote (") are quoted with double-quote (")
  // - pair of double-quote ("") in quoted field represents single double-quote (")
  //
  // Examples:
  // 1,"two, 2","three ""3""", four , -> ['1', 'two, 2', 'three "3"', ' four ', '']
  // 1,"two                           -> Malformed CSV (quoted field 'two' is not closed)
  // 1, "two"                         -> Malformed CSV (quoted field 'two' has leading spaces)
  // 1,two "2"                        -> Malformed CSV (field with " must be quoted)
  // 1,"tw"o"                         -> Malformed CSV (no separator after quoted field 'tw')

  const std::regex exp(R"(^(?:([^,"]+)|(?:"((?:[^"]|(?:""))*)\"))(?:(?:,)|(?:$)))");
  auto i = csv.begin();
  const auto end = csv.end();
  std::smatch match;
  while (i != end && std::regex_search(i, end, match, exp)) {
    // Replace pair of double-quote ("") with single double-quote (") in quoted field.
    if (match[2].length() > 0) {
      lines->emplace_back(match[2].first, match[2].second);
      boost::algorithm::replace_all(lines->back(), "\"\"", "\"");
    } else {
      lines->emplace_back(match[1].first, match[1].second);
    }
    i += match.length();
  }
  SCHECK(i == end, InvalidArgument, Format("Malformed CSV '$0'", csv));
  if (!csv.empty() && csv.back() == ',') {
    lines->emplace_back();
  }
  return Status::OK();
}

namespace {
// Append any Pg gFlag with non default value, or non-promoted AutoFlag
void AppendPgGFlags(vector<string>* lines) {
  vector<CommandLineFlagInfo> flags;
  GetAllFlags(&flags);

  for (const CommandLineFlagInfo& flag : flags) {
    std::unordered_set<FlagTag> tags;
    GetFlagTags(flag.name, &tags);
    if (!tags.contains(FlagTag::kPg)) {
      continue;
    }

    // Skip flags that do not have a custom override
    if (flag.is_default) {
      if (!tags.contains(FlagTag::kAuto)) {
        continue;
      }

      // AutoFlags in not-promoted state will be set to their initial value.
      // In the promoted state they will be set to their target value. (guc default)
      // We only need to override when AutoFlags is non-promoted.
      auto* desc = GetAutoFlagDescription(flag.name);
      CHECK_NOTNULL(desc);
      if (IsFlagPromoted(flag, *desc)) {
        continue;
      }
    }

    const string pg_flag_prefix = "ysql_";
    if (!flag.name.starts_with(pg_flag_prefix)) {
      LOG(DFATAL) << "Flags with Pg Flag tag should have 'ysql_' prefix. Flag_name: " << flag.name;
      continue;
    }

    string pg_variable_name = flag.name.substr(pg_flag_prefix.length());
    lines->push_back(Format("$0=$1", pg_variable_name, flag.current_value));
  }
}
}  // namespace

Result<string> WritePostgresConfig(const PgProcessConf& conf) {
  // First add default configuration created by local initdb.
  string default_conf_path = JoinPathSegments(conf.data_dir, "postgresql.conf");
  std::ifstream conf_file;
  conf_file.open(default_conf_path, std::ios_base::in);
  if (!conf_file) {
    return STATUS_FORMAT(
        IOError,
        "Failed to read default postgres configuration '$0': errno=$1: $2",
        default_conf_path,
        errno,
        ErrnoToString(errno));
  }

  // Gather the default extensions:
  vector<string> metricsLibs;
  if (FLAGS_pg_stat_statements_enabled) {
    metricsLibs.push_back("pg_stat_statements");
  }
  metricsLibs.push_back("yb_pg_metrics");
  metricsLibs.push_back("pgaudit");
  metricsLibs.push_back("pg_hint_plan");

  if (FLAGS_enable_pg_cron) {
    metricsLibs.push_back("pg_cron");
  }

  vector<string> lines;
  string line;
  while (std::getline(conf_file, line)) {
    lines.push_back(line);
  }
  conf_file.close();

  vector<string> user_configs;
  if (!FLAGS_ysql_pg_conf_csv.empty()) {
    RETURN_NOT_OK(ReadCSVValues(FLAGS_ysql_pg_conf_csv, &user_configs));
  } else if (!FLAGS_ysql_pg_conf.empty()) {
    ReadCommaSeparatedValues(FLAGS_ysql_pg_conf, &user_configs);
  }

  // If the user has given any shared_preload_libraries, merge them in.
  for (string &value : user_configs) {
    if (boost::starts_with(value, "shared_preload_libraries")) {
      MergeSharedPreloadLibraries(value, &metricsLibs);
    } else {
      lines.push_back(value);
    }
  }

  // Add shared_preload_libraries to the ysql_pg.conf.
  lines.push_back(Format("shared_preload_libraries='$0'", boost::join(metricsLibs, ",")));

  if (conf.enable_tls) {
    lines.push_back("ssl=on");
    lines.push_back(Format("ssl_cert_file='$0/node.$1.crt'",
                           conf.certs_for_client_dir,
                           conf.cert_base_name));
    lines.push_back(Format("ssl_key_file='$0/node.$1.key'",
                           conf.certs_for_client_dir,
                           conf.cert_base_name));
    lines.push_back(Format("ssl_ca_file='$0/ca.crt'", conf.certs_for_client_dir));
  }

  // Add cron.database_name
  lines.push_back(Format("cron.database_name='$0'", FLAGS_ysql_cron_database_name));

  // Finally add gFlags.
  // If the file contains multiple entries for the same parameter, all but the last one are
  // ignored. If there are duplicates in FLAGS_ysql_pg_conf_csv then we want the values specified
  // via the gFlag to take precedence.
  AppendPgGFlags(&lines);

  string conf_path = JoinPathSegments(conf.data_dir, "ysql_pg.conf");
  RETURN_NOT_OK(WriteConfigFile(conf_path, lines));
  return "config_file=" + conf_path;
}

Result<string> WritePgHbaConfig(const PgProcessConf& conf) {
  vector<string> lines;

  // Add the user-defined custom configuration lines if any.
  // Put this first so that it can be used to override the auto-generated config below.
  if (!FLAGS_ysql_hba_conf_csv.empty()) {
    RETURN_NOT_OK(ReadCSVValues(FLAGS_ysql_hba_conf_csv, &lines));
  } else if (!FLAGS_ysql_hba_conf.empty()) {
    ReadCommaSeparatedValues(FLAGS_ysql_hba_conf, &lines);
  }
  // Add auto-generated config for the enable auth and enable_tls flags.
  if (FLAGS_ysql_enable_auth || conf.enable_tls) {
    const auto host_type =  conf.enable_tls ? "hostssl" : "host";
    const auto auth_method = FLAGS_ysql_enable_auth ? "md5" : "trust";
    lines.push_back(Format("$0 all all all $1", host_type, auth_method));
  }

  // Enforce a default hba configuration so users don't lock themselves out.
  if (lines.empty()) {
    LOG(WARNING) << "No hba configuration lines found, defaulting to trust all configuration.";
    lines.push_back("host all all all trust");
  }

  // Add comments to the hba config file noting the internally hardcoded config line.
  lines.insert(lines.begin(), {
      "# Internal configuration:",
      "# local all postgres yb-tserver-key",
  });

  const auto conf_path = JoinPathSegments(conf.data_dir, "ysql_hba.conf");
  RETURN_NOT_OK(WriteConfigFile(conf_path, lines));
  return "hba_file=" + conf_path;
}

Result<string> WritePgIdentConfig(const PgProcessConf& conf) {
  vector<string> lines;

  // Add the user-defined custom configuration lines if any.
  if (!FLAGS_ysql_ident_conf_csv.empty()) {
    RETURN_NOT_OK(ReadCSVValues(FLAGS_ysql_ident_conf_csv, &lines));
  }

  if (lines.empty()) {
    LOG(INFO) << "No user name mapping configuration lines found.";
  }

  // Add comments to the ident config file noting the record structure.
  lines.insert(lines.begin(), {
      "# MAPNAME IDP-USERNAME YB-USERNAME"
  });

  const auto conf_path = JoinPathSegments(conf.data_dir, "ysql_ident.conf");
  RETURN_NOT_OK(WriteConfigFile(conf_path, lines));
  return "ident_file=" + conf_path;
}

Result<vector<string>> WritePgConfigFiles(const PgProcessConf& conf) {
  vector<string> args;
  args.push_back("-c");
  args.push_back(VERIFY_RESULT_PREPEND(WritePostgresConfig(conf),
      "Failed to write ysql pg configuration: "));
  args.push_back("-c");
  args.push_back(VERIFY_RESULT_PREPEND(WritePgHbaConfig(conf),
      "Failed to write ysql hba configuration: "));
  args.push_back("-c");
  args.push_back(VERIFY_RESULT_PREPEND(WritePgIdentConfig(conf),
      "Failed to write ysql ident configuration: "));
  return args;
}

}  // namespace

string GetPostgresInstallRoot() {
  return JoinPathSegments(yb::env_util::GetRootDir("postgres"), "postgres");
}

Result<PgProcessConf> PgProcessConf::CreateValidateAndRunInitDb(
    const std::string& bind_addresses,
    const std::string& data_dir,
    const int tserver_shm_fd) {
  PgProcessConf conf;
  if (!bind_addresses.empty()) {
    auto pg_host_port = VERIFY_RESULT(HostPort::FromString(
        bind_addresses, PgProcessConf::kDefaultPort));
    conf.listen_addresses = pg_host_port.host();
    conf.pg_port = pg_host_port.port();
  }
  conf.data_dir = data_dir;
  conf.tserver_shm_fd = tserver_shm_fd;
  PgWrapper pg_wrapper(conf);
  RETURN_NOT_OK(pg_wrapper.PreflightCheck());
  RETURN_NOT_OK(pg_wrapper.InitDbLocalOnlyIfNeeded());
  return conf;
}

// ------------------------------------------------------------------------------------------------
// PgWrapper: managing one instance of a PostgreSQL child process
// ------------------------------------------------------------------------------------------------

PgWrapper::PgWrapper(PgProcessConf conf)
    : conf_(std::move(conf)) {
}

Status PgWrapper::PreflightCheck() {
  RETURN_NOT_OK(CheckExecutableValid(GetPostgresExecutablePath()));
  RETURN_NOT_OK(CheckExecutableValid(GetInitDbExecutablePath()));
  return Status::OK();
}

Status PgWrapper::Start() {
  auto postgres_executable = GetPostgresExecutablePath();
  RETURN_NOT_OK(CheckExecutableValid(postgres_executable));

  bool log_to_file = !FLAGS_logtostderr && !FLAGS_log_dir.empty() && !conf_.force_disable_log_file;
  VLOG(1) << "Deciding whether the child postgres process should to file: "
          << YB_EXPR_TO_STREAM_COMMA_SEPARATED(
              FLAGS_logtostderr,
              FLAGS_log_dir.empty(),
              conf_.force_disable_log_file,
              log_to_file);

  vector<string> argv {};

#ifdef YB_VALGRIND_PATH
  if (FLAGS_yb_enable_valgrind) {
    vector<string> valgrind_argv {
      AS_STRING(YB_VALGRIND_PATH),
      "--leak-check=no",
      "--gen-suppressions=all",
      "--suppressions=" + GetPostgresSuppressionsPath(),
      "--time-stamp=yes",
      "--track-origins=yes",
      "--error-markers=VALGRINDERROR_BEGIN,VALGRINDERROR_END",
      "--trace-children=yes",
    };

    argv.insert(argv.end(), valgrind_argv.begin(), valgrind_argv.end());

    if (log_to_file) {
      argv.push_back("--log-file=" + FLAGS_log_dir + "/valgrind_postgres_check_%p.log");
    }
  }
#else
  if (FLAGS_yb_enable_valgrind) {
    LOG(ERROR) << "yb_enable_valgrind is ON, but Yugabyte was not compiled with Valgrind support.";
  }
#endif

  vector<string> postgres_argv {
    postgres_executable,
    "-D", conf_.data_dir,
    "-p", std::to_string(conf_.pg_port),
    "-h", conf_.listen_addresses,
  };

  argv.insert(argv.end(), postgres_argv.begin(), postgres_argv.end());

  // Configure UNIX domain socket for index backfill tserver-postgres communication and for
  // Yugabyte Platform backups.
  argv.push_back("-k");
  const std::string& socket_dir = PgDeriveSocketDir(
      HostPort(conf_.listen_addresses, conf_.pg_port));
  RETURN_NOT_OK(Env::Default()->CreateDirs(socket_dir));
  argv.push_back(socket_dir);

  // Also tighten permissions on the socket.
  argv.push_back("-c");
  argv.push_back("unix_socket_permissions=0700");

  if (log_to_file) {
    argv.push_back("-c");
    argv.push_back("logging_collector=on");
    // FLAGS_log_dir should already be set by tserver during startup.
    argv.push_back("-c");
    argv.push_back("log_directory=" + FLAGS_log_dir);
  }

  argv.push_back("-c");
  argv.push_back("yb_pg_metrics.node_name=" + FLAGS_metric_node_name);
  argv.push_back("-c");
  argv.push_back("yb_pg_metrics.port=" + std::to_string(FLAGS_pgsql_proxy_webserver_port));

  auto config_file_args = CHECK_RESULT(WritePgConfigFiles(conf_));
  argv.insert(argv.end(), config_file_args.begin(), config_file_args.end());

  if (FLAGS_pg_verbose_error_log) {
    argv.push_back("-c");
    argv.push_back("log_error_verbosity=VERBOSE");
  }

  proc_.emplace(argv[0], argv);

  vector<string> ld_library_path {
    GetPostgresLibPath(),
    GetPostgresThirdPartyLibPath()
  };
  proc_->SetEnv("LD_LIBRARY_PATH", boost::join(ld_library_path, ":"));
  std::string stats_key = std::to_string(ysql_conn_mgr_stats_shmem_key_);

  unsetenv(YSQL_CONN_MGR_SHMEM_KEY_ENV_NAME);
  if (FLAGS_enable_ysql_conn_mgr_stats)
    proc_->SetEnv(YSQL_CONN_MGR_SHMEM_KEY_ENV_NAME, stats_key);

  proc_->ShareParentStderr();
  proc_->ShareParentStdout();
  proc_->SetEnv("FLAGS_yb_pg_terminate_child_backend",
                FLAGS_yb_pg_terminate_child_backend ? "true" : "false");
  proc_->SetEnv("FLAGS_yb_backend_oom_score_adj", FLAGS_yb_backend_oom_score_adj);
  proc_->SetEnv("FLAGS_yb_webserver_oom_score_adj", FLAGS_yb_webserver_oom_score_adj);

  // Pass down custom temp path through environment variable.
  if (!VERIFY_RESULT(Env::Default()->DoesDirectoryExist(FLAGS_tmp_dir))) {
    return STATUS_FORMAT(IOError, "Directory $0 does not exist", FLAGS_tmp_dir);
  }
  proc_->SetEnv("FLAGS_tmp_dir", FLAGS_tmp_dir);

  // See YBSetParentDeathSignal in pg_yb_utils.c for how this is used.
  proc_->SetEnv("YB_PG_PDEATHSIG", Format("$0", SIGINT));
  proc_->InheritNonstandardFd(conf_.tserver_shm_fd);
  SetCommonEnv(&*proc_, /* yb_enabled */ true);

  proc_->SetEnv("FLAGS_mem_tracker_tcmalloc_gc_release_bytes",
                FLAGS_pg_mem_tracker_tcmalloc_gc_release_bytes);
  proc_->SetEnv("FLAGS_mem_tracker_update_consumption_interval_us",
                FLAGS_pg_mem_tracker_update_consumption_interval_us);

  proc_->SetEnv("YB_ALLOW_CLIENT_SET_TSERVER_KEY_AUTH",
      FLAGS_enable_ysql_conn_mgr ? "1" : "0");

  rpc::SetOpenSSLEnv(&*proc_);

  RETURN_NOT_OK(proc_->Start());
  if (!FLAGS_postmaster_cgroup.empty()) {
    std::string path = FLAGS_postmaster_cgroup + "/cgroup.procs";
    proc_->AddPIDToCGroup(path, proc_->pid());
  }
  LOG(INFO) << "PostgreSQL server running as pid " << proc_->pid();
  return Status::OK();
}

Status PgWrapper::SetYsqlConnManagerStatsShmKey(key_t key) {
  ysql_conn_mgr_stats_shmem_key_ = key;
  if (key == -1)
    return STATUS(
        InternalError,
        "Unable to create shared memory segment for sharing the stats for Ysql Connection "
        "Manager.");

  return Status::OK();
}

Status PgWrapper::ReloadConfig() {
  return proc_->Kill(SIGHUP);
}

Status PgWrapper::UpdateAndReloadConfig() {
  VERIFY_RESULT(WritePostgresConfig(conf_));
  return ReloadConfig();
}

Status PgWrapper::InitDb(const string& versioned_data_dir) {
  const string initdb_program_path = GetInitDbExecutablePath();
  RETURN_NOT_OK(CheckExecutableValid(initdb_program_path));
  if (!Env::Default()->FileExists(initdb_program_path)) {
    return STATUS_FORMAT(IOError, "initdb not found at: $0", initdb_program_path);
  }

  // A set versioned_data_dir means it's local initdb, so we need to initialize in the actual
  // directory. Otherwise, we can use the symlink.
  const string& data_dir = versioned_data_dir.empty() ? conf_.data_dir : versioned_data_dir;
  vector<string> initdb_args { initdb_program_path, "-D", data_dir, "-U", "postgres" };
  LOG(INFO) << "Launching initdb: " << AsString(initdb_args);

  Subprocess initdb_subprocess(initdb_program_path, initdb_args);
  initdb_subprocess.InheritNonstandardFd(conf_.tserver_shm_fd);
  bool yb_enabled = versioned_data_dir.empty();
  SetCommonEnv(&initdb_subprocess, yb_enabled);
  int status = 0;
  RETURN_NOT_OK(initdb_subprocess.Start());
  RETURN_NOT_OK(initdb_subprocess.Wait(&status));
  if (status != 0) {
    SCHECK(WIFEXITED(status), InternalError,
           Format("$0 did not exit normally", initdb_program_path));
    return STATUS_FORMAT(RuntimeError, "$0 failed with exit code $1",
                         initdb_program_path,
                         WEXITSTATUS(status));
  }

  LOG(INFO) << "initdb completed successfully. Database initialized at " << conf_.data_dir;
  return Status::OK();
}

namespace {

constexpr auto kVersionChars = 2;

Result<int32_t> GetCurrentPgVersion() {
  const char* curr_pg_ver_cstr;
  PG_RETURN_NOT_OK(YbgGetPgVersion(&curr_pg_ver_cstr));
  string curr_pg_ver_str = curr_pg_ver_cstr;
  return CheckedStoi(curr_pg_ver_str.substr(0, kVersionChars));
}

Result<int32_t> GetPgDirectoryVersion(const string& data_dir) {
  std::unique_ptr<SequentialFile> result;
  std::string full_path = JoinPathSegments(data_dir, "PG_VERSION");
  RETURN_NOT_OK(Env::Default()->NewSequentialFile(full_path, &result));
  Slice slc;
  uint8_t buf[64];
  RETURN_NOT_OK(result->Read(ARRAYSIZE(buf), &slc, buf));
  // There's a trailing newline in the PG_VERSION file ("##\n").
  return CheckedStoi(slc.Prefix(kVersionChars));
}

}  // namespace

string PgWrapper::MakeVersionedDataDir(int32_t version) {
  return conf_.data_dir + "_" + std::to_string(version);
}

// The data directory contains PG files for a particular PG version.
// Across upgrades and rollbacks, other than briefly during initialization time, we always want a
// valid data directory that matches the current major PG version. Also, we would like to restore
// PG11's data in case of rollback. We do this by using a symlink to a version-specific data
// directory.
// This code is written to be identical for a tablet server hosting any major PG version.
Status PgWrapper::InitDbLocalOnlyIfNeeded() {
  int32_t current_pg_version = VERIFY_RESULT(GetCurrentPgVersion());

  // One-time migration in case this installation is not yet using a symlink
  if (VERIFY_RESULT(Env::Default()->DoesDirectoryExist(conf_.data_dir)) &&
      !VERIFY_RESULT(Env::Default()->IsSymlink(conf_.data_dir))) {
    int32_t directory_version = VERIFY_RESULT(GetPgDirectoryVersion(conf_.data_dir));
    string migrated_versioned_dir = MakeVersionedDataDir(directory_version);
    LOG(INFO) << "Data directory " << conf_.data_dir << " already exists for version "
              << directory_version << ", performing one-time migration to "
              << migrated_versioned_dir << ".";
    RETURN_NOT_OK(Env::Default()->RenameFile(conf_.data_dir, migrated_versioned_dir));
    if (current_pg_version == directory_version) {
      LOG(INFO) << "Linking " << conf_.data_dir << " to " << migrated_versioned_dir
                << " and skipping initdb.";
      return Env::Default()->SymlinkPath(migrated_versioned_dir, conf_.data_dir);
    }
  }

  string versioned_data_dir = MakeVersionedDataDir(current_pg_version);
  if (Env::Default()->FileExists(conf_.data_dir)) {
    // Get version from symlink
    string link = VERIFY_RESULT(Env::Default()->ReadLink(conf_.data_dir));
    SCHECK_GE(link.size(), kVersionChars, InternalError,
              Format("conf_.data_dir too short: $0 bytes", link.size()));
    int32_t symlink_version = VERIFY_RESULT(CheckedStoi(link.substr(link.size() - kVersionChars)));
    if (current_pg_version == symlink_version) {
      LOG(INFO) << "Data directory " << versioned_data_dir
                << " already exists, skipping initdb";
      return Status::OK();
    }
    if (current_pg_version < symlink_version) {
      // Looks like a rollback. If the directory exists, use it.
      if (Env::Default()->DirExists(versioned_data_dir)) {
        LOG(INFO) << "Data directory " << versioned_data_dir
                  << " already exists for rollback. Linking " << conf_.data_dir
                  << " and skipping initdb.";
        return Env::Default()->SymlinkPath(versioned_data_dir, conf_.data_dir);
      }
    }
  }

  // At this point it's either an upgrade, or no symlink exists. In the upgrade case, there may
  // have been a prior rollback, and we're trying again. In this case, we want a clean installation.
  // If no symlink exists, the common case is that this is the first installation, but it's
  // possible a prior installation failed before creating the symlink, so we want a clean
  // installation here also.
  RETURN_NOT_OK(DeleteIfExists(versioned_data_dir, Env::Default()));

  // Run local initdb. Do not communicate with the YugaByte cluster at all. This function is only
  // concerned with setting up the local PostgreSQL data directory on this tablet server. We skip
  // local initdb if versioned_data_dir already exists.
  RETURN_NOT_OK(InitDb(versioned_data_dir));
  return Env::Default()->SymlinkPath(versioned_data_dir, conf_.data_dir);
}

Status PgWrapper::InitDbForYSQL(
    const string& master_addresses, const string& tmp_dir_base,
    int tserver_shm_fd) {
  LOG(INFO) << "Running initdb to initialize YSQL cluster with master addresses "
            << master_addresses;
  PgProcessConf conf;
  conf.master_addresses = master_addresses;
  conf.pg_port = 0;  // We should not use this port.
  std::mt19937 rng{std::random_device()()};
  conf.data_dir = Format("$0/tmp_pg_data_$1", tmp_dir_base, rng());
  conf.tserver_shm_fd = tserver_shm_fd;
  auto se = ScopeExit([&conf] {
    auto is_dir = Env::Default()->IsDirectory(conf.data_dir);
    if (is_dir.ok()) {
      if (is_dir.get()) {
        Status del_status = Env::Default()->DeleteRecursively(conf.data_dir);
        if (!del_status.ok()) {
          LOG(WARNING) << "Failed to delete directory " << conf.data_dir;
        }
      }
    } else if (!is_dir.status().IsNotFound()) {
      LOG(WARNING) << "Failed to check directory existence for " << conf.data_dir << ": "
                   << is_dir.status();
    }
  });
  PgWrapper pg_wrapper(conf);
  auto start_time = std::chrono::steady_clock::now();
  Status initdb_status = pg_wrapper.InitDb();
  auto elapsed_time = std::chrono::steady_clock::now() - start_time;
  LOG(INFO)
      << "initdb took "
      << std::chrono::duration_cast<std::chrono::milliseconds>(elapsed_time).count() << " ms";
  if (!initdb_status.ok()) {
    LOG(ERROR) << "initdb failed: " << initdb_status;
  }
  return initdb_status;
}

string PgWrapper::GetPostgresExecutablePath() {
  return JoinPathSegments(GetPostgresInstallRoot(), "bin", "postgres");
}

string PgWrapper::GetPostgresSuppressionsPath() {
  return JoinPathSegments(yb::env_util::GetRootDir("postgres_build"),
    "postgres_build", "src", "tools", "valgrind.supp");
}

string PgWrapper::GetPostgresLibPath() {
  return JoinPathSegments(GetPostgresInstallRoot(), "lib");
}

string PgWrapper::GetPostgresThirdPartyLibPath() {
  return JoinPathSegments(GetPostgresInstallRoot(), "..", "lib", "yb-thirdparty");
}

string PgWrapper::GetInitDbExecutablePath() {
  return JoinPathSegments(GetPostgresInstallRoot(), "bin", "initdb");
}

void PgWrapper::SetCommonEnv(Subprocess* proc, bool yb_enabled) {
  // Used to resolve relative paths during YB init within PG code.
  // Needed because PG changes its current working dir to a data dir.
  char cwd[PATH_MAX];
  CHECK(getcwd(cwd, sizeof(cwd)) != nullptr);
  proc->SetEnv("YB_WORKING_DIR", cwd);
  // A temporary workaround for a failure to look up a user name by uid in an LDAP environment.
  proc->SetEnv("YB_PG_FALLBACK_SYSTEM_USER_NAME", "postgres");
  proc->SetEnv("YB_PG_ALLOW_RUNNING_AS_ANY_USER", "1");
  CHECK_NE(conf_.tserver_shm_fd, -1);
  proc->SetEnv("FLAGS_pggate_tserver_shm_fd", std::to_string(conf_.tserver_shm_fd));
#ifdef OS_MACOSX
  // Postmaster with NLS support fails to start on Mac unless LC_ALL is properly set
  if (getenv("LC_ALL") == nullptr) {
    proc->SetEnv("LC_ALL", "en_US.UTF-8");
  }
#endif
  if (yb_enabled) {
    proc->SetEnv("YB_ENABLED_IN_POSTGRES", "1");
    proc->SetEnv("FLAGS_pggate_master_addresses", conf_.master_addresses);
    // Postgres process can't compute default certs dir by itself
    // as it knows nothing about t-server's root data directory.
    // Solution is to specify it explicitly.
    proc->SetEnv("FLAGS_certs_dir", conf_.certs_dir);
    proc->SetEnv("FLAGS_certs_for_client_dir", conf_.certs_for_client_dir);

    proc->SetEnv("YB_PG_TRANSACTIONS_ENABLED", FLAGS_pg_transactions_enabled ? "1" : "0");

#ifdef ADDRESS_SANITIZER
    // Disable reporting signal-unsafe behavior for PostgreSQL because it does a lot of work in
    // signal handlers on shutdown.

    const char* asan_options = getenv("ASAN_OPTIONS");
    proc->SetEnv(
        "ASAN_OPTIONS",
        std::string(asan_options ? asan_options : "") + " report_signal_unsafe=0");
#endif

    // Pass non-default flags to the child process using FLAGS_... environment variables.
    static const std::vector<string> explicit_flags{"pggate_master_addresses",
                                                    "certs_dir",
                                                    "certs_for_client_dir",
                                                    "mem_tracker_tcmalloc_gc_release_bytes",
                                                    "mem_tracker_update_consumption_interval_us"};
    std::vector<google::CommandLineFlagInfo> flag_infos;
    google::GetAllFlags(&flag_infos);
    for (const auto& flag_info : flag_infos) {
      // Skip the flags that we set explicitly using conf_ above.
      if (!flag_info.is_default &&
          std::find(explicit_flags.begin(),
                    explicit_flags.end(),
                    flag_info.name) == explicit_flags.end()) {
        proc->SetEnv("FLAGS_" + flag_info.name, flag_info.current_value);
      }
    }
  } else {
    proc->SetEnv("YB_PG_LOCAL_NODE_INITDB", "1");
  }
}

// ------------------------------------------------------------------------------------------------
// PgSupervisor: monitoring a PostgreSQL child process and restarting if needed
// ------------------------------------------------------------------------------------------------

PgSupervisor::PgSupervisor(PgProcessConf conf, tserver::TabletServerIf* tserver)
    : conf_(std::move(conf)) {
  if (tserver) {
    tserver->RegisterCertificateReloader(std::bind(&PgSupervisor::ReloadConfig, this));
  }
}

PgSupervisor::~PgSupervisor() {
  std::lock_guard lock(mtx_);
  DeregisterPgFlagChangeNotifications();
}

Status PgSupervisor::CleanupOldServerUnlocked() {
  std::string postmaster_pid_filename = JoinPathSegments(conf_.data_dir, "postmaster.pid");
  if (Env::Default()->FileExists(postmaster_pid_filename)) {
    std::ifstream postmaster_pid_file;
    postmaster_pid_file.open(postmaster_pid_filename, std::ios_base::in);
    pid_t postgres_pid = 0;

    if (!postmaster_pid_file.eof()) {
      postmaster_pid_file >> postgres_pid;
    }

    if (!postmaster_pid_file.good() || postgres_pid == 0) {
      LOG(ERROR) << strings::Substitute("Error reading postgres process ID from file $0. $1 $2",
          postmaster_pid_filename, ErrnoToString(errno), errno);
    } else {
      LOG(WARNING) << "Killing older postgres process: " << postgres_pid;
      // If process does not exist, system may return "process does not exist" or
      // "operation not permitted" error. Ignore those errors.
      postmaster_pid_file.close();
      bool postgres_found = true;
      string cmdline = "";
#ifdef __linux__
      string cmd_filename = "/proc/" + std::to_string(postgres_pid) + "/cmdline";
      std::ifstream postmaster_cmd_file;
      postmaster_cmd_file.open(cmd_filename, std::ios_base::in);
      if (postmaster_cmd_file.good()) {
        postmaster_cmd_file >> cmdline;
        postgres_found = cmdline.find("/postgres") != std::string::npos;
        postmaster_cmd_file.close();
      }
#endif
      if (postgres_found) {
        if (kill(postgres_pid, SIGKILL) != 0 && errno != ESRCH && errno != EPERM) {
          return STATUS(RuntimeError, "Unable to kill", Errno(errno));
        }
      } else {
        LOG(WARNING) << "Didn't find postgres in " << cmdline;
      }
    }
    WARN_NOT_OK(Env::Default()->DeleteFile(postmaster_pid_filename),
                "Failed to remove postmaster pid file");
  }
  return Status::OK();
}

Status PgSupervisor::ReloadConfig() {
  std::lock_guard lock(mtx_);
  if (process_wrapper_) {
    return process_wrapper_->ReloadConfig();
  }
  return Status::OK();
}


Status PgSupervisor::UpdateAndReloadConfig() {
  // See GHI #16055. TSAN detects that Start() and UpdateAndReloadConfig each acquire M0 and M1 in
  // inverse order which may run into a deadlock. However, Start() is always called first and will
  // acquire M0 and M1 before it registers the callback UpdateAndReloadConfig() and will never
  // be called again. Thus the deadlock called out by TSAN is not possible. Silence TSAN warnings
  // from this function to prevent spurious failures.
  debug::ScopedTSANIgnoreSync ignore_sync;
  debug::ScopedTSANIgnoreReadsAndWrites ignore_reads_and_writes;
  std::lock_guard lock(mtx_);
  if (process_wrapper_) {
    return process_wrapper_->UpdateAndReloadConfig();
  }
  return Status::OK();
}

Status PgSupervisor::RegisterReloadPgConfigCallback(const void* flag_ptr) {
  // DeRegisterForPgFlagChangeNotifications is called before flag_callbacks_ is destroyed, so its
  // safe to bind to this.
  flag_callbacks_.emplace_back(VERIFY_RESULT(RegisterFlagUpdateCallback(
      flag_ptr, "ReloadPgConfig", std::bind(&PgSupervisor::UpdateAndReloadConfig, this))));

  return Status::OK();
}

Status PgSupervisor::RegisterPgFlagChangeNotifications() {
  DeregisterPgFlagChangeNotifications();

  vector<CommandLineFlagInfo> flags;
  GetAllFlags(&flags);
  for (const CommandLineFlagInfo& flag : flags) {
    std::unordered_set<FlagTag> tags;
    GetFlagTags(flag.name, &tags);
    if (tags.contains(FlagTag::kPg)) {
      RETURN_NOT_OK(RegisterReloadPgConfigCallback(flag.flag_ptr));
    }
  }

  RETURN_NOT_OK(RegisterReloadPgConfigCallback(&FLAGS_ysql_pg_conf_csv));

  return Status::OK();
}

void PgSupervisor::DeregisterPgFlagChangeNotifications() {
  for (auto& callback : flag_callbacks_) {
    callback.Deregister();
  }
  flag_callbacks_.clear();
}

std::shared_ptr<ProcessWrapper> PgSupervisor::CreateProcessWrapper() {
  auto pgwrapper = std::make_shared<PgWrapper>(conf_);

  if (FLAGS_enable_ysql_conn_mgr) {
    if (FLAGS_enable_ysql_conn_mgr_stats)
      CHECK_OK(pgwrapper->SetYsqlConnManagerStatsShmKey(GetYsqlConnManagerStatsShmkey()));
  } else {
    FLAGS_enable_ysql_conn_mgr_stats = false;
  }

  return pgwrapper;
}

void PgSupervisor::PrepareForStop() {
  PgSupervisor::DeregisterPgFlagChangeNotifications();
}

Status PgSupervisor::PrepareForStart() {
  RETURN_NOT_OK(CleanupOldServerUnlocked());
  RETURN_NOT_OK(RegisterPgFlagChangeNotifications());
  return Status::OK();
}

key_t PgSupervisor::GetYsqlConnManagerStatsShmkey() {
  // Create the shared memory if not yet created.
  if (ysql_conn_mgr_stats_shmem_key_ > 0) {
    return ysql_conn_mgr_stats_shmem_key_;
  }

  // This will be called only when ysql connection manager is enabled and that
  // too just by the PgAdvisor and ysql_conn_manager_advisor so that they can send the
  // shared memory key to ysql_conn_manager for publishing stats and to
  // postmaster to pass it on to yb_pg_metrics to pull ysql_conn_manager stats
  // from memory.
  // Let's use a key start at 13000 + 997 (largest 3 digit prime number). Just decreasing
  // the chances of collision with the pg shared memory key space logic.
  key_t shmem_key = 13000 + 997;
  size_t size_of_shmem = YSQL_CONN_MGR_MAX_POOLS * sizeof(struct ConnectionStats);
  key_t shmid = -1;

  while (true) {
    shmid = shmget(shmem_key, size_of_shmem, IPC_CREAT | IPC_EXCL | 0666);

    if (shmid < 0) {
      switch (errno) {
        case EACCES:
          LOG(ERROR) << "Unable to create shared memory segment, not authorised to create shared "
                        "memory segment";
          return -1;
        case ENOSPC:
          LOG(ERROR)
              << "Unable to create shared memory segment, no space left.";
          return -1;
        case ENOMEM:
          LOG(ERROR)
              << "Unable to create shared memory segment, no memory left";
          return -1;
        default:
          shmem_key++;
          continue;
      }
    }

    ysql_conn_mgr_stats_shmem_key_ = shmem_key;
    return ysql_conn_mgr_stats_shmem_key_;
  }
}

}  // namespace pgwrapper
}  // namespace yb
