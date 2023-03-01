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

import java.time.Duration;
import java.util.List;

import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase.VersionCheckMode;
import com.yugabyte.yw.forms.RuntimeConfigFormData.ScopedConfig.ScopeType;

public class UniverseConfKeys extends RuntimeConfigKeysModule {

  public static final ConfKeyInfo<Duration> alertMaxClockSkew =
      new ConfKeyInfo<>(
          "yb.alert.max_clock_skew_ms",
          ScopeType.UNIVERSE,
          "Clock Skew",
          "Default threshold for Clock Skew alert",
          ConfDataType.DurationType);
  public static final ConfKeyInfo<Boolean> cloudEnabled =
      new ConfKeyInfo<>(
          "yb.cloud.enabled",
          ScopeType.UNIVERSE,
          "Cloud Enabled",
          "Enables YBM specific features",
          ConfDataType.BooleanType);
  public static final ConfKeyInfo<String> universeBootScript =
      new ConfKeyInfo<>(
          "yb.universe_boot_script",
          ScopeType.UNIVERSE,
          "Universe Boot Script",
          "Custom script to run on VM boot during universe provisioning",
          ConfDataType.StringType);
  public static final ConfKeyInfo<Boolean> healthLogOutput =
      new ConfKeyInfo<>(
          "yb.health.logOutput",
          ScopeType.UNIVERSE,
          "Health Log Output",
          "It determines whether to log the output "
              + "of the node health check script to the console",
          ConfDataType.BooleanType);
  public static final ConfKeyInfo<Integer> nodeCheckTimeoutSec =
      new ConfKeyInfo<>(
          "yb.health.nodeCheckTimeoutSec",
          ScopeType.UNIVERSE,
          "Node Checkout Time",
          "The timeout (in seconds) for node check operation as part of universe health check",
          ConfDataType.IntegerType);
  public static final ConfKeyInfo<Boolean> ybUpgradeBlacklistLeaders =
      new ConfKeyInfo<>(
          "yb.upgrade.blacklist_leaders",
          ScopeType.UNIVERSE,
          "YB Upgrade Blacklist Leaders",
          "Determines (boolean) whether we enable/disable "
              + "leader blacklisting when performing universe/node tasks",
          ConfDataType.BooleanType);
  public static final ConfKeyInfo<Integer> ybUpgradeBlacklistLeaderWaitTimeMs =
      new ConfKeyInfo<>(
          "yb.upgrade.blacklist_leader_wait_time_ms",
          ScopeType.UNIVERSE,
          "YB Upgrade Blacklist Leader Wait Time in Ms",
          "The timeout (in milliseconds) that we wait of leader blacklisting on a node to complete",
          ConfDataType.IntegerType);
  public static final ConfKeyInfo<Integer> ybUpgradeMaxFollowerLagThresholdMs =
      new ConfKeyInfo<>(
          "yb.upgrade.max_follower_lag_threshold_ms",
          ScopeType.UNIVERSE,
          "YB Upgrade Max Follower Lag Threshold ",
          "The maximum time (in milliseconds) that we allow a tserver to be behind its peers",
          ConfDataType.IntegerType);
  // TODO(naorem): Add correct metadata
  public static final ConfKeyInfo<Boolean> ybUpgradeVmImage =
      new ConfKeyInfo<>(
          "yb.upgrade.vmImage",
          ScopeType.UNIVERSE,
          "YB Upgrade VM Image",
          "TODO - Leave this for feature owners to fill in",
          ConfDataType.BooleanType);
  // TODO(): Add correct metadata
  public static final ConfKeyInfo<Boolean> allowDowngrades =
      new ConfKeyInfo<>(
          "yb.upgrade.allow_downgrades",
          ScopeType.UNIVERSE,
          "YB Upgrade Allow Downgrades",
          "TODO - Leave this for feature owners to fill in",
          ConfDataType.BooleanType);
  public static final ConfKeyInfo<Boolean> singleConnectionYsqlUpgrade =
      new ConfKeyInfo<>(
          "yb.upgrade.single_connection_ysql_upgrade",
          ScopeType.UNIVERSE,
          "YB Upgrade Use Single Connection Param",
          "The flag, which controls, "
              + "if YSQL catalog upgrade will be performed in single or multi connection mode."
              + "Single connection mode makes it work even on tiny DB nodes.",
          ConfDataType.BooleanType);
  public static final ConfKeyInfo<Boolean> ybEditWaitForLeadersOnPreferred =
      new ConfKeyInfo<>(
          "yb.edit.wait_for_leaders_on_preferred",
          ScopeType.UNIVERSE,
          "YB Edit Wait For Leaders On Preferred Only",
          "Controls whether we perform the createWaitForLeadersOnPreferredOnly subtask"
              + "in editUniverse",
          ConfDataType.BooleanType);
  public static final ConfKeyInfo<String> ybNumReleasesToKeepDefault =
      new ConfKeyInfo<>(
          "yb.releases.num_releases_to_keep_default",
          ScopeType.UNIVERSE,
          "Default Releases Count",
          "Number of Releases to Keep",
          ConfDataType.StringType);
  public static final ConfKeyInfo<String> ybNumReleasesToKeepCloud =
      new ConfKeyInfo<>(
          "yb.releases.num_releases_to_keep_cloud",
          ScopeType.UNIVERSE,
          "Cloud Releases Count",
          "Number Of Cloud Releases To Keep",
          ConfDataType.StringType);
  public static final ConfKeyInfo<Integer> dbMemPostgresMaxMemMb =
      new ConfKeyInfo<>(
          "yb.dbmem.postgres.max_mem_mb",
          ScopeType.UNIVERSE,
          "DB Postgres Max Mem",
          "Amount of memory to limit the postgres process to via the ysql cgroup",
          ConfDataType.IntegerType);
  public static final ConfKeyInfo<Integer> dbMemPostgresReadReplicaMaxMemMb =
      new ConfKeyInfo<>(
          "yb.dbmem.postgres.rr_max_mem_mb",
          ScopeType.UNIVERSE,
          "DB Postgres Max Mem for read replicas",
          "The amount of memory in MB to limit the postgres process in read replicas to via the "
              + "ysql cgroup. "
              + "If the value is -1, it will default to the 'yb.dbmem.postgres.max_mem_mb' value. "
              + "0 will not set any cgroup limits. "
              + ">0 set max memory of postgres to this value for read replicas",
          ConfDataType.IntegerType);
  public static final ConfKeyInfo<Long> dbMemAvailableLimit =
      new ConfKeyInfo<>(
          "yb.dbmem.checks.mem_available_limit_kb",
          ScopeType.UNIVERSE,
          "DB Available Mem Limit",
          "Minimum available memory required on DB nodes for software upgrade.",
          ConfDataType.LongType);
  public static final ConfKeyInfo<Boolean> pgBasedBackup =
      new ConfKeyInfo<>(
          "yb.backup.pg_based",
          ScopeType.UNIVERSE,
          "PG Based Backup",
          "Enable PG-based backup",
          ConfDataType.BooleanType);
  public static final ConfKeyInfo<Boolean> dbReadWriteTest =
      new ConfKeyInfo<>(
          "yb.metrics.db_read_write_test",
          ScopeType.UNIVERSE,
          "DB Read Write Test",
          "The flag defines, if we perform DB write-read check on DB nodes or not.",
          ConfDataType.BooleanType);
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
          ConfDataType.StringType);
  public static final ConfKeyInfo<VersionCheckMode> universeVersionCheckMode =
      new ConfKeyInfo<>(
          "yb.universe_version_check_mode",
          ScopeType.UNIVERSE,
          "Universe Version Check Mode",
          "Possible values: NEVER, HA_ONLY, ALWAYS",
          ConfDataType.VersionCheckModeEnum);
  public static final ConfKeyInfo<Boolean> taskOverrideForceUniverseLock =
      new ConfKeyInfo<>(
          "yb.task.override_force_universe_lock",
          ScopeType.UNIVERSE,
          "Override Force Universe Lock",
          "Whether overriding universe lock is allowed when force option is selected."
              + "If it is disabled, force option will wait for the lock to be released.",
          ConfDataType.BooleanType);
  // TODO(): Add correct metadata
  public static final ConfKeyInfo<Boolean> enableSshKeyExpiration =
      new ConfKeyInfo<>(
          "yb.security.ssh_keys.enable_ssh_key_expiration",
          ScopeType.UNIVERSE,
          "Enable SSH Key Expiration",
          "TODO",
          ConfDataType.BooleanType);
  // TODO(): Add correct metadata
  public static final ConfKeyInfo<Integer> enableSshKeyExpirationThresholdDays =
      new ConfKeyInfo<>(
          "yb.security.ssh_keys.ssh_key_expiration_threshold_days",
          ScopeType.UNIVERSE,
          "SSh Key Expiration Threshold",
          "TODO",
          ConfDataType.IntegerType);
  public static final ConfKeyInfo<String> nfsDirs =
      new ConfKeyInfo<>(
          "yb.ybc_flags.nfs_dirs",
          ScopeType.UNIVERSE,
          "NFS Directry Path",
          "Authorised NFS directories for backups",
          ConfDataType.StringType);
  public static final ConfKeyInfo<Boolean> ybcEnableVervbose =
      new ConfKeyInfo<>(
          "yb.ybc_flags.enable_verbose",
          ScopeType.UNIVERSE,
          "Enable Verbose Logging",
          "Enable verbose ybc logging",
          ConfDataType.BooleanType);
  public static final ConfKeyInfo<Integer> maxThreads =
      new ConfKeyInfo<>(
          "yb.perf_advisor.max_threads",
          ScopeType.UNIVERSE,
          "Max Thread Count",
          "Max number of threads to support parallel querying of nodes",
          ConfDataType.IntegerType);
  public static final ConfKeyInfo<Boolean> ybcAllowScheduledUpgrade =
      new ConfKeyInfo<>(
          "yb.ybc.upgrade.allow_scheduled_upgrade",
          ScopeType.UNIVERSE,
          "Allow Scheduled YBC Upgrades",
          "Enable Scheduled upgrade of ybc on the universe",
          ConfDataType.BooleanType);
  public static final ConfKeyInfo<Boolean> gflagsAllowUserOverride =
      new ConfKeyInfo<>(
          "yb.gflags.allow_user_override",
          ScopeType.UNIVERSE,
          "Allow User Gflags Override",
          "Allow users to override default Gflags values",
          ConfDataType.BooleanType);
  public static final ConfKeyInfo<Boolean> enableTriggerAPI =
      new ConfKeyInfo<>(
          "yb.health.trigger_api.enabled",
          ScopeType.UNIVERSE,
          "Enable Trigger API",
          "Allow trigger_health_check API to be called",
          ConfDataType.BooleanType);
  public static final ConfKeyInfo<Boolean> backupLogVerbose =
      new ConfKeyInfo<>(
          "yb.backup.log.verbose",
          ScopeType.UNIVERSE,
          "Verbose Backup Log",
          "Enable verbose backup logging",
          ConfDataType.BooleanType);
  public static final ConfKeyInfo<Boolean> waitForLbForAddedNodes =
      new ConfKeyInfo<>(
          "yb.wait_for_lb_for_added_nodes",
          ScopeType.UNIVERSE,
          "Wait for LB for Added Nodes",
          "Wait for Load Balancer for added nodes",
          ConfDataType.BooleanType);
  public static final ConfKeyInfo<Boolean> isAuthEnforced =
      new ConfKeyInfo<>(
          "yb.universe.auth.is_enforced",
          ScopeType.UNIVERSE,
          "Enforce Auth",
          "Enforces users to enter password for YSQL/YCQL during Universe creation",
          ConfDataType.BooleanType);
  public static final ConfKeyInfo<Duration> waitForMasterLeaderTimeout =
      new ConfKeyInfo<>(
          "yb.wait_for_master_leader_timeout",
          ScopeType.UNIVERSE,
          "Wait For master Leader timeout",
          "Time in seconds to wait for master leader before timeout for List tables API",
          ConfDataType.DurationType);
  // TODO(Shashank): Add correct metadata
  public static final ConfKeyInfo<Integer> slowQueryLimit =
      new ConfKeyInfo<>(
          "yb.query_stats.slow_queries.limit",
          ScopeType.UNIVERSE,
          "Slow Queries Limit",
          "TODO - Leave this for feature owners to fill in",
          ConfDataType.IntegerType);
  // TODO(Shashank): Add correct metadata
  public static final ConfKeyInfo<String> slowQueryOrderByKey =
      new ConfKeyInfo<>(
          "yb.query_stats.slow_queries.order_by",
          ScopeType.UNIVERSE,
          "Slow Queries Order By Key",
          "TODO - Leave this for feature owners to fill in",
          ConfDataType.StringType);
  // TODO(Shashank)
  public static final ConfKeyInfo<List> excludedQueries =
      new ConfKeyInfo<>(
          "yb.query_stats.excluded_queries",
          ScopeType.UNIVERSE,
          "Excluded Queries",
          "TODO - Leave this for feature owners to fill in",
          ConfDataType.StringListType);
  public static final ConfKeyInfo<Boolean> nodeUIHttpsEnabled =
      new ConfKeyInfo<>(
          "yb.node_ui.https.enabled",
          ScopeType.UNIVERSE,
          "Enable https on Master/TServer UI",
          "Allow https on Master/TServer UI for a universe",
          ConfDataType.BooleanType);
}
