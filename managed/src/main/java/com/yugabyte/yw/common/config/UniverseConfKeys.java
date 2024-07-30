/*
 * Copyright 2022 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.common.config;

import com.google.common.collect.ImmutableList;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase.VersionCheckMode;
import com.yugabyte.yw.common.CloudUtil.Protocol;
import com.yugabyte.yw.common.NodeManager.SkipCertValidationType;
import com.yugabyte.yw.common.config.ConfKeyInfo.ConfKeyTags;
import com.yugabyte.yw.forms.RuntimeConfigFormData.ScopedConfig.ScopeType;
import java.time.Duration;
import java.util.List;

public class UniverseConfKeys extends RuntimeConfigKeysModule {

  public static final ConfKeyInfo<Duration> alertMaxClockSkew =
      new ConfKeyInfo<>(
          "yb.alert.max_clock_skew_ms",
          ScopeType.UNIVERSE,
          "Clock Skew",
          "Default threshold for Clock Skew alert",
          ConfDataType.DurationType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Boolean> healthLogOutput =
      new ConfKeyInfo<>(
          "yb.health.logOutput",
          ScopeType.UNIVERSE,
          "Health Log Output",
          "It determines whether to log the output "
              + "of the node health check script to the console",
          ConfDataType.BooleanType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Integer> nodeCheckTimeoutSec =
      new ConfKeyInfo<>(
          "yb.health.nodeCheckTimeoutSec",
          ScopeType.UNIVERSE,
          "Node Checkout Time",
          "The timeout (in seconds) for node check operation as part of universe health check",
          ConfDataType.IntegerType,
          ImmutableList.of(ConfKeyTags.PUBLIC));

  public static final ConfKeyInfo<Integer> ddlAtomicityIntervalSec =
      new ConfKeyInfo<>(
          "yb.health.ddl_atomicity_interval_sec",
          ScopeType.UNIVERSE,
          "DDL Atomicity Check Interval",
          "The interval (in seconds) between DDL atomicity checks",
          ConfDataType.IntegerType,
          ImmutableList.of(ConfKeyTags.PUBLIC));

  public static final ConfKeyInfo<Boolean> ybUpgradeBlacklistLeaders =
      new ConfKeyInfo<>(
          "yb.upgrade.blacklist_leaders",
          ScopeType.UNIVERSE,
          "YB Upgrade Blacklist Leaders",
          "Determines (boolean) whether we enable/disable "
              + "leader blacklisting when performing universe/node tasks",
          ConfDataType.BooleanType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Integer> ybUpgradeBlacklistLeaderWaitTimeMs =
      new ConfKeyInfo<>(
          "yb.upgrade.blacklist_leader_wait_time_ms",
          ScopeType.UNIVERSE,
          "YB Upgrade Blacklist Leader Wait Time in Ms",
          "The timeout (in milliseconds) that we wait of leader blacklisting on a node to complete",
          ConfDataType.IntegerType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Boolean> leaderBlacklistFailOnTimeout =
      new ConfKeyInfo<>(
          "yb.node_ops.leader_blacklist.fail_on_timeout",
          ScopeType.UNIVERSE,
          "Fail task on leader blacklist timeout",
          "Determines (boolean) whether we fail the task after waiting for leader blacklist "
              + "timeout is reached",
          ConfDataType.BooleanType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Integer> ybUpgradeMaxFollowerLagThresholdMs =
      new ConfKeyInfo<>(
          "yb.upgrade.max_follower_lag_threshold_ms",
          ScopeType.UNIVERSE,
          "YB Upgrade Max Follower Lag Threshold ",
          "The maximum time (in milliseconds) that we allow a tserver to be behind its peers",
          ConfDataType.IntegerType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  // TODO(): Add correct metadata
  public static final ConfKeyInfo<Boolean> allowDowngrades =
      new ConfKeyInfo<>(
          "yb.upgrade.allow_downgrades",
          ScopeType.UNIVERSE,
          "YB Upgrade Allow Downgrades",
          "TODO - Leave this for feature owners to fill in",
          ConfDataType.BooleanType,
          ImmutableList.of(ConfKeyTags.BETA));
  public static final ConfKeyInfo<Boolean> singleConnectionYsqlUpgrade =
      new ConfKeyInfo<>(
          "yb.upgrade.single_connection_ysql_upgrade",
          ScopeType.UNIVERSE,
          "YB Upgrade Use Single Connection Param",
          "The flag, which controls, "
              + "if YSQL catalog upgrade will be performed in single or multi connection mode."
              + "Single connection mode makes it work even on tiny DB nodes.",
          ConfDataType.BooleanType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Duration> ybEditWaitDurationBeforeBlacklistClear =
      new ConfKeyInfo<>(
          "yb.edit.wait_before_blacklist_clear",
          ScopeType.UNIVERSE,
          "YB edit sleep time in ms before blacklist clear in ms",
          "Sleep time before clearing nodes from blacklist in ms",
          ConfDataType.DurationType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Boolean> ybEditWaitForLeadersOnPreferred =
      new ConfKeyInfo<>(
          "yb.edit.wait_for_leaders_on_preferred",
          ScopeType.UNIVERSE,
          "YB Edit Wait For Leaders On Preferred Only",
          "Controls whether we perform the createWaitForLeadersOnPreferredOnly subtask"
              + "in editUniverse",
          ConfDataType.BooleanType,
          ImmutableList.of(ConfKeyTags.INTERNAL));
  public static final ConfKeyInfo<Integer> ybNumReleasesToKeepDefault =
      new ConfKeyInfo<>(
          "yb.releases.num_releases_to_keep_default",
          ScopeType.UNIVERSE,
          "Default Releases Count",
          "Number of Releases to Keep",
          ConfDataType.IntegerType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Integer> ybNumReleasesToKeepCloud =
      new ConfKeyInfo<>(
          "yb.releases.num_releases_to_keep_cloud",
          ScopeType.UNIVERSE,
          "Cloud Releases Count",
          "Number Of Cloud Releases To Keep",
          ConfDataType.IntegerType,
          ImmutableList.of(ConfKeyTags.PUBLIC));

  @Deprecated
  public static final ConfKeyInfo<Integer> dbMemPostgresMaxMemMb =
      new ConfKeyInfo<>(
          "yb.dbmem.postgres.max_mem_mb",
          ScopeType.UNIVERSE,
          "DB Postgres Max Mem (DEPRECATED)",
          "Amount of memory to limit the postgres process to via the ysql cgroup. "
              + "DEPRECATED for now - use 'cgroupSize' in userIntent",
          ConfDataType.IntegerType,
          ImmutableList.of(ConfKeyTags.BETA));

  @Deprecated
  public static final ConfKeyInfo<Integer> dbMemPostgresReadReplicaMaxMemMb =
      new ConfKeyInfo<>(
          "yb.dbmem.postgres.rr_max_mem_mb",
          ScopeType.UNIVERSE,
          "DB Postgres Max Mem for read replicas (DEPRECATED)",
          "The amount of memory in MB to limit the postgres process in read replicas to via the "
              + "ysql cgroup. "
              + "If the value is -1, it will default to the 'yb.dbmem.postgres.max_mem_mb' value. "
              + "0 will not set any cgroup limits. "
              + ">0 set max memory of postgres to this value for read replicas."
              + "DEPRECATED for now - use 'cgroupSize' in userIntent",
          ConfDataType.IntegerType,
          ImmutableList.of(ConfKeyTags.BETA));

  public static final ConfKeyInfo<Long> dbMemAvailableLimit =
      new ConfKeyInfo<>(
          "yb.dbmem.checks.mem_available_limit_kb",
          ScopeType.UNIVERSE,
          "DB Available Mem Limit",
          "Minimum available memory required on DB nodes for software upgrade.",
          ConfDataType.LongType,
          ImmutableList.of(ConfKeyTags.PUBLIC));

  public static final ConfKeyInfo<Boolean> pgBasedBackup =
      new ConfKeyInfo<>(
          "yb.backup.pg_based",
          ScopeType.UNIVERSE,
          "PG Based Backup",
          "Enable PG-based backup",
          ConfDataType.BooleanType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Boolean> dbReadWriteTest =
      new ConfKeyInfo<>(
          "yb.metrics.db_read_write_test",
          ScopeType.UNIVERSE,
          "DB Read Write Test",
          "The flag defines, if we perform DB write-read check on DB nodes or not.",
          ConfDataType.BooleanType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Boolean> ysqlshConnectivityTest =
      new ConfKeyInfo<>(
          "yb.metrics.ysqlsh_connectivity_test",
          ScopeType.UNIVERSE,
          "YSQLSH Connectivity Test",
          "The flag defines, if we perform YSQLSH Connectivity check on DB nodes or not.",
          ConfDataType.BooleanType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Boolean> cqlshConnectivityTest =
      new ConfKeyInfo<>(
          "yb.metrics.cqlsh_connectivity_test",
          ScopeType.UNIVERSE,
          "CQLSH Connectivity Test",
          "The flag defines, if we perform CQLSH Connectivity check on DB nodes or not.",
          ConfDataType.BooleanType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<String> metricsCollectionLevel =
      new ConfKeyInfo<>(
          "yb.metrics.collection_level",
          ScopeType.UNIVERSE,
          "Metrics Collection Level",
          "DB node metrics collection level."
              + "ALL - collect all metrics, "
              + "NORMAL - default value, which only limits some per-table metrics, "
              + "MINIMAL - limits both node level and further limits table level "
              + "metrics we collect and "
              + "OFF to completely disable metric collection.",
          ConfDataType.StringType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<VersionCheckMode> universeVersionCheckMode =
      new ConfKeyInfo<>(
          "yb.universe_version_check_mode",
          ScopeType.UNIVERSE,
          "Universe Version Check Mode",
          "Possible values: NEVER, HA_ONLY, ALWAYS",
          ConfDataType.VersionCheckModeEnum,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Boolean> taskOverrideForceUniverseLock =
      new ConfKeyInfo<>(
          "yb.task.override_force_universe_lock",
          ScopeType.UNIVERSE,
          "Override Force Universe Lock",
          "Whether overriding universe lock is allowed when force option is selected."
              + "If it is disabled, force option will wait for the lock to be released.",
          ConfDataType.BooleanType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  // TODO(): Add correct metadata
  public static final ConfKeyInfo<Boolean> enableSshKeyExpiration =
      new ConfKeyInfo<>(
          "yb.security.ssh_keys.enable_ssh_key_expiration",
          ScopeType.UNIVERSE,
          "Enable SSH Key Expiration",
          "TODO",
          ConfDataType.BooleanType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  // TODO(): Add correct metadata
  public static final ConfKeyInfo<Integer> sshKeyExpirationThresholdDays =
      new ConfKeyInfo<>(
          "yb.security.ssh_keys.ssh_key_expiration_threshold_days",
          ScopeType.UNIVERSE,
          "SSh Key Expiration Threshold",
          "TODO",
          ConfDataType.IntegerType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Boolean> enableSSE =
      new ConfKeyInfo<>(
          "yb.backup.enable_sse",
          ScopeType.UNIVERSE,
          "Enable SSE",
          "Enable SSE during backup/restore",
          ConfDataType.BooleanType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Boolean> allowTableByTableBackupYCQL =
      new ConfKeyInfo<>(
          "yb.backup.allow_table_by_table_backup_ycql",
          ScopeType.UNIVERSE,
          "Allow Table by Table backups for YCQL",
          "Backup tables individually during YCQL backup",
          ConfDataType.BooleanType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<String> nfsDirs =
      new ConfKeyInfo<>(
          "yb.ybc_flags.nfs_dirs",
          ScopeType.UNIVERSE,
          "NFS Directry Path",
          "Authorised NFS directories for backups",
          ConfDataType.StringType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Boolean> ybcEnableVervbose =
      new ConfKeyInfo<>(
          "yb.ybc_flags.enable_verbose",
          ScopeType.UNIVERSE,
          "Enable Verbose Logging",
          "Enable verbose ybc logging",
          ConfDataType.BooleanType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Integer> maxThreads =
      new ConfKeyInfo<>(
          "yb.perf_advisor.max_threads",
          ScopeType.UNIVERSE,
          "Max Thread Count",
          "Max number of threads to support parallel querying of nodes",
          ConfDataType.IntegerType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Boolean> ybcAllowScheduledUpgrade =
      new ConfKeyInfo<>(
          "ybc.upgrade.allow_scheduled_upgrade",
          ScopeType.UNIVERSE,
          "Allow Scheduled YBC Upgrades",
          "Enable Scheduled upgrade of ybc on the universe",
          ConfDataType.BooleanType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Boolean> gflagsAllowUserOverride =
      new ConfKeyInfo<>(
          "yb.gflags.allow_user_override",
          ScopeType.UNIVERSE,
          "Allow User Gflags Override",
          "Allow users to override default Gflags values",
          ConfDataType.BooleanType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Boolean> enableTriggerAPI =
      new ConfKeyInfo<>(
          "yb.health.trigger_api.enabled",
          ScopeType.UNIVERSE,
          "Enable Trigger API",
          "Allow trigger_health_check API to be called",
          ConfDataType.BooleanType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Boolean> backupLogVerbose =
      new ConfKeyInfo<>(
          "yb.backup.log.verbose",
          ScopeType.UNIVERSE,
          "Verbose Backup Log",
          "Enable verbose backup logging",
          ConfDataType.BooleanType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Boolean> waitForLbForAddedNodes =
      new ConfKeyInfo<>(
          "yb.wait_for_lb_for_added_nodes",
          ScopeType.UNIVERSE,
          "Wait for LB for Added Nodes",
          "Wait for Load Balancer for added nodes",
          ConfDataType.BooleanType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Duration> waitForMasterLeaderTimeout =
      new ConfKeyInfo<>(
          "yb.wait_for_master_leader_timeout",
          ScopeType.UNIVERSE,
          "Wait For master Leader timeout",
          "Time in seconds to wait for master leader before timeout for List tables API",
          ConfDataType.DurationType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Integer> slowQueryLimit =
      new ConfKeyInfo<>(
          "yb.query_stats.slow_queries.limit",
          ScopeType.UNIVERSE,
          "Slow Queries Limit",
          "The number of queries to fetch.",
          ConfDataType.IntegerType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<String> slowQueryOrderByKey =
      new ConfKeyInfo<>(
          "yb.query_stats.slow_queries.order_by",
          ScopeType.UNIVERSE,
          "Slow Queries Order By Key",
          "We sort queries by this metric. Possible values: total_time, max_time, mean_time, rows,"
              + " calls",
          ConfDataType.StringType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Boolean> setEnableNestloopOff =
      new ConfKeyInfo<>(
          "yb.query_stats.slow_queries.set_enable_nestloop_off",
          ScopeType.UNIVERSE,
          "Turn off batch nest loop for running slow sql queries",
          "This config turns off and on batch nestloop during running the join statement "
              + "for slow queries. If true, it will be turned off and we expect better "
              + "performance.",
          ConfDataType.BooleanType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<List> excludedQueries =
      new ConfKeyInfo<>(
          "yb.query_stats.excluded_queries",
          ScopeType.UNIVERSE,
          "Excluded Queries",
          "List of queries to exclude from slow queries.",
          ConfDataType.StringListType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Integer> slowQueryLength =
      new ConfKeyInfo<>(
          "yb.query_stats.slow_queries.query_length",
          ScopeType.UNIVERSE,
          "Query character limit",
          "Query character limit in slow queries.",
          ConfDataType.IntegerType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<String> ansibleStrategy =
      new ConfKeyInfo<>(
          "yb.ansible.strategy",
          ScopeType.UNIVERSE,
          "Ansible Strategy",
          "strategy can be linear, mitogen_linear or debug",
          ConfDataType.StringType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Integer> ansibleConnectionTimeoutSecs =
      new ConfKeyInfo<>(
          "yb.ansible.conn_timeout_secs",
          ScopeType.UNIVERSE,
          "Ansible Connection Timeout Duration",
          "This is the default timeout for connection plugins to use.",
          ConfDataType.IntegerType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Integer> ansibleVerbosity =
      new ConfKeyInfo<>(
          "yb.ansible.verbosity",
          ScopeType.UNIVERSE,
          "Ansible Verbosity Level",
          "verbosity of ansible logs, 0 to 4 (more verbose)",
          ConfDataType.IntegerType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Boolean> ansibleDebug =
      new ConfKeyInfo<>(
          "yb.ansible.debug",
          ScopeType.UNIVERSE,
          "Ansible Debug Output",
          "Debug output (can include secrets in output)",
          ConfDataType.BooleanType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Boolean> ansibleDiffAlways =
      new ConfKeyInfo<>(
          "yb.ansible.diff_always",
          ScopeType.UNIVERSE,
          "Ansible Diff Always",
          "Configuration toggle to tell modules to show differences "
              + "when in 'changed' status, equivalent to --diff.",
          ConfDataType.BooleanType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<String> ansibleLocalTemp =
      new ConfKeyInfo<>(
          "yb.ansible.local_temp",
          ScopeType.UNIVERSE,
          "Ansible Local Temp Directory",
          "Temporary directory for Ansible to use on the controller.",
          ConfDataType.StringType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Boolean> perfAdvisorEnabled =
      new ConfKeyInfo<>(
          "yb.perf_advisor.enabled",
          ScopeType.UNIVERSE,
          "Enable Performance Advisor",
          "Defines if performance advisor is enabled for the universe or not",
          ConfDataType.BooleanType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Integer> perfAdvisorUniverseFrequencyMins =
      new ConfKeyInfo<>(
          "yb.perf_advisor.universe_frequency_mins",
          ScopeType.UNIVERSE,
          "Performance Advisor Run Frequency",
          "Defines performance advisor run frequency for universe",
          ConfDataType.IntegerType,
          ImmutableList.of(ConfKeyTags.PUBLIC));

  public static final ConfKeyInfo<Double> perfAdvisorConnectionSkewThreshold =
      new ConfKeyInfo<>(
          "yb.perf_advisor.connection_skew_threshold_pct",
          ScopeType.UNIVERSE,
          "Performance Advisor connection skew threshold",
          "Defines max difference between avg connections count usage and"
              + " node connection count before connection skew recommendation is raised",
          ConfDataType.DoubleType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Integer> perfAdvisorConnectionSkewMinConnections =
      new ConfKeyInfo<>(
          "yb.perf_advisor.connection_skew_min_connections",
          ScopeType.UNIVERSE,
          "Performance Advisor connection skew min connections",
          "Defines minimal number of connections for connection "
              + "skew recommendation to be raised",
          ConfDataType.IntegerType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Integer> perfAdvisorConnectionSkewIntervalMins =
      new ConfKeyInfo<>(
          "yb.perf_advisor.connection_skew_interval_mins",
          ScopeType.UNIVERSE,
          "Performance Advisor connection skew interval mins",
          "Defines time interval for connection skew recommendation check, in minutes",
          ConfDataType.IntegerType,
          ImmutableList.of(ConfKeyTags.PUBLIC));

  public static final ConfKeyInfo<Double> perfAdvisorCpuSkewThreshold =
      new ConfKeyInfo<>(
          "yb.perf_advisor.cpu_skew_threshold_pct",
          ScopeType.UNIVERSE,
          "Performance Advisor cpu skew threshold",
          "Defines max difference between avg cpu usage and"
              + " node cpu usage before cpu skew recommendation is raised",
          ConfDataType.DoubleType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Double> perfAdvisorCpuSkewMinUsage =
      new ConfKeyInfo<>(
          "yb.perf_advisor.cpu_skew_min_usage_pct",
          ScopeType.UNIVERSE,
          "Performance Advisor cpu skew min usage",
          "Defines minimal cpu usage for cpu skew recommendation to be raised",
          ConfDataType.DoubleType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Integer> perfAdvisorCpuSkewIntervalMins =
      new ConfKeyInfo<>(
          "yb.perf_advisor.cpu_skew_interval_mins",
          ScopeType.UNIVERSE,
          "Performance Advisor cpu skew interval mins",
          "Defines time interval for cpu skew recommendation check, in minutes",
          ConfDataType.IntegerType,
          ImmutableList.of(ConfKeyTags.PUBLIC));

  public static final ConfKeyInfo<Double> perfAdvisorCpuUsageThreshold =
      new ConfKeyInfo<>(
          "yb.perf_advisor.cpu_usage_threshold",
          ScopeType.UNIVERSE,
          "Performance Advisor CPU usage threshold",
          "Defines max allowed average CPU usage per 10 minutes before "
              + "CPU usage recommendation is raised",
          ConfDataType.DoubleType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Integer> perfAdvisorCpuUsageIntervalMins =
      new ConfKeyInfo<>(
          "yb.perf_advisor.cpu_usage_interval_mins",
          ScopeType.UNIVERSE,
          "Performance Advisor cpu usage interval mins",
          "Defines time interval for cpu usage recommendation check, in minutes",
          ConfDataType.IntegerType,
          ImmutableList.of(ConfKeyTags.PUBLIC));

  public static final ConfKeyInfo<Double> perfAdvisorQuerySkewThreshold =
      new ConfKeyInfo<>(
          "yb.perf_advisor.query_skew_threshold_pct",
          ScopeType.UNIVERSE,
          "Performance Advisor query skew threshold",
          "Defines max difference between avg queries count and"
              + " node queries count before query skew recommendation is raised",
          ConfDataType.DoubleType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Integer> perfAdvisorQuerySkewMinQueries =
      new ConfKeyInfo<>(
          "yb.perf_advisor.query_skew_min_queries",
          ScopeType.UNIVERSE,
          "Performance Advisor query skew min queries",
          "Defines minimal queries count for query skew recommendation to be raised",
          ConfDataType.IntegerType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Integer> perfAdvisorQuerySkewIntervalMins =
      new ConfKeyInfo<>(
          "yb.perf_advisor.query_skew_interval_mins",
          ScopeType.UNIVERSE,
          "Performance Advisor query skew interval mins",
          "Defines time interval for query skew recommendation check, in minutes",
          ConfDataType.IntegerType,
          ImmutableList.of(ConfKeyTags.PUBLIC));

  public static final ConfKeyInfo<Integer> perfAdvisorRejectedConnThreshold =
      new ConfKeyInfo<>(
          "yb.perf_advisor.rejected_conn_threshold",
          ScopeType.UNIVERSE,
          "Performance Advisor rejected connections threshold",
          "Defines number of rejected connections during configured interval"
              + " for rejected connections recommendation to be raised ",
          ConfDataType.IntegerType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Integer> perfAdvisorRejectedConnIntervalMins =
      new ConfKeyInfo<>(
          "yb.perf_advisor.rejected_conn_interval_mins",
          ScopeType.UNIVERSE,
          "Performance Advisor rejected connections interval mins",
          "Defines time interval for rejected connections recommendation check, in minutes",
          ConfDataType.IntegerType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Double> perfAdvisorHotShardWriteSkewThresholdPct =
      new ConfKeyInfo<>(
          "yb.perf_advisor.hot_shard_write_skew_threshold_pct",
          ScopeType.UNIVERSE,
          "Performance Advisor hot shard write skew threshold",
          "Defines max difference between average node writes and hot shard node writes before "
              + "hot shard recommendation is raised",
          ConfDataType.DoubleType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Double> perfAdvisorHotShardReadSkewThresholdPct =
      new ConfKeyInfo<>(
          "yb.perf_advisor.hot_shard_read_skew_threshold_pct",
          ScopeType.UNIVERSE,
          "Performance Advisor hot shard read skew threshold",
          "Defines max difference between average node reads and hot shard node reads before "
              + "hot shard recommendation is raised",
          ConfDataType.DoubleType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Integer> perfAdvisorHotShardIntervalMins =
      new ConfKeyInfo<>(
          "yb.perf_advisor.hot_shard_interval_mins",
          ScopeType.UNIVERSE,
          "Performance Advisor hot shard interval mins",
          "Defines time interval for hot hard recommendation check, in minutes",
          ConfDataType.IntegerType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Integer> perfAdvisorHotShardMinimalWrites =
      new ConfKeyInfo<>(
          "yb.perf_advisor.hot_shard_min_node_writes",
          ScopeType.UNIVERSE,
          "Performance Advisor hot shard minimal writes",
          "Defines min writes for hot shard recommendation to be raised",
          ConfDataType.IntegerType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Integer> perfAdvisorHotShardMinimalReads =
      new ConfKeyInfo<>(
          "yb.perf_advisor.hot_shard_min_node_reads",
          ScopeType.UNIVERSE,
          "Performance Advisor hot shard minimal reads",
          "Defines min reads for hot shard recommendation to be raised",
          ConfDataType.IntegerType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<SkipCertValidationType> tlsSkipCertValidation =
      new ConfKeyInfo<>(
          "yb.tls.skip_cert_validation",
          ScopeType.UNIVERSE,
          "Skip TLS Cert Validation",
          "Used to skip certificates validation for the configure phase."
              + "Possible values - ALL, HOSTNAME, NONE",
          ConfDataType.SkipCertValdationEnum,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Boolean> deleteOrphanSnapshotOnStartup =
      new ConfKeyInfo<>(
          "yb.snapshot_cleanup.delete_orphan_on_startup",
          ScopeType.UNIVERSE,
          "Clean Orphan snapshots",
          "Clean orphan(non-scheduled) snapshots on Yugaware startup/restart",
          ConfDataType.BooleanType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Boolean> nodeUIHttpsEnabled =
      new ConfKeyInfo<>(
          "yb.node_ui.https.enabled",
          ScopeType.UNIVERSE,
          "Enable https on Master/TServer UI",
          "Allow https on Master/TServer UI for a universe",
          ConfDataType.BooleanType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Long> helmTimeoutSecs =
      new ConfKeyInfo<>(
          "yb.helm.timeout_secs",
          ScopeType.UNIVERSE,
          "Helm Timeout in Seconds",
          "Timeout used for internal universe-level helm operations like install/upgrade in secs",
          ConfDataType.LongType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Boolean> enablePerfAdvisor =
      new ConfKeyInfo<>(
          "yb.ui.feature_flags.perf_advisor",
          ScopeType.UNIVERSE,
          "Enable Perf Advisor to view recommendations",
          "Builds recommendations to help tune our applications accordingly",
          ConfDataType.BooleanType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Boolean> promoteAutoFlag =
      new ConfKeyInfo<>(
          "yb.upgrade.promote_auto_flag",
          ScopeType.UNIVERSE,
          "Promote AutoFlags",
          "Promotes Auto flags while upgrading YB-DB",
          ConfDataType.BooleanType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Duration> autoFlagUpdateSleepTimeInMilliSeconds =
      new ConfKeyInfo<>(
          "yb.upgrade.auto_flag_update_sleep_time_ms",
          ScopeType.UNIVERSE,
          "Sleep time after auto flags are updated.",
          "Controls the amount of time(ms) to wait after auto flags are updated",
          ConfDataType.DurationType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Boolean> allowUpgradeOnTransitUniverse =
      new ConfKeyInfo<>(
          "yb.upgrade.allow_upgrade_on_transit_universe",
          ScopeType.UNIVERSE,
          "Allow upgrade on transit universe",
          "Allow universe upgrade when nodes are in transit mode",
          ConfDataType.BooleanType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Boolean> promoteAutoFlagsForceFully =
      new ConfKeyInfo<>(
          "yb.upgrade.promote_flags_forcefully",
          ScopeType.UNIVERSE,
          "Promote AutoFlags Forcefully",
          "Promote AutoFlags Forcefully during software upgrade",
          ConfDataType.BooleanType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Long> minIncrementalScheduleFrequencyInSecs =
      new ConfKeyInfo<>(
          "yb.backup.minIncrementalScheduleFrequencyInSecs",
          ScopeType.UNIVERSE,
          "Minimum Incremental backup schedule frequency",
          "Minimum Incremental backup schedule frequency in seconds",
          ConfDataType.LongType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<String> universeLogsRegexPattern =
      new ConfKeyInfo<>(
          "yb.support_bundle.universe_logs_regex_pattern",
          ScopeType.UNIVERSE,
          "Universe logs regex pattern",
          "Universe logs regex pattern in support bundle",
          ConfDataType.StringType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<String> postgresLogsRegexPattern =
      new ConfKeyInfo<>(
          "yb.support_bundle.postgres_logs_regex_pattern",
          ScopeType.UNIVERSE,
          "Postgres logs regex pattern",
          "Postgres logs regex pattern in support bundle",
          ConfDataType.StringType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Integer> ysqlUpgradeTimeoutSec =
      new ConfKeyInfo<>(
          "yb.upgrade.ysql_upgrade_timeout_sec",
          ScopeType.UNIVERSE,
          "YSQL Upgrade Timeout in seconds",
          "Controls the yb-client admin operation timeout when performing the runUpgradeYSQL "
              + "subtask rpc calls.",
          ConfDataType.IntegerType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Duration> underReplicatedTabletsTimeout =
      new ConfKeyInfo<>(
          "yb.checks.under_replicated_tablets.timeout",
          ScopeType.UNIVERSE,
          "Under replicated tablets check timeout",
          "Controls the max time out when performing the checkUnderReplicatedTablets subtask",
          ConfDataType.DurationType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Boolean> underReplicatedTabletsCheckEnabled =
      new ConfKeyInfo<>(
          "yb.checks.under_replicated_tablets.enabled",
          ScopeType.UNIVERSE,
          "Enabling under replicated tablets check",
          "Controls whether or not to perform the checkUnderReplicatedTablets subtask",
          ConfDataType.BooleanType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Duration> changeMasterConfigCheckTimeout =
      new ConfKeyInfo<>(
          "yb.checks.change_master_config.timeout",
          ScopeType.UNIVERSE,
          "Master config change result check timeout",
          "Controls the max time out when waiting for master config change to finish",
          ConfDataType.DurationType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Boolean> changeMasterConfigCheckEnabled =
      new ConfKeyInfo<>(
          "yb.checks.change_master_config.enabled",
          ScopeType.UNIVERSE,
          "Enabling Master config change result check",
          "Controls whether or not to wait for master config change to finish",
          ConfDataType.BooleanType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Boolean> followerLagCheckEnabled =
      new ConfKeyInfo<>(
          "yb.checks.follower_lag.enabled",
          ScopeType.UNIVERSE,
          "Enabling follower lag check",
          "Controls whether or not to perform the follower lag checks",
          ConfDataType.BooleanType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Duration> followerLagTimeout =
      new ConfKeyInfo<>(
          "yb.checks.follower_lag.timeout",
          ScopeType.UNIVERSE,
          "Follower lag check timeout",
          "Controls the max time out when performing follower lag checks",
          ConfDataType.DurationType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Duration> followerLagMaxThreshold =
      new ConfKeyInfo<>(
          "yb.checks.follower_lag.max_threshold",
          ScopeType.UNIVERSE,
          "Max threshold for follower lag",
          "The maximum time that we allow a tserver to be behind its peers",
          ConfDataType.DurationType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Duration> waitForServerReadyTimeout =
      new ConfKeyInfo<>(
          "yb.checks.wait_for_server_ready.timeout",
          ScopeType.UNIVERSE,
          "Wait for server ready timeout",
          "Controls the max time for server to finish locally bootstrapping",
          ConfDataType.DurationType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Long> checkMemoryTimeoutSecs =
      new ConfKeyInfo<>(
          "yb.dbmem.checks.timeout",
          ScopeType.UNIVERSE,
          "Memory check timeout",
          "Timeout for memory check in secs",
          ConfDataType.LongType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Duration> sleepTimeBeforeRestoreXClusterSetup =
      new ConfKeyInfo<>(
          "yb.xcluster.sleep_time_before_restore",
          ScopeType.UNIVERSE,
          "Wait time before doing restore during xCluster setup task",
          "The amount of time to sleep (wait) before executing restore subtask during "
              + "xCluster setup; it is useful because xCluster setup also drops the database "
              + "before restore and the sleep makes sure the drop operation has reached all "
              + "the nodes",
          ConfDataType.DurationType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Boolean> useServerBroadcastAddressForYbBackup =
      new ConfKeyInfo<>(
          "yb.backup.use_server_broadcast_address_for_yb_backup",
          ScopeType.UNIVERSE,
          "Use server broadcast address for yb_backup",
          "Controls whether server_broadcast_address entry should be used during yb_backup.py"
              + " backup/restore",
          ConfDataType.BooleanType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Long> slowQueryTimeoutSecs =
      new ConfKeyInfo<>(
          "yb.query_stats.slow_queries.timeout_secs",
          ScopeType.UNIVERSE,
          "Slow Queries Timeout",
          "Timeout in secs for slow queries",
          ConfDataType.LongType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Long> ysqlTimeoutSecs =
      new ConfKeyInfo<>(
          "yb.ysql_timeout_secs",
          ScopeType.UNIVERSE,
          "YSQL Queries Timeout",
          "Timeout in secs for YSQL queries",
          ConfDataType.LongType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Integer> numCoresToKeep =
      new ConfKeyInfo<>(
          "yb.num_cores_to_keep",
          ScopeType.UNIVERSE,
          "Number of cores to keep",
          "Controls the configuration to set the number of cores to keep in the Ansible layer",
          ConfDataType.IntegerType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Boolean> ensureSyncGetReplicationStatus =
      new ConfKeyInfo<>(
          "yb.xcluster.ensure_sync_get_replication_status",
          ScopeType.UNIVERSE,
          "Whether to check YBA xCluster object is in sync with DB replication group",
          "It ensures that the YBA XCluster object for tables that are in replication is "
              + "in sync with replication group in DB. If they are not in sync and this is true, "
              + "getting the xCluster object will throw an exception and the user has to resync "
              + "the xCluster config.",
          ConfDataType.BooleanType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<List> customHealthCheckPorts =
      new ConfKeyInfo<>(
          "yb.universe.network_load_balancer.custom_health_check_ports",
          ScopeType.UNIVERSE,
          "Network Load balancer health check ports",
          "Ports to use for health checks performed by the network load balancer. Invalid and "
              + "duplicate ports will be ignored. For GCP, only the first health "
              + "check port would be used.",
          ConfDataType.IntegerListType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Protocol> customHealthCheckProtocol =
      new ConfKeyInfo<>(
          "yb.universe.network_load_balancer.custom_health_check_protocol",
          ScopeType.UNIVERSE,
          "Network Load balancer health check protocol",
          "Protocol to use for health checks performed by the network load balancer",
          ConfDataType.ProtocolEnum,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<List> customHealthCheckPaths =
      new ConfKeyInfo<>(
          "yb.universe.network_load_balancer.custom_health_check_paths",
          ScopeType.UNIVERSE,
          "Network Load balancer health check paths",
          "Paths probed by HTTP/HTTPS health checks performed by the network load balancer. "
              + "Paths are mapped one-to-one with the custom health check ports "
              + "runtime configuration.",
          ConfDataType.StringListType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Boolean> validateLocalRelease =
      new ConfKeyInfo<>(
          "yb.universe.validate_local_release",
          ScopeType.UNIVERSE,
          "Validate filepath for local release",
          "For certain tasks validates the existence of local filepath for the universe software "
              + "version.",
          ConfDataType.BooleanType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Boolean> enableRollbackSupport =
      new ConfKeyInfo<>(
          "yb.upgrade.enable_rollback_support",
          ScopeType.UNIVERSE,
          "Enable Rollback Support",
          "Enable Yugabyte DB Rollback support",
          ConfDataType.BooleanType,
          ImmutableList.of(ConfKeyTags.INTERNAL));
  public static final ConfKeyInfo<Boolean> allowGFlagsOverrideDuringPreFinalize =
      new ConfKeyInfo<>(
          "yb.gflags.allow_during_prefinalize",
          ScopeType.UNIVERSE,
          "Allow editing GFlags for a universe in pre-finalize state",
          "Allow editing GFlags for a universe in pre-finalize state",
          ConfDataType.BooleanType,
          ImmutableList.of(ConfKeyTags.INTERNAL));
  public static final ConfKeyInfo<Duration> pitrCreatePollDelay =
      new ConfKeyInfo<>(
          "yb.pitr.create_poll_delay",
          ScopeType.UNIVERSE,
          "The delay before the next poll of the PITR config creation status",
          "It is the delay after which the create PITR config subtask rechecks the status of the"
              + " PITR config creation in each iteration",
          ConfDataType.DurationType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Duration> pitrCreateTimeout =
      new ConfKeyInfo<>(
          "yb.pitr.create_timeout",
          ScopeType.UNIVERSE,
          "The timeout for creating a PITR config",
          "It is the maximum time that the create PITR config subtask waits for the PITR config "
              + "to be created; otherwise, it will fail the operation",
          ConfDataType.DurationType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Duration> txnXClusterPitrDefaultRetentionPeriod =
      new ConfKeyInfo<>(
          "yb.xcluster.transactional.pitr.default_retention_period",
          ScopeType.UNIVERSE,
          "Default PITR retention period for txn xCluster",
          "The default retention period used to create PITR configs for transactional "
              + "xCluster replication; it will be used when there is no existing PITR configs "
              + "and it is not specified in the task parameters",
          ConfDataType.DurationType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Duration> txnXClusterPitrDefaultSnapshotInterval =
      new ConfKeyInfo<>(
          "yb.xcluster.transactional.pitr.default_snapshot_interval",
          ScopeType.UNIVERSE,
          "Default PITR snapshot interval for txn xCluster",
          "The default snapshot interval used to create PITR configs for transactional "
              + "xCluster replication; it will be used when there is no existing PITR configs "
              + "and it is not specified in the task parameters",
          ConfDataType.DurationType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Boolean> skipUpgradeFinalize =
      new ConfKeyInfo<>(
          "yb.upgrade.skip_finalize",
          ScopeType.UNIVERSE,
          "Skip Upgrade Finalize",
          "Skip Auto-flags promotions and ysql upgrade during software upgrade",
          ConfDataType.BooleanType,
          ImmutableList.of(ConfKeyTags.INTERNAL));
  public static final ConfKeyInfo<Boolean> allowConfigureYSQL =
      new ConfKeyInfo<>(
          "yb.configure_db_api.ysql",
          ScopeType.UNIVERSE,
          "Configure YSQL DB API",
          "Allow users to configure YSQL DB API from UI",
          ConfDataType.BooleanType,
          ImmutableList.of(ConfKeyTags.INTERNAL));
  public static final ConfKeyInfo<Boolean> allowConfigureYCQL =
      new ConfKeyInfo<>(
          "yb.configure_db_api.ycql",
          ScopeType.UNIVERSE,
          "Configure YCQL DB API",
          "Allow users to configure YCQL DB API from UI",
          ConfDataType.BooleanType,
          ImmutableList.of(ConfKeyTags.INTERNAL));
  public static final ConfKeyInfo<Duration> waitForReplicationDrainTimeout =
      new ConfKeyInfo<>(
          "yb.xcluster.transactional.wait_for_replication_drain_timeout",
          ScopeType.UNIVERSE,
          "Timeout for the WaitForReplicationDrain subtask",
          "The minimum amount of time that the waitForReplicationDrain subtask waits for the "
              + "replication streams to completely drain from the source to the target universe",
          ConfDataType.DurationType,
          ImmutableList.of(ConfKeyTags.INTERNAL));
  public static final ConfKeyInfo<Boolean> skipBackupMetadataValidation =
      new ConfKeyInfo<>(
          "yb.backup.skip_metadata_validation",
          ScopeType.UNIVERSE,
          "Skip backup metadata validation",
          "Skip backup metadata based validation during restore",
          ConfDataType.BooleanType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Boolean> alwaysBackupTablespaces =
      new ConfKeyInfo<>(
          "yb.backup.always_backup_tablespaces",
          ScopeType.UNIVERSE,
          "Always backup tablespaces when taking YSQL backup",
          "Always backup tablespaces when taking ysql backups. This is a UI flag"
              + " used to appropriately send 'useTablespaces' parameter to backend in API.",
          ConfDataType.BooleanType,
          ImmutableList.of(ConfKeyTags.INTERNAL));
  public static final ConfKeyInfo<Integer> nodeAgentReinstallParallelism =
      new ConfKeyInfo<>(
          "yb.node_agent.reinstall_parallelism",
          ScopeType.UNIVERSE,
          "Parallelism for Node Agent Reinstallation",
          "Number of parallel node agent reinstallations at a time",
          ConfDataType.IntegerType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Boolean> ldapUniverseSync =
      new ConfKeyInfo<>(
          "yb.security.ldap.ldap_universe_sync",
          ScopeType.UNIVERSE,
          "To perform sync of user-groups between the Universe DB nodes and LDAP Server",
          "If configured, this feature allows users to synchronise user groups configured"
              + " on the upstream LDAP Server with user roles in YBDB nodes associated"
              + " with the universe.",
          ConfDataType.BooleanType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Boolean> notifyPeerOnRemoval =
      new ConfKeyInfo<>(
          "yb.gflags.notify_peer_of_removal_from_cluster",
          ScopeType.UNIVERSE,
          "Notify Peers in Cluster on Node Removal",
          "Notify peers in cluster on a master node removal",
          ConfDataType.BooleanType,
          ImmutableList.of(ConfKeyTags.INTERNAL));
  public static final ConfKeyInfo<Boolean> alwaysWaitForDataMove =
      new ConfKeyInfo<>(
          "yb.always_wait_for_data_move",
          ScopeType.UNIVERSE,
          "Always wait for data move on remove node",
          "Always run wait for data move during remove node",
          ConfDataType.BooleanType,
          ImmutableList.of(ConfKeyTags.INTERNAL));
  public static final ConfKeyInfo<Boolean> clusterMembershipCheckEnabled =
      new ConfKeyInfo<>(
          "yb.checks.cluster_membership.enabled",
          ScopeType.UNIVERSE,
          "Enable check for cluster membership",
          "If enabled, performs a pre-check to make sure node is not part of master quorum"
              + "and the node does not have any tablets assigned to it in the tserver quorum.",
          ConfDataType.BooleanType,
          ImmutableList.of(ConfKeyTags.INTERNAL));
  public static final ConfKeyInfo<Duration> clusterMembershipCheckTimeout =
      new ConfKeyInfo<>(
          "yb.checks.cluster_membership.timeout",
          ScopeType.UNIVERSE,
          "Cluster membership check timeout",
          "Controls the max time to check that there are no tablets assigned to the node",
          ConfDataType.DurationType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Boolean> skipConfigBasedPreflightValidation =
      new ConfKeyInfo<>(
          "yb.backup.skip_config_based_preflight_validation",
          ScopeType.UNIVERSE,
          "Skip storage config backup/restore preflight validation",
          "Skip preflight validation before backup/scheduled backups/incremental backups/restores."
              + " This skips the storage config/success marker based validations done before B/R.",
          ConfDataType.BooleanType,
          ImmutableList.of(ConfKeyTags.INTERNAL));

  public static final ConfKeyInfo<Boolean> enableDbQueryApi =
      new ConfKeyInfo<>(
          "yb.security.enable_db_query_api",
          ScopeType.UNIVERSE,
          "Enable .../run_query API for the universe",
          "Enables the ability to execute SQL queries through YBA API",
          ConfDataType.BooleanType,
          ImmutableList.of(ConfKeyTags.INTERNAL));
  public static final ConfKeyInfo<Boolean> verifyClusterStateBeforeTask =
      new ConfKeyInfo<>(
          "yb.task.verify_cluster_state",
          ScopeType.UNIVERSE,
          "Verify current cluster state (from db perspective) before running task",
          "Verify current cluster state (from db perspective) before running task",
          ConfDataType.BooleanType,
          ImmutableList.of(ConfKeyTags.INTERNAL));
  public static final ConfKeyInfo<Duration> xclusterSetupAlterTimeout =
      new ConfKeyInfo<>(
          "yb.xcluster.operation_timeout",
          ScopeType.UNIVERSE,
          "Wait time for xcluster/DR replication setup and edit RPCs.",
          "Wait time for xcluster/DR replication setup and edit RPCs.",
          ConfDataType.DurationType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Boolean> leaderlessTabletsCheckEnabled =
      new ConfKeyInfo<>(
          "yb.checks.leaderless_tablets.enabled",
          ScopeType.UNIVERSE,
          "Leaderless tablets check enabled",
          " Whether to run CheckLeaderlessTablets subtask before running universe tasks",
          ConfDataType.BooleanType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Duration> leaderlessTabletsTimeout =
      new ConfKeyInfo<>(
          "yb.checks.leaderless_tablets.timeout",
          ScopeType.UNIVERSE,
          "Leaderless tablets check timeout",
          "Controls the max time out when performing the CheckLeaderlessTablets subtask",
          ConfDataType.DurationType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Boolean> clockSyncCheckEnabled =
      new ConfKeyInfo<>(
          "yb.wait_for_clock_sync.enabled",
          ScopeType.UNIVERSE,
          "Enable Clock Sync check",
          "Enable Clock Sync check",
          ConfDataType.BooleanType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Boolean> enableYbcForUniverse =
      new ConfKeyInfo<>(
          "ybc.universe.enabled",
          ScopeType.UNIVERSE,
          "Enable YBC",
          "Enable YBC for universes during software upgrade",
          ConfDataType.BooleanType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Boolean> useNodesAreSafeToTakeDown =
      new ConfKeyInfo<>(
          "yb.checks.nodes_safe_to_take_down.enabled",
          ScopeType.UNIVERSE,
          "Check if nodes are safe to take down before running upgrades",
          "Check if nodes are safe to take down before running upgrades",
          ConfDataType.BooleanType,
          ImmutableList.of(ConfKeyTags.INTERNAL));

  public static final ConfKeyInfo<Duration> nodesAreSafeToTakeDownCheckTimeout =
      new ConfKeyInfo<>(
          "yb.checks.nodes_safe_to_take_down.timeout",
          ScopeType.UNIVERSE,
          "Timeout for checking if nodes are safe to take down before running upgrades",
          "Timeout for checking if nodes are safe to take down before running upgrades",
          ConfDataType.DurationType,
          ImmutableList.of(ConfKeyTags.INTERNAL));
  public static final ConfKeyInfo<Integer> targetNodeDiskUsagePercentage =
      new ConfKeyInfo<>(
          "yb.checks.node_disk_size.target_usage_percentage",
          ScopeType.UNIVERSE,
          "Target Node Disk Usage Percentage",
          "Percentage of current disk usage that may consume on the target nodes",
          ConfDataType.IntegerType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Boolean> enableAutoMasterFailover =
      new ConfKeyInfo<>(
          "yb.auto_master_failover.enabled",
          ScopeType.UNIVERSE,
          "Enable Automated Master Failover",
          "Enable Automated Master Failover for universes in background process",
          ConfDataType.BooleanType,
          ImmutableList.of(ConfKeyTags.INTERNAL));
  public static final ConfKeyInfo<Duration> autoMasterFailoverMaxMasterFollowerLag =
      new ConfKeyInfo<>(
          "yb.auto_master_failover.max_master_follower_lag",
          ScopeType.UNIVERSE,
          "Max Master Follower Lag for Automated Master Failover",
          "Max lag allowed for a master follower, after which the master is considered for"
              + " failover",
          ConfDataType.DurationType,
          ImmutableList.of(ConfKeyTags.INTERNAL));
  public static final ConfKeyInfo<Duration> autoMasterFailoverMaxMasterHeartbeatDelay =
      new ConfKeyInfo<>(
          "yb.auto_master_failover.max_master_heartbeat_delay",
          ScopeType.UNIVERSE,
          "Max master heartbeat delay",
          "Maximum value of heartbeat delay allowed before master is considered to have failed",
          ConfDataType.DurationType,
          ImmutableList.of(ConfKeyTags.INTERNAL));
  public static final ConfKeyInfo<Boolean> upgradeBatchRollEnabled =
      new ConfKeyInfo<>(
          "yb.task.upgrade.batch_roll_enabled",
          ScopeType.UNIVERSE,
          "Stop multiple nodes in az simultaneously during upgrade",
          "Stop multiple nodes simultaneously in az during upgrade",
          ConfDataType.BooleanType,
          ImmutableList.of(ConfKeyTags.INTERNAL));
  public static final ConfKeyInfo<Duration> autoMasterFailoverDetectionInterval =
      new ConfKeyInfo<>(
          "yb.auto_master_failover.detect_interval",
          ScopeType.UNIVERSE,
          "Automated Master Failover Detection Interval",
          "Automated master failover detection interval for a universe in background process",
          ConfDataType.DurationType,
          ImmutableList.of(ConfKeyTags.INTERNAL));
  public static final ConfKeyInfo<Duration> autoMasterFailoverTaskInterval =
      new ConfKeyInfo<>(
          "yb.auto_master_failover.task_interval",
          ScopeType.UNIVERSE,
          "Automated Master Failover Task Interval",
          "Automated master failover task submission interval for a universe in background process",
          ConfDataType.DurationType,
          ImmutableList.of(ConfKeyTags.INTERNAL));
  public static final ConfKeyInfo<Boolean> nodeAgentNodeActionUseJavaClient =
      new ConfKeyInfo<>(
          "yb.node_agent.node_action.use_java_client",
          ScopeType.UNIVERSE,
          "Use Node Agent Java Client for Node Actions",
          "Use node agent java client to run node actions on the remote nodes",
          ConfDataType.BooleanType,
          ImmutableList.of(ConfKeyTags.INTERNAL));
  public static final ConfKeyInfo<Boolean> xClusterSyncOnUniverse =
      new ConfKeyInfo<>(
          "yb.xcluster.xcluster_sync_on_universe",
          ScopeType.UNIVERSE,
          "XCluster Sync on Universe",
          "Enable automatic synchronization of XCluster on Universe",
          ConfDataType.BooleanType,
          ImmutableList.of(ConfKeyTags.BETA));
  public static final ConfKeyInfo<Duration> autoMasterFailoverCooldown =
      new ConfKeyInfo<>(
          "yb.auto_master_failover.cooldown",
          ScopeType.UNIVERSE,
          "Cooldown period for consecutive master failovers",
          "Minimum duration that the platform waits after the last master failover task completion"
              + " before considering the next master failover",
          ConfDataType.DurationType,
          ImmutableList.of(ConfKeyTags.INTERNAL));
  public static final ConfKeyInfo<Integer> autoMasterFailoverMaxTaskRetries =
      new ConfKeyInfo<>(
          "yb.auto_master_failover.max_task_retries",
          ScopeType.UNIVERSE,
          "Max retries for Master Failover Task",
          "Maximum number of times the master failover task will be retried in case of failures,"
              + " before giving up and requiring manual intervention",
          ConfDataType.IntegerType,
          ImmutableList.of(ConfKeyTags.INTERNAL));
  public static final ConfKeyInfo<Integer> cpuUsageAggregationInterval =
      new ConfKeyInfo<>(
          "yb.alert.cpu_usage_interval_secs",
          ScopeType.UNIVERSE,
          "CPU usage alert aggregation interval",
          "CPU usage alert aggregation interval in seconds.",
          ConfDataType.IntegerType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Boolean> healthCheckTimeDrift =
      new ConfKeyInfo<>(
          "yb.health_checks.check_clock_time_drift",
          ScopeType.UNIVERSE,
          "Enable health checks for time drift between nodes",
          "Enable health checks for time drift between nodes.",
          ConfDataType.BooleanType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Integer> healthCheckTimeDriftWrnThreshold =
      new ConfKeyInfo<>(
          "yb.health_checks.time_drift_wrn_threshold_ms",
          ScopeType.UNIVERSE,
          "Time drift threshold for warning health check",
          "Threshold to raise a warning when time drift exceeds this amount",
          ConfDataType.IntegerType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Integer> healthCheckTimeDriftErrThreshold =
      new ConfKeyInfo<>(
          "yb.health_checks.time_drift_err_threshold_ms",
          ScopeType.UNIVERSE,
          "Time drift threshold for error health check",
          "Threshold to raise a error when time drift exceeds this amount",
          ConfDataType.IntegerType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
}
