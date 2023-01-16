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

import com.yugabyte.yw.forms.RuntimeConfigFormData.ScopedConfig.ScopeType;

public class GlobalConfKeys extends RuntimeConfigKeysModule {

  public static final ConfKeyInfo<Integer> taskDbQueryLimit =
      new ConfKeyInfo<>(
          "yb.customer_task_db_query_limit",
          ScopeType.GLOBAL, // TODO Customer
          "Max Number of Customer Tasks to fetch",
          "Knob that can be used when there are too many customer tasks"
              + " overwhelming the server",
          ConfDataType.IntegerType);
  public static final ConfKeyInfo<Integer> maxParallelNodeChecks =
      new ConfKeyInfo<>(
          "yb.health.max_num_parallel_node_checks",
          ScopeType.GLOBAL,
          "Max Number of Parallel Node Checks",
          "Number of parallel node checks, spawned as part of universes health check process",
          ConfDataType.IntegerType);
  public static final ConfKeyInfo<Boolean> logScriptOutput =
      new ConfKeyInfo<>(
          "yb.ha.logScriptOutput",
          ScopeType.GLOBAL,
          "Log Script Output For YBA HA Feature",
          "To log backup restore script output for debugging issues",
          ConfDataType.BooleanType);
  // Looks like most of these ansible keys are already universe scope:
  public static final ConfKeyInfo<String> ansibleStrategy =
      new ConfKeyInfo<>(
          "yb.ansible.strategy",
          ScopeType.GLOBAL,
          "Ansible Strategy",
          "strategy can be linear, mitogen_linear or debug",
          ConfDataType.StringType);
  public static final ConfKeyInfo<Integer> ansibleConnectionTimeoutSecs =
      new ConfKeyInfo<>(
          "yb.ansible.conn_timeout_secs",
          ScopeType.GLOBAL,
          "Ansible Connection Timeout Duration",
          "This is the default timeout for connection plugins to use.",
          ConfDataType.IntegerType);
  public static final ConfKeyInfo<Integer> ansibleVerbosity =
      new ConfKeyInfo<>(
          "yb.ansible.verbosity",
          ScopeType.GLOBAL,
          "Ansible Verbosity Level",
          "verbosity of ansible logs, 0 to 4 (more verbose)",
          ConfDataType.IntegerType);
  public static final ConfKeyInfo<Boolean> ansibleDebug =
      new ConfKeyInfo<>(
          "yb.ansible.debug",
          ScopeType.GLOBAL,
          "Ansible Debug Output",
          "Debug output (can include secrets in output)",
          ConfDataType.BooleanType);
  public static final ConfKeyInfo<Boolean> ansibleDiffAlways =
      new ConfKeyInfo<>(
          "yb.ansible.diff_always",
          ScopeType.GLOBAL,
          "Ansible Diff Always",
          "Configuration toggle to tell modules to show differences "
              + "when in 'changed' status, equivalent to --diff.",
          ConfDataType.BooleanType);
  public static final ConfKeyInfo<String> ansibleLocalTemp =
      new ConfKeyInfo<>(
          "yb.ansible.local_temp",
          ScopeType.GLOBAL,
          "Ansible Local Temp Directory",
          "Temporary directory for Ansible to use on the controller.",
          ConfDataType.StringType);
  // TODO: Use Enum type SkipCertValidationType
  // I traced this to be Using Universe scope
  public static final ConfKeyInfo<String> tlsSkipCertValidation =
      new ConfKeyInfo<>(
          "yb.tls.skip_cert_validation",
          ScopeType.GLOBAL,
          "Skip TLS Cert Validation",
          "Used to skip certificates validation for the configure phase."
              + "Possible values - ALL, HOSTNAME",
          ConfDataType.StringType);
  // TODO(Steven): Add correct metadata
  public static final ConfKeyInfo<Boolean> useKubectl =
      new ConfKeyInfo<>(
          "yb.use_kubectl",
          ScopeType.GLOBAL,
          "Use Kubectl",
          "TODO - Leave this for feature owners to fill ",
          ConfDataType.BooleanType);
  // TODO(): Add correct metadata
  public static final ConfKeyInfo<Boolean> useNewHelmNaming =
      new ConfKeyInfo<>(
          "yb.use_new_helm_naming",
          ScopeType.GLOBAL,
          "Use New Helm Naming",
          "TODO - Leave this for feature owners to fill in",
          ConfDataType.BooleanType);
  // TODO(taran): Add correct metadata
  public static final ConfKeyInfo<Boolean> useOauth =
      new ConfKeyInfo<>(
          "yb.security.use_oauth",
          ScopeType.GLOBAL,
          "Use OAUTH",
          "TODO - Leave this for feature owners to fill in",
          ConfDataType.BooleanType);
  // TODO(taran): Add correct metadata
  public static final ConfKeyInfo<String> ybSecurityType =
      new ConfKeyInfo<>(
          "yb.security.type",
          ScopeType.GLOBAL,
          "YB Security Type",
          "TODO - Leave this for feature owners to fill in",
          ConfDataType.StringType);
  // TODO(taran): Add correct metadata
  public static final ConfKeyInfo<String> ybClientID =
      new ConfKeyInfo<>(
          "yb.security.clientID",
          ScopeType.GLOBAL,
          "YB Client ID",
          "TODO - Leave this for feature owners to fill in",
          ConfDataType.StringType);
  // TODO(taran): Add correct metadata
  public static final ConfKeyInfo<String> ybSecuritySecret =
      new ConfKeyInfo<>(
          "yb.security.secret",
          ScopeType.GLOBAL,
          "YB Security Secret",
          "TODO - Leave this for feature owners to fill in",
          ConfDataType.StringType);
  // TODO(taran): Add correct metadata
  public static final ConfKeyInfo<String> discoveryURI =
      new ConfKeyInfo<>(
          "yb.security.discoveryURI",
          ScopeType.GLOBAL,
          "Discovery URI",
          "TODO - Leave this for feature owners to fill in",
          ConfDataType.StringType);
  // TODO(taran): Add correct metadata
  public static final ConfKeyInfo<String> oidcScope =
      new ConfKeyInfo<>(
          "yb.security.iodcScope",
          ScopeType.GLOBAL,
          "OIDC Scope",
          "TODO - Leave this for feature owners to fill in",
          ConfDataType.StringType);
  // TODO(taran): Add correct metadata
  public static final ConfKeyInfo<String> oidcEmailAttribute =
      new ConfKeyInfo<>(
          "yb.security.oidcEmailAttribute",
          ScopeType.GLOBAL,
          "OIDC Email Attribute",
          "TODO - Leave this for feature owners to fill in",
          ConfDataType.StringType);
  public static final ConfKeyInfo<Boolean> ssh2Enabled =
      new ConfKeyInfo<>(
          "yb.security.ssh2_enabled",
          ScopeType.GLOBAL,
          "Enable SSH2",
          "Flag for enabling ssh2 on YBA",
          ConfDataType.BooleanType);
  public static final ConfKeyInfo<Boolean> enableCustomHooks =
      new ConfKeyInfo<>(
          "yb.security.custom_hooks.enable_custom_hooks",
          ScopeType.GLOBAL,
          "Enable Custom Hooks",
          "Flag for enabling custom hooks on YBA",
          ConfDataType.BooleanType);
  public static final ConfKeyInfo<Boolean> enableSudo =
      new ConfKeyInfo<>(
          "yb.security.custom_hooks.enable_sudo",
          ScopeType.GLOBAL,
          "Enable SUDO",
          "Flag for enabling sudo access while running custom hooks",
          ConfDataType.BooleanType);
  public static final ConfKeyInfo<Boolean> disableXxHashChecksum =
      new ConfKeyInfo<>(
          "yb.backup.disable_xxhash_checksum",
          ScopeType.GLOBAL,
          "Disable XX Hash Checksum",
          "Flag for disabling xxhsum based checksums for computing the backup",
          ConfDataType.BooleanType);
  // TODO(): Add correct metadata
  public static final ConfKeyInfo<Boolean> auditVerifyLogging =
      new ConfKeyInfo<>(
          "yb.audit.log.verifyLogging",
          ScopeType.GLOBAL,
          "Audit Verify Logging",
          "TODO - Leave this for feature owners to fill in",
          ConfDataType.BooleanType);
  // TODO(): Add correct metadata
  public static final ConfKeyInfo<Boolean> auditOutputToStdout =
      new ConfKeyInfo<>(
          "yb.audit.log.outputToStdout",
          ScopeType.GLOBAL,
          "Audit Log Output to Stdout",
          "TODO - Leave this for feature owners to fill in",
          ConfDataType.BooleanType);
  // TODO(): Add correct metadata
  public static final ConfKeyInfo<Boolean> auditOutputToFile =
      new ConfKeyInfo<>(
          "yb.audit.log.outputToFile",
          ScopeType.GLOBAL,
          "Audit Log Output to File",
          "TODO - Leave this for feature owners to fill in",
          ConfDataType.BooleanType);
  // TODO(): Add correct metadata
  public static final ConfKeyInfo<String> auditRolloverPattern =
      new ConfKeyInfo<>(
          "yb.audit.log.rolloverPattern",
          ScopeType.GLOBAL,
          "Audit Log Rollover Pattern",
          "TODO - Leave this for feature owners to fill in",
          ConfDataType.StringType);
  // TODO(): Add correct metadata
  public static final ConfKeyInfo<String> auditMaxHistory =
      new ConfKeyInfo<>(
          "yb.audit.log.maxHistory",
          ScopeType.GLOBAL,
          "Audit Log Max History",
          "TODO - Leave this for feature owners to fill in",
          ConfDataType.StringType);
  public static final ConfKeyInfo<Boolean> supportBundleK8sEnabled =
      new ConfKeyInfo<>(
          "yb.support_bundle.k8s_enabled",
          ScopeType.GLOBAL,
          "Enable K8s Support Bundle",
          "This config lets you enable support bundle creation on k8s universes.",
          ConfDataType.BooleanType);
  public static final ConfKeyInfo<Boolean> supportBundleOnPremEnabled =
      new ConfKeyInfo<>(
          "yb.support_bundle.onprem_enabled",
          ScopeType.GLOBAL,
          "Enable On Prem Support Bundle",
          "This config lets you enable support bundle creation for onprem universes.",
          ConfDataType.BooleanType);
  public static final ConfKeyInfo<Boolean> runtimeConfigUiEnableForAll =
      new ConfKeyInfo<>(
          "yb.runtime_conf_ui.enable_for_all",
          ScopeType.GLOBAL,
          "Runtime Config UI",
          "Allows users to view the runtime configuration properties via UI",
          ConfDataType.BooleanType);
  public static final ConfKeyInfo<Boolean> isPlatformDowngradeAllowed =
      new ConfKeyInfo<>(
          "yb.is_platform_downgrade_allowed",
          ScopeType.GLOBAL,
          "Allow Platform Downgrade",
          "Allow Downgrading the Platform Version",
          ConfDataType.BooleanType);
  public static final ConfKeyInfo<Duration> ybcUpgradeInterval =
      new ConfKeyInfo<>(
          "ybc.upgrade.scheduler_interval",
          ScopeType.GLOBAL,
          "YBC Upgrade Interval",
          "YBC Upgrade interval",
          ConfDataType.DurationType);
  public static final ConfKeyInfo<Integer> ybcUniverseBatchSize =
      new ConfKeyInfo<>(
          "ybc.upgrade.universe_batch_size",
          ScopeType.GLOBAL,
          "YBC Universe Upgrade Batch Size",
          "The number of maximum universes on which ybc will be upgraded simultaneously",
          ConfDataType.IntegerType);
  public static final ConfKeyInfo<Integer> ybcNodeBatchSize =
      new ConfKeyInfo<>(
          "ybc.upgrade.node_batch_size",
          ScopeType.GLOBAL,
          "YBC Node Upgrade Batch Size",
          "The number of maximum nodes on which ybc will be upgraded simultaneously",
          ConfDataType.IntegerType);
  public static final ConfKeyInfo<String> ybcStableVersion =
      new ConfKeyInfo<>(
          "ybc.releases.stable_version",
          ScopeType.GLOBAL,
          "YBC Stable Release",
          "Stable version for Yb-Controller",
          ConfDataType.StringType);
  public static final ConfKeyInfo<Boolean> enableCertReload =
      new ConfKeyInfo<>(
          "yb.features.cert_reload.enabled",
          ScopeType.GLOBAL,
          "Enable Cert Reload",
          "Enable hot reload of TLS certificates without restart of the DB nodes",
          ConfDataType.BooleanType);
  public static final ConfKeyInfo<Boolean> cmdOutputDelete =
      new ConfKeyInfo<>(
          "yb.logs.shell.cmdOutputDelete",
          ScopeType.GLOBAL,
          "Delete Output File",
          "Flag to delete temp output file created by the shell command",
          ConfDataType.BooleanType);
  public static final ConfKeyInfo<Integer> shellOutputRetentationHours =
      new ConfKeyInfo<>(
          "yb.logs.shell.output_retention_hours",
          ScopeType.GLOBAL,
          "Shell Output Retention Duration",
          "Output logs for shell commands are written to tmp folder."
              + "This setting defines how long will we wait before garbage collecting them.",
          ConfDataType.IntegerType);
  public static final ConfKeyInfo<Long> shellMaxOutputDirSize =
      new ConfKeyInfo<>(
          "yb.logs.shell.output_dir_max_size",
          ScopeType.GLOBAL,
          "Shell Output Max Directory Size",
          "Output logs for shell commands are written to tmp folder."
              + "This setting defines rotation policy based on directory size.",
          ConfDataType.BytesType);
  // TODO(Shashank): Add correct metadata
  public static final ConfKeyInfo<Long> logsMaxMsgSize =
      new ConfKeyInfo<>(
          "yb.logs.max_msg_size",
          ScopeType.GLOBAL,
          "Max Log Message Size",
          "TODO",
          ConfDataType.BytesType);
  public static final ConfKeyInfo<Duration> kmsRefreshInterval =
      new ConfKeyInfo<>(
          "yb.kms.refresh_interval",
          ScopeType.GLOBAL,
          "KMS Refresh Interval",
          "Default refresh interval for the KMS providers.",
          ConfDataType.DurationType);
}
