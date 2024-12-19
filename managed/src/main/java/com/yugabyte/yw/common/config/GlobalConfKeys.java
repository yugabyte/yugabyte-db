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
import com.yugabyte.yw.common.LdapUtil.TlsProtocol;
import com.yugabyte.yw.common.config.ConfKeyInfo.ConfKeyTags;
import com.yugabyte.yw.forms.RuntimeConfigFormData.ScopedConfig.ScopeType;
import com.yugabyte.yw.models.Users.Role;
import java.time.Duration;
import java.util.List;
import org.apache.directory.api.ldap.model.message.SearchScope;

public class GlobalConfKeys extends RuntimeConfigKeysModule {

  public static final ConfKeyInfo<Integer> maxParallelNodeChecks =
      new ConfKeyInfo<>(
          "yb.health.max_num_parallel_node_checks",
          ScopeType.GLOBAL,
          "Max Number of Parallel Node Checks",
          "Number of parallel node checks, spawned as part of universes health check process",
          ConfDataType.IntegerType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Boolean> logScriptOutput =
      new ConfKeyInfo<>(
          "yb.ha.logScriptOutput",
          ScopeType.GLOBAL,
          "Log Script Output For YBA HA Feature",
          "To log backup restore script output for debugging issues",
          ConfDataType.BooleanType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Boolean> useKubectl =
      new ConfKeyInfo<>(
          "yb.use_kubectl",
          ScopeType.GLOBAL,
          "Use Kubectl",
          "Use java library instead of spinning up kubectl process.",
          ConfDataType.BooleanType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  // TODO(): Add correct metadata
  public static final ConfKeyInfo<Boolean> useNewHelmNaming =
      new ConfKeyInfo<>(
          "yb.use_new_helm_naming",
          ScopeType.GLOBAL,
          "Use New Helm Naming",
          "TODO - Leave this for feature owners to fill in",
          ConfDataType.BooleanType,
          ImmutableList.of(ConfKeyTags.INTERNAL));

  public static final ConfKeyInfo<Boolean> usek8sCustomResources =
      new ConfKeyInfo<>(
          "yb.use_k8s_custom_resources",
          ScopeType.GLOBAL,
          "Specify custom CPU/memory values for k8s universes",
          "Use custom CPU/Memory for kubernetes nodes. Once enabled, shouldn't be disabled.",
          ConfDataType.BooleanType,
          ImmutableList.of(ConfKeyTags.INTERNAL));

  public static final ConfKeyInfo<Boolean> useSingleZone =
      new ConfKeyInfo<>(
          "yb.use_single_zone",
          ScopeType.GLOBAL,
          "Use single zone node placement in case of create universe",
          "Use single zone node placement in case of create universe",
          ConfDataType.BooleanType,
          ImmutableList.of(ConfKeyTags.INTERNAL));

  public static final ConfKeyInfo<Boolean> useOauth =
      new ConfKeyInfo<>(
          "yb.security.use_oauth",
          ScopeType.GLOBAL,
          "Use OAUTH",
          "Hidden because this key has dedicated UI",
          ConfDataType.BooleanType,
          ImmutableList.of(ConfKeyTags.INTERNAL));
  public static final ConfKeyInfo<String> ybSecurityType =
      new ConfKeyInfo<>(
          "yb.security.type",
          ScopeType.GLOBAL,
          "YB Security Type",
          "Hidden because this key has dedicated UI",
          ConfDataType.StringType,
          ImmutableList.of(ConfKeyTags.INTERNAL));
  public static final ConfKeyInfo<Boolean> displayJWTToken =
      new ConfKeyInfo<>(
          "yb.security.showJWTInfoOnLogin",
          ScopeType.GLOBAL,
          "Display JWT Token on Login Screen",
          "Display JWT Token on Login Screen",
          ConfDataType.BooleanType,
          ImmutableList.of(ConfKeyTags.INTERNAL));
  public static final ConfKeyInfo<String> ybClientID =
      new ConfKeyInfo<>(
          "yb.security.clientID",
          ScopeType.GLOBAL,
          "YB Client ID",
          "Hidden because this key has dedicated UI",
          ConfDataType.StringType,
          ImmutableList.of(ConfKeyTags.INTERNAL));
  public static final ConfKeyInfo<String> ybSecuritySecret =
      new ConfKeyInfo<>(
          "yb.security.secret",
          ScopeType.GLOBAL,
          "YB Security Secret",
          "Hidden because this key has dedicated UI",
          ConfDataType.StringType,
          ImmutableList.of(ConfKeyTags.INTERNAL));
  public static final ConfKeyInfo<String> discoveryURI =
      new ConfKeyInfo<>(
          "yb.security.discoveryURI",
          ScopeType.GLOBAL,
          "Discovery URI",
          "Hidden because this key has dedicated UI",
          ConfDataType.StringType,
          ImmutableList.of(ConfKeyTags.INTERNAL));
  public static final ConfKeyInfo<String> oidcProviderMetadata =
      new ConfKeyInfo<>(
          "yb.security.oidcProviderMetadata",
          ScopeType.GLOBAL,
          "Provider Metadata from discoveryURI",
          "Hidden because this key has dedicated UI",
          ConfDataType.StringType,
          ImmutableList.of(ConfKeyTags.INTERNAL));
  public static final ConfKeyInfo<String> oidcScope =
      new ConfKeyInfo<>(
          "yb.security.oidcScope",
          ScopeType.GLOBAL,
          "OIDC Scope",
          "Hidden because this key has dedicated UI",
          ConfDataType.StringType,
          ImmutableList.of(ConfKeyTags.INTERNAL));
  public static final ConfKeyInfo<String> oidcEmailAttribute =
      new ConfKeyInfo<>(
          "yb.security.oidcEmailAttribute",
          ScopeType.GLOBAL,
          "OIDC Email Attribute",
          "Hidden because this key has dedicated UI",
          ConfDataType.StringType,
          ImmutableList.of(ConfKeyTags.INTERNAL));
  public static final ConfKeyInfo<Boolean> ssh2Enabled =
      new ConfKeyInfo<>(
          "yb.security.ssh2_enabled",
          ScopeType.GLOBAL,
          "Enable SSH2",
          "Flag for enabling ssh2 on YBA",
          ConfDataType.BooleanType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Boolean> enableCustomHooks =
      new ConfKeyInfo<>(
          "yb.security.custom_hooks.enable_custom_hooks",
          ScopeType.GLOBAL,
          "Enable Custom Hooks",
          "Flag for enabling custom hooks on YBA",
          ConfDataType.BooleanType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Boolean> enableSudo =
      new ConfKeyInfo<>(
          "yb.security.custom_hooks.enable_sudo",
          ScopeType.GLOBAL,
          "Enable SUDO",
          "Flag for enabling sudo access while running custom hooks",
          ConfDataType.BooleanType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Boolean> enabledApiTriggerHooks =
      new ConfKeyInfo<>(
          "yb.security.custom_hooks.enable_api_triggered_hooks",
          ScopeType.GLOBAL,
          "Enable API Triggered Hooks",
          "Flag for enabling API Triggered Hooks on YBA",
          ConfDataType.BooleanType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Boolean> disableXxHashChecksum =
      new ConfKeyInfo<>(
          "yb.backup.disable_xxhash_checksum",
          ScopeType.GLOBAL,
          "Disable XX Hash Checksum",
          "Flag for disabling xxhsum based checksums for computing the backup",
          ConfDataType.BooleanType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  // TODO(): Add correct metadata
  public static final ConfKeyInfo<Boolean> auditVerifyLogging =
      new ConfKeyInfo<>(
          "yb.audit.log.verifyLogging",
          ScopeType.GLOBAL,
          "Audit Verify Logging",
          "TODO - Leave this for feature owners to fill in",
          ConfDataType.BooleanType,
          ImmutableList.of(ConfKeyTags.INTERNAL));
  // TODO(): Add correct metadata
  public static final ConfKeyInfo<Boolean> auditOutputToStdout =
      new ConfKeyInfo<>(
          "yb.audit.log.outputToStdout",
          ScopeType.GLOBAL,
          "Audit Log Output to Stdout",
          "TODO - Leave this for feature owners to fill in",
          ConfDataType.BooleanType,
          ImmutableList.of(ConfKeyTags.INTERNAL));
  // TODO(): Add correct metadata
  public static final ConfKeyInfo<Boolean> auditOutputToFile =
      new ConfKeyInfo<>(
          "yb.audit.log.outputToFile",
          ScopeType.GLOBAL,
          "Audit Log Output to File",
          "TODO - Leave this for feature owners to fill in",
          ConfDataType.BooleanType,
          ImmutableList.of(ConfKeyTags.INTERNAL));
  // TODO(): Add correct metadata
  public static final ConfKeyInfo<String> auditRolloverPattern =
      new ConfKeyInfo<>(
          "yb.audit.log.rolloverPattern",
          ScopeType.GLOBAL,
          "Audit Log Rollover Pattern",
          "TODO - Leave this for feature owners to fill in",
          ConfDataType.StringType,
          ImmutableList.of(ConfKeyTags.INTERNAL));
  // TODO(): Add correct metadata
  public static final ConfKeyInfo<String> auditMaxHistory =
      new ConfKeyInfo<>(
          "yb.audit.log.maxHistory",
          ScopeType.GLOBAL,
          "Audit Log Max History",
          "TODO - Leave this for feature owners to fill in",
          ConfDataType.StringType,
          ImmutableList.of(ConfKeyTags.INTERNAL));
  public static final ConfKeyInfo<String> auditLogFileNamePrefix =
      new ConfKeyInfo<>(
          "yb.audit.log.fileNamePrefix",
          ScopeType.GLOBAL,
          "Audit Log File Name Prefix",
          "Flag to set a custom prefix for the audit log files. "
              + "Can only be set through audit_logging_config API.",
          ConfDataType.StringType,
          ImmutableList.of(ConfKeyTags.INTERNAL));
  public static final ConfKeyInfo<Boolean> supportBundleK8sEnabled =
      new ConfKeyInfo<>(
          "yb.support_bundle.k8s_enabled",
          ScopeType.GLOBAL,
          "Enable K8s Support Bundle",
          "This config lets you enable support bundle creation on k8s universes.",
          ConfDataType.BooleanType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Boolean> supportBundleOnPremEnabled =
      new ConfKeyInfo<>(
          "yb.support_bundle.onprem_enabled",
          ScopeType.GLOBAL,
          "Enable On Prem Support Bundle",
          "This config lets you enable support bundle creation for onprem universes.",
          ConfDataType.BooleanType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Boolean> supportBundleAllowCoresCollection =
      new ConfKeyInfo<>(
          "yb.support_bundle.allow_cores_collection",
          ScopeType.GLOBAL,
          "Allow collection of cores in Support Bundle",
          "This global config allows you to disable collection of cores in support bundle, even if"
              + " it is passed as a component while creating.",
          ConfDataType.BooleanType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Integer> supportBundleNodeCheckTimeoutSec =
      new ConfKeyInfo<>(
          "yb.support_bundle.node_check_timeout_sec",
          ScopeType.GLOBAL,
          "Node reachable check timeout in Support Bundle",
          "This global config is a timeout after which the node is deemed unreachable when creating"
              + " a support bundle.",
          ConfDataType.IntegerType,
          ImmutableList.of(ConfKeyTags.INTERNAL));
  public static final ConfKeyInfo<Integer> snapshotCreationMaxAttempts =
      new ConfKeyInfo<>(
          "yb.snapshot_creation.max_attempts",
          ScopeType.GLOBAL,
          "Snapshot creation max attempts",
          "Max attempts while waiting for AWS Snapshot Creation",
          ConfDataType.IntegerType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Integer> snapshotCreationDelay =
      new ConfKeyInfo<>(
          "yb.snapshot_creation.delay",
          ScopeType.GLOBAL,
          "Snapshot creation delay",
          "Delay per attempt while waiting for AWS Snapshot Creation",
          ConfDataType.IntegerType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Boolean> runtimeConfigUiEnableForAll =
      new ConfKeyInfo<>(
          "yb.runtime_conf_ui.enable_for_all",
          ScopeType.GLOBAL,
          "Runtime Config UI",
          "Allows users to view the runtime configuration properties via UI",
          ConfDataType.BooleanType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Boolean> isPlatformDowngradeAllowed =
      new ConfKeyInfo<>(
          "yb.is_platform_downgrade_allowed",
          ScopeType.GLOBAL,
          "Allow Platform Downgrade",
          "Allow Downgrading the Platform Version",
          ConfDataType.BooleanType,
          ImmutableList.of(ConfKeyTags.INTERNAL));
  public static final ConfKeyInfo<Duration> ybcUpgradeInterval =
      new ConfKeyInfo<>(
          "ybc.upgrade.scheduler_interval",
          ScopeType.GLOBAL,
          "YBC Upgrade Interval",
          "YBC Upgrade interval",
          ConfDataType.DurationType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Integer> ybcUniverseBatchSize =
      new ConfKeyInfo<>(
          "ybc.upgrade.universe_batch_size",
          ScopeType.GLOBAL,
          "YBC Universe Upgrade Batch Size",
          "The number of maximum universes on which ybc will be upgraded simultaneously",
          ConfDataType.IntegerType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Integer> ybcNodeBatchSize =
      new ConfKeyInfo<>(
          "ybc.upgrade.node_batch_size",
          ScopeType.GLOBAL,
          "YBC Node Upgrade Batch Size",
          "The number of maximum nodes on which ybc will be upgraded simultaneously",
          ConfDataType.IntegerType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<String> ybcStableVersion =
      new ConfKeyInfo<>(
          "ybc.releases.stable_version",
          ScopeType.GLOBAL,
          "YBC Stable Release",
          "Stable version for Yb-Controller",
          ConfDataType.StringType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Integer> ybcAdminOperationTimeoutMs =
      new ConfKeyInfo<>(
          "ybc.timeout.admin_operation_timeout_ms",
          ScopeType.GLOBAL,
          "YBC admin operation timeout",
          "YBC client timeout in milliseconds for admin operations",
          ConfDataType.IntegerType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Integer> xclusterDbSyncTimeoutMs =
      new ConfKeyInfo<>(
          "yb.xcluster.db_sync_timeout_ms",
          ScopeType.GLOBAL,
          "XCluster config DB sync timeout",
          "XCluster config background DB sync timeout in milliseconds",
          ConfDataType.IntegerType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Integer> xclusterGetApiTimeoutMs =
      new ConfKeyInfo<>(
          "yb.xcluster.get_api_timeout_ms",
          ScopeType.GLOBAL,
          "XCluster/DR config GET API timeout",
          "XCluster/DR config GET API timeout in milliseconds",
          ConfDataType.IntegerType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Integer> ybcSocketReadTimeoutMs =
      new ConfKeyInfo<>(
          "ybc.timeout.socket_read_timeout_ms",
          ScopeType.GLOBAL,
          "YBC socket read timeout",
          "YBC client socket read timeout in milliseconds",
          ConfDataType.IntegerType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Integer> ybcOperationTimeoutMs =
      new ConfKeyInfo<>(
          "ybc.timeout.operation_timeout_ms",
          ScopeType.GLOBAL,
          "YBC operation timeout",
          "YBC client timeout in milliseconds for operations",
          ConfDataType.IntegerType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Boolean> enforceCertVerificationBackupRestore =
      new ConfKeyInfo<>(
          "yb.certVerifyBackupRestore.is_enforced",
          ScopeType.GLOBAL,
          "Server certificate verification for S3 backup/restore",
          "Enforce server certificate verification during S3 backup/restore",
          ConfDataType.BooleanType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<String> javaxNetSslTrustStore =
      new ConfKeyInfo<>(
          "yb.wellKnownCA.trustStore.path",
          ScopeType.GLOBAL,
          "Javax Net SSL TrustStore",
          "Java property javax.net.ssl.trustStore",
          ConfDataType.StringType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<String> javaxNetSslTrustStoreType =
      new ConfKeyInfo<>(
          "yb.wellKnownCA.trustStore.type",
          ScopeType.GLOBAL,
          "Javax Net SSL TrustStore Type",
          "Java property javax.net.ssl.trustStoreType",
          ConfDataType.StringType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<String> javaxNetSslTrustStorePassword =
      new ConfKeyInfo<>(
          "yb.wellKnownCA.trustStore.password",
          ScopeType.GLOBAL,
          "Javax Net SSL TrustStore Password",
          "Java property javax.net.ssl.trustStorePassword",
          ConfDataType.StringType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Boolean> enableCertReload =
      new ConfKeyInfo<>(
          "yb.features.cert_reload.enabled",
          ScopeType.GLOBAL,
          "Enable Cert Reload",
          "Enable hot reload of TLS certificates without restart of the DB nodes",
          ConfDataType.BooleanType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<String> orgNameSelfSignedCert =
      new ConfKeyInfo<>(
          "yb.tlsCertificate.organizationName",
          ScopeType.GLOBAL,
          "Organization name for self signed certificates",
          "Specify an organization name for self signed certificates",
          ConfDataType.StringType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Boolean> cmdOutputDelete =
      new ConfKeyInfo<>(
          "yb.logs.cmdOutputDelete",
          ScopeType.GLOBAL,
          "Delete Output File",
          "Flag to delete temp output file created by the shell command",
          ConfDataType.BooleanType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Integer> shellOutputRetentationHours =
      new ConfKeyInfo<>(
          "yb.logs.shell.output_retention_hours",
          ScopeType.GLOBAL,
          "Shell Output Retention Duration",
          "Output logs for shell commands are written to tmp folder."
              + "This setting defines how long will we wait before garbage collecting them.",
          ConfDataType.IntegerType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Long> shellMaxOutputDirSize =
      new ConfKeyInfo<>(
          "yb.logs.shell.output_dir_max_size",
          ScopeType.GLOBAL,
          "Shell Output Max Directory Size",
          "Output logs for shell commands are written to tmp folder."
              + "This setting defines rotation policy based on directory size.",
          ConfDataType.BytesType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Long> logsMaxMsgSize =
      new ConfKeyInfo<>(
          "yb.logs.max_msg_size",
          ScopeType.GLOBAL,
          "Max Size of each log message",
          "We limit the length of each log line as sometimes we dump entire output"
              + " of script. If you want to debug something specific and the script output is"
              + "getting truncated in application log then increase this limit",
          ConfDataType.BytesType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Duration> kmsRefreshInterval =
      new ConfKeyInfo<>(
          "yb.kms.refresh_interval",
          ScopeType.GLOBAL,
          "KMS Refresh Interval",
          "Default refresh interval for the KMS providers.",
          ConfDataType.DurationType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  // TODO() Add metadata
  public static final ConfKeyInfo<Boolean> startMasterOnStopNode =
      new ConfKeyInfo<>(
          "yb.start_master_on_stop_node",
          ScopeType.GLOBAL,
          "Start Master On Stop Node",
          "Auto-start master process on a similar available node on stopping a master node",
          ConfDataType.BooleanType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Boolean> startMasterOnRemoveNode =
      new ConfKeyInfo<>(
          "yb.start_master_on_remove_node",
          ScopeType.GLOBAL,
          "Start Master On Remove Node",
          "Auto-start master process on a similar available node on removal of a master node",
          ConfDataType.BooleanType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static ConfKeyInfo<Boolean> useLdap =
      new ConfKeyInfo<>(
          "yb.security.ldap.use_ldap",
          ScopeType.GLOBAL,
          "Use LDAP",
          "Hidden because this key has dedicated UI",
          ConfDataType.BooleanType,
          ImmutableList.of(ConfKeyTags.INTERNAL));
  public static ConfKeyInfo<String> ldapUrl =
      new ConfKeyInfo<>(
          "yb.security.ldap.ldap_url",
          ScopeType.GLOBAL,
          "LDAP URL",
          "Hidden because this key has dedicated UI",
          ConfDataType.StringType,
          ImmutableList.of(ConfKeyTags.INTERNAL));
  public static ConfKeyInfo<String> ldapPort =
      new ConfKeyInfo<>(
          "yb.security.ldap.ldap_port",
          ScopeType.GLOBAL,
          "LDAP Port",
          "Hidden because this key has dedicated UI",
          ConfDataType.StringType,
          ImmutableList.of(ConfKeyTags.INTERNAL));
  public static ConfKeyInfo<String> ldapBaseDn =
      new ConfKeyInfo<>(
          "yb.security.ldap.ldap_basedn",
          ScopeType.GLOBAL,
          "LDAP Base DN",
          "Hidden because this key has dedicated UI",
          ConfDataType.StringType,
          ImmutableList.of(ConfKeyTags.INTERNAL));
  public static ConfKeyInfo<String> ldapDnPrefix =
      new ConfKeyInfo<>(
          "yb.security.ldap.ldap_dn_prefix",
          ScopeType.GLOBAL,
          "LDAP DN Prefix",
          "Hidden because this key has dedicated UI",
          ConfDataType.StringType,
          ImmutableList.of(ConfKeyTags.INTERNAL));
  public static ConfKeyInfo<String> ldapCustomerUUID =
      new ConfKeyInfo<>(
          "yb.security.ldap.ldap_customeruuid",
          ScopeType.GLOBAL,
          "LDAP Customer UUID",
          "Hidden because this key has dedicated UI",
          ConfDataType.StringType,
          ImmutableList.of(ConfKeyTags.INTERNAL));
  public static ConfKeyInfo<String> ldapServiceAccountDistinguishedName =
      new ConfKeyInfo<>(
          "yb.security.ldap.ldap_service_account_distinguished_name",
          ScopeType.GLOBAL,
          "LDAP Service Account Username",
          "Hidden because this key has dedicated UI",
          ConfDataType.StringType,
          ImmutableList.of(ConfKeyTags.INTERNAL));
  public static ConfKeyInfo<String> ldapServiceAccountPassword =
      new ConfKeyInfo<>(
          "yb.security.ldap.ldap_service_account_password",
          ScopeType.GLOBAL,
          "LDAP Service Account Password",
          "Hidden because this key has dedicated UI",
          ConfDataType.StringType,
          ImmutableList.of(ConfKeyTags.INTERNAL));
  public static ConfKeyInfo<Boolean> enableLdap =
      new ConfKeyInfo<>(
          "yb.security.ldap.enable_ldaps",
          ScopeType.GLOBAL,
          "Enable LDAPS",
          "TODO - Leave this for feature owners to fill in",
          ConfDataType.BooleanType,
          ImmutableList.of(ConfKeyTags.INTERNAL));
  public static ConfKeyInfo<Boolean> enableLdapStartTls =
      new ConfKeyInfo<>(
          "yb.security.ldap.enable_ldap_start_tls",
          ScopeType.GLOBAL,
          "Enable LDAPS start TLS",
          "Hidden because this key has dedicated UI",
          ConfDataType.BooleanType,
          ImmutableList.of(ConfKeyTags.INTERNAL));
  public static ConfKeyInfo<Boolean> ldapUseSearchAndBind =
      new ConfKeyInfo<>(
          "yb.security.ldap.use_search_and_bind",
          ScopeType.GLOBAL,
          "Use Search and Bind",
          "Hidden because this key has dedicated UI",
          ConfDataType.BooleanType,
          ImmutableList.of(ConfKeyTags.INTERNAL));
  public static ConfKeyInfo<String> ldapSearchAttribute =
      new ConfKeyInfo<>(
          "yb.security.ldap.ldap_search_attribute",
          ScopeType.GLOBAL,
          "LDAP Search Attribute",
          "Hidden because this key has dedicated UI",
          ConfDataType.StringType,
          ImmutableList.of(ConfKeyTags.INTERNAL));
  public static ConfKeyInfo<String> ldapGroupSearchFilter =
      new ConfKeyInfo<>(
          "yb.security.ldap.ldap_group_search_filter",
          ScopeType.GLOBAL,
          "LDAP Group Search Filter Query",
          "Hidden because this key has dedicated UI",
          ConfDataType.StringType,
          ImmutableList.of(ConfKeyTags.INTERNAL));
  public static ConfKeyInfo<SearchScope> ldapGroupSearchScope =
      new ConfKeyInfo<>(
          "yb.security.ldap.ldap_group_search_scope",
          ScopeType.GLOBAL,
          "LDAP group search scope in case of filter query",
          "Hidden because this key has dedicated UI",
          ConfDataType.LdapSearchScopeEnum,
          ImmutableList.of(ConfKeyTags.INTERNAL));
  public static ConfKeyInfo<String> ldapGroupSearchBaseDn =
      new ConfKeyInfo<>(
          "yb.security.ldap.ldap_group_search_base_dn",
          ScopeType.GLOBAL,
          "LDAP group search base DN in case of filter query",
          "Hidden because this key has dedicated UI",
          ConfDataType.StringType,
          ImmutableList.of(ConfKeyTags.INTERNAL));
  public static ConfKeyInfo<String> ldapGroupMemberOfAttribute =
      new ConfKeyInfo<>(
          "yb.security.ldap.ldap_group_member_of_attribute",
          ScopeType.GLOBAL,
          "memberOf attribute in user LDAP entry to be used for group memberships",
          "Hidden because this key has dedicated UI",
          ConfDataType.StringType,
          ImmutableList.of(ConfKeyTags.INTERNAL));
  public static ConfKeyInfo<Boolean> ldapGroupUseQuery =
      new ConfKeyInfo<>(
          "yb.security.ldap.ldap_group_use_query",
          ScopeType.GLOBAL,
          "Whether to use query search filter or user attribute "
              + "for establishing LDAP group membership",
          "Hidden because this key has dedicated UI",
          ConfDataType.BooleanType,
          ImmutableList.of(ConfKeyTags.INTERNAL));
  public static ConfKeyInfo<Boolean> ldapGroupUseRoleMapping =
      new ConfKeyInfo<>(
          "yb.security.ldap.ldap_group_use_role_mapping",
          ScopeType.GLOBAL,
          "Whether to use ldap group to role mapping",
          "Hidden because this key has dedicated UI",
          ConfDataType.BooleanType,
          ImmutableList.of(ConfKeyTags.INTERNAL));
  public static ConfKeyInfo<Role> ldapDefaultRole =
      new ConfKeyInfo<>(
          "yb.security.ldap.ldap_default_role",
          ScopeType.GLOBAL,
          "LDAP Default Role",
          "Which role to use in case role cannot be discerned via LDAP",
          ConfDataType.UserRoleEnum,
          ImmutableList.of(ConfKeyTags.INTERNAL));
  public static ConfKeyInfo<TlsProtocol> ldapTlsProtocol =
      new ConfKeyInfo<>(
          "yb.security.ldap.ldap_tls_protocol",
          ScopeType.GLOBAL,
          "Which TLS protocol to use for StartTLS or LDAPS",
          "Hidden because this key has dedicated UI",
          ConfDataType.LdapTlsProtocol,
          ImmutableList.of(ConfKeyTags.INTERNAL));
  public static ConfKeyInfo<Boolean> ldapsEnforceCertVerification =
      new ConfKeyInfo<>(
          "yb.security.ldap.enforce_server_cert_verification",
          ScopeType.GLOBAL,
          "Server certificate verification for LDAPs/LDAP-TLS",
          "Enforce server certificate verification for LDAPs/LDAP-TLS",
          ConfDataType.BooleanType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static ConfKeyInfo<Integer> ldapPageQuerySize =
      new ConfKeyInfo<>(
          "yb.security.ldap.page_query_size",
          ScopeType.GLOBAL,
          "Pagination query size for LDAP server",
          "Pagination query size for LDAP server",
          ConfDataType.IntegerType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static ConfKeyInfo<Boolean> enableDetailedLogs =
      new ConfKeyInfo<>(
          "yb.security.enable_detailed_logs",
          ScopeType.GLOBAL,
          "Enable Detailed Logs",
          "Enable detailed security logs",
          ConfDataType.BooleanType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static ConfKeyInfo<Integer> maxVolumeCount =
      new ConfKeyInfo<>(
          "yb.max_volume_count",
          ScopeType.GLOBAL,
          "Maximum Volume Count",
          "Maximum Volume Count",
          ConfDataType.IntegerType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static ConfKeyInfo<Boolean> fsStatelessSuppressError =
      new ConfKeyInfo<>(
          "yb.fs_stateless.suppress_error",
          ScopeType.GLOBAL,
          "Supress Error",
          "If set, suppresses exceptions to be thrown as part of FS <-> DB sync on YBA startup",
          ConfDataType.BooleanType,
          ImmutableList.of(ConfKeyTags.INTERNAL));
  public static ConfKeyInfo<Long> fsStatelessMaxFileSizeBytes =
      new ConfKeyInfo<>(
          "yb.fs_stateless.max_file_size_bytes",
          ScopeType.GLOBAL,
          "Max File Size",
          "Maximum size of file that can be persisted in DB",
          ConfDataType.LongType,
          ImmutableList.of(ConfKeyTags.INTERNAL));
  public static ConfKeyInfo<Integer> fsStatelessMaxFilesCountPersist =
      new ConfKeyInfo<>(
          "yb.fs_stateless.max_files_count_persist",
          ScopeType.GLOBAL,
          "Max Files Persist",
          "Maximum number of files that can be persisted in DB",
          ConfDataType.IntegerType,
          ImmutableList.of(ConfKeyTags.INTERNAL));
  public static ConfKeyInfo<Boolean> disableSyncDbToFsStartup =
      new ConfKeyInfo<>(
          "yb.fs_stateless.disable_sync_db_to_fs_startup",
          ScopeType.GLOBAL,
          "Sync DB State to FS",
          "If disables does not syncs the files in DB to FS on every YBA startup",
          ConfDataType.BooleanType,
          ImmutableList.of(ConfKeyTags.INTERNAL));
  public static ConfKeyInfo<Duration> taskGcCheckInterval =
      new ConfKeyInfo<>(
          "yb.taskGC.gc_check_interval",
          ScopeType.GLOBAL,
          "Task Garbage Collector Check Interval",
          "How frequently do we check for completed tasks in database",
          ConfDataType.DurationType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<List> tagList =
      new ConfKeyInfo<>(
          "yb.runtime_conf_ui.tag_filter",
          ScopeType.GLOBAL,
          "UI Tag Filters",
          "List of tags to filter which keys are displayed",
          ConfDataType.TagListType,
          ImmutableList.of(ConfKeyTags.INTERNAL));
  public static final ConfKeyInfo<Boolean> dataValidationEnabled =
      new ConfKeyInfo<>(
          "runtime_config.data_validation.enabled",
          ScopeType.GLOBAL,
          "Enable Data Validation",
          "Enable data validation while setting runtime keys",
          ConfDataType.BooleanType,
          ImmutableList.of(ConfKeyTags.INTERNAL));
  public static final ConfKeyInfo<Boolean> scopeStrictnessEnabled =
      new ConfKeyInfo<>(
          "runtime_config.scope_strictness.enabled",
          ScopeType.GLOBAL,
          "Enable Scope Strictness",
          "Enable scope strictness while setting runtime keys",
          ConfDataType.BooleanType,
          ImmutableList.of(ConfKeyTags.INTERNAL));
  public static final ConfKeyInfo<Duration> nodeAgentPollerInterval =
      new ConfKeyInfo<>(
          "yb.node_agent.poller_interval",
          ScopeType.GLOBAL,
          "Node Agent Poller Interval",
          "Node agent poller interval",
          ConfDataType.DurationType,
          ImmutableList.of(ConfKeyTags.INTERNAL));
  public static final ConfKeyInfo<Duration> deadNodeAgentRetention =
      new ConfKeyInfo<>(
          "yb.node_agent.retention_duration",
          ScopeType.GLOBAL,
          "Dead Node Agent Retention Duration",
          "Retention duration for a dead node agent before deletion",
          ConfDataType.DurationType,
          ImmutableList.of(ConfKeyTags.INTERNAL));
  public static final ConfKeyInfo<Boolean> backwardCompatibleDate =
      new ConfKeyInfo<>(
          "yb.api.backward_compatible_date",
          ScopeType.GLOBAL,
          "API support for backward compatible date fields",
          "Enable when a client to the YBAnywhere API wants to continue using the older date "
              + " fields in non-ISO format. Default behaviour is to not populate such deprecated "
              + "API fields and only return newer date fields.",
          ConfDataType.BooleanType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Boolean> attachDetachEnabled =
      new ConfKeyInfo<>(
          "yb.attach_detach.enabled",
          ScopeType.GLOBAL,
          "Allow universes to be detached/attached",
          "Allow universes to be detached from a source platform and attached to dest platform",
          ConfDataType.BooleanType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Boolean> transactionalXClusterEnabled =
      new ConfKeyInfo<>(
          "yb.xcluster.transactional.enabled",
          ScopeType.GLOBAL,
          "Whether YBA supports transactional xCluster configs",
          "It indicates whether YBA should support transactional xCluster configs",
          ConfDataType.BooleanType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Boolean> disasterRecoveryEnabled =
      new ConfKeyInfo<>(
          "yb.xcluster.dr.enabled",
          ScopeType.GLOBAL,
          "Enable disaster recovery",
          "It indicates whether creating disaster recovery configs are enabled",
          ConfDataType.BooleanType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Boolean> xclusterEnableAutoFlagValidation =
      new ConfKeyInfo<>(
          "yb.xcluster.enable_auto_flag_validation",
          ScopeType.GLOBAL,
          "Enable xcluster/DR auto flag validation",
          "Enables checks for xcluster/disaster recovery validations for autoflags for xcluster/DR"
              + " operations",
          ConfDataType.BooleanType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Boolean> enableYbcForXCluster =
      new ConfKeyInfo<>(
          "yb.xcluster.use_ybc",
          ScopeType.GLOBAL,
          "Enable YBC for xCluster",
          "Enable YBC to take backup and restore during xClsuter bootstrap",
          ConfDataType.BooleanType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Boolean> allowDbVersionMoreThanYbaVersion =
      new ConfKeyInfo<>(
          "yb.allow_db_version_more_than_yba_version",
          ScopeType.GLOBAL,
          "Whether installation of YugabyteDB version higher than YBA version is allowed",
          "It indicates whether the installation of YugabyteDB with a version higher than "
              + "YBA version is allowed on universe nodes",
          ConfDataType.BooleanType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Boolean> skipVersionChecks =
      new ConfKeyInfo<>(
          "yb.skip_version_checks",
          ScopeType.GLOBAL,
          "Skip DB / YBA version comparison checks",
          "Whether we should skip DB / YBA version comparison checks during upgrades, etc. "
              + "Gives more flexibilty, but user should be careful when enabling this.",
          ConfDataType.BooleanType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<String> pgDumpPath =
      new ConfKeyInfo<>(
          "db.default.pg_dump_path",
          ScopeType.GLOBAL,
          "Path to pg_dump on the YBA node",
          "Set during yba-installer for both custom postgres and version specific postgres "
              + "installation",
          ConfDataType.StringType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<String> pgRestorePath =
      new ConfKeyInfo<>(
          "db.default.pg_restore_path",
          ScopeType.GLOBAL,
          "Path to pg_restore on the YBA node",
          "Set during yba-installer for both custom postgres and version specific postgres "
              + "installation",
          ConfDataType.StringType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<String> ybdbReleasePathRegex =
      new ConfKeyInfo<>(
          "yb.regex.release_pattern.ybdb",
          ScopeType.GLOBAL,
          "Regex for match Yugabyte DB release .tar.gz files",
          "Regex pattern used to find Yugabyte DB release .tar.gz files",
          ConfDataType.StringType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<String> ybdbHelmReleasePathRegex =
      new ConfKeyInfo<>(
          "yb.regex.release_pattern.helm",
          ScopeType.GLOBAL,
          "Regex for match Yugabyte DB release helm .tar.gz files",
          "Regex pattern used to find Yugabyte DB helm .tar.gz files",
          ConfDataType.StringType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Boolean> enableTaskAndFailedRequestDetailedLogging =
      new ConfKeyInfo<>(
          "yb.logging.enable_task_failed_request_logs",
          ScopeType.GLOBAL,
          "Enables extra logging",
          "Enables extra logging for task params and request body",
          ConfDataType.BooleanType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<String> applicationLogFileNamePrefix =
      new ConfKeyInfo<>(
          "yb.logging.fileNamePrefix",
          ScopeType.GLOBAL,
          "Application Log File Name Prefix",
          "Flag to set a custom prefix for the application log files. "
              + "Can only be set through logging_config API.",
          ConfDataType.StringType,
          ImmutableList.of(ConfKeyTags.INTERNAL));
  public static final ConfKeyInfo<Boolean> fixDatabaseFullPaths =
      new ConfKeyInfo<>(
          "yb.fixPaths",
          ScopeType.GLOBAL,
          "Whether YBA should fix paths on startup",
          "When enabled YBA will try to replace all filepaths in the database with updated values "
              + "for the configurable part of the path (like storage or releases path)",
          ConfDataType.BooleanType,
          ImmutableList.of(ConfKeyTags.INTERNAL));
  public static final ConfKeyInfo<Integer> awsDiskResizeCooldownHours =
      new ConfKeyInfo<>(
          "yb.aws.disk_resize_cooldown_hours",
          ScopeType.GLOBAL,
          "Cooldown after disk resize in aws (in hours)",
          "Cooldown after disk resize in aws (in hours)",
          ConfDataType.IntegerType,
          ImmutableList.of(ConfKeyTags.INTERNAL));
  public static final ConfKeyInfo<String> ybTmpDirectoryPath =
      new ConfKeyInfo<>(
          "yb.filepaths.tmpDirectory",
          ScopeType.GLOBAL,
          "tmp directory path",
          "Path to the tmp directory to be used by YBA",
          ConfDataType.StringType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Duration> nodeAgentTokenLifetime =
      new ConfKeyInfo<>(
          "yb.node_agent.client.token_lifetime",
          ScopeType.GLOBAL,
          "Node Agent Token Lifetime",
          "Lifetime of token used by node agent clients",
          ConfDataType.DurationType,
          ImmutableList.of(ConfKeyTags.INTERNAL));
  public static final ConfKeyInfo<Integer> nodeAgentServerPort =
      new ConfKeyInfo<>(
          "yb.node_agent.server.port",
          ScopeType.GLOBAL,
          "Node Agent Server Port",
          "Listening port for node agent servers",
          ConfDataType.IntegerType,
          ImmutableList.of(ConfKeyTags.INTERNAL));
  public static final ConfKeyInfo<String> nodeAgentInstallPath =
      new ConfKeyInfo<>(
          "yb.node_agent.server.install_path",
          ScopeType.GLOBAL,
          "Node Agent Server Installation Path",
          "Installation path for node agent",
          ConfDataType.StringType,
          ImmutableList.of(ConfKeyTags.INTERNAL));
  public static final ConfKeyInfo<Integer> waitForProviderTasksTimeoutMs =
      new ConfKeyInfo<>(
          "yb.edit_provider.new.wait_for_tasks_timeout_ms",
          ScopeType.GLOBAL,
          "Provider edit wait timeout in milliseconds",
          "Timeout, that is used in edit provider task to wait until concurrent"
              + " provider tasks are completed",
          ConfDataType.IntegerType,
          ImmutableList.of(ConfKeyTags.INTERNAL));
  public static final ConfKeyInfo<Integer> waitForProviderTasksStepMs =
      new ConfKeyInfo<>(
          "yb.edit_provider.new.wait_for_tasks_step_ms",
          ScopeType.GLOBAL,
          "Provider edit wait step in milliseconds",
          "Time between checks, that is used in edit provider task to wait until"
              + " concurrent provider tasks are completed ",
          ConfDataType.IntegerType,
          ImmutableList.of(ConfKeyTags.INTERNAL));
  public static final ConfKeyInfo<String> KubernetesOperatorCustomerUUID =
      new ConfKeyInfo<>(
          "yb.kubernetes.operator.customer_uuid",
          ScopeType.GLOBAL,
          "Customer UUID to use with Kubernentes Operator",
          "Customer UUID to use with Kubernentes Operator, do not change once set",
          ConfDataType.StringType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Boolean> KubernetesOperatorEnabled =
      new ConfKeyInfo<>(
          "yb.kubernetes.operator.enabled",
          ScopeType.GLOBAL,
          "Enable Kubernentes Operator",
          "Enable Kubernentes Operator, requires restart to take effect",
          ConfDataType.BooleanType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<String> KubernetesOperatorNamespace =
      new ConfKeyInfo<>(
          "yb.kubernetes.operator.namespace",
          ScopeType.GLOBAL,
          "Change the namespace kubernetes operator listens on",
          "Change the namespace kubernetes operator listens on. By default, all namespaces "
              + "are watched. Requires restart to take effect",
          ConfDataType.StringType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Boolean> acceptableClockSkewWaitEnabled =
      new ConfKeyInfo<>(
          "yb.wait_for_clock_sync.inline_enabled",
          ScopeType.GLOBAL,
          "Whether to wait for clock skew decrease",
          "Whether to wait for the clock skew to go below the threshold before starting the "
              + "master and tserver processes",
          ConfDataType.BooleanType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Duration> waitForClockSyncMaxAcceptableClockSkew =
      new ConfKeyInfo<>(
          "yb.wait_for_clock_sync.max_acceptable_clock_skew",
          ScopeType.GLOBAL,
          "Max acceptable clock skew on the DB nodes",
          "The maximum clock skew on the DB nodes before the tserver or master process can"
              + "start",
          ConfDataType.DurationType,
          ImmutableList.of(ConfKeyTags.INTERNAL));
  public static final ConfKeyInfo<Duration> waitForClockSyncTimeout =
      new ConfKeyInfo<>(
          "yb.wait_for_clock_sync.timeout",
          ScopeType.GLOBAL,
          "Timeout for the waitForClockSync subtask",
          "The amount of time that the waitForClockSync subtask waits for the clocks of the "
              + "DB nodes to sync; if the sync does not happen within this the time specified in "
              + "this config value, the subtask will fail",
          ConfDataType.DurationType,
          ImmutableList.of(ConfKeyTags.INTERNAL));
  public static ConfKeyInfo<Boolean> editProviderWaitForTasks =
      new ConfKeyInfo<>(
          "yb.edit_provider.new.allow_wait",
          ScopeType.GLOBAL,
          "Enable wait for autotasks to finish during provider edit",
          "Enable wait for autotasks to finish during provider edit",
          ConfDataType.BooleanType,
          ImmutableList.of(ConfKeyTags.INTERNAL));
  public static final ConfKeyInfo<Integer> deleteExpiredBackupMaxGCSize =
      new ConfKeyInfo<>(
          "yb.backup.delete_expired_backup_max_gc_size",
          ScopeType.GLOBAL,
          "Delete Expired Backup MAX GC Size",
          "Number of expired backups to be deleted in a single GC iteration.",
          ConfDataType.IntegerType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<String> metricsExternalUrl =
      new ConfKeyInfo<>(
          "yb.metrics.external.url",
          ScopeType.GLOBAL,
          "Prometheus external URL",
          "URL used to generate Prometheus metrics on YBA UI and to set up HA metrics federation.",
          ConfDataType.StringType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Boolean> metricsLinkUseBrowserFqdn =
      new ConfKeyInfo<>(
          "yb.metrics.link.use_browser_fqdn",
          ScopeType.GLOBAL,
          "Prometheus link use browser FQDN",
          "If Prometheus link in browser should point to current FQDN in browser"
              + " or use value from backend.",
          ConfDataType.BooleanType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Boolean> oidcFeatureEnhancements =
      new ConfKeyInfo<>(
          "yb.security.oidc_feature_enhancements",
          ScopeType.GLOBAL,
          "OIDC feature enhancements",
          "Enables the OIDC enhancements such as auth_token retrieval, user registration in YBA"
              + " on login, etc.",
          ConfDataType.BooleanType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Boolean> installLocalesDbNodes =
      new ConfKeyInfo<>(
          "yb.install_locales_db_nodes",
          ScopeType.GLOBAL,
          "Install Locales DB nodes",
          "If enabled YBA will install locales on the DB nodes",
          ConfDataType.BooleanType,
          ImmutableList.of(ConfKeyTags.INTERNAL));
  public static final ConfKeyInfo<Boolean> disableImageBundleValidation =
      new ConfKeyInfo<>(
          "yb.edit_provider.new.disable_image_bundle_validations",
          ScopeType.GLOBAL,
          "Image Bundle Validation",
          "Disables Image Bundle Validation",
          ConfDataType.BooleanType,
          ImmutableList.of(ConfKeyTags.INTERNAL));
  public static final ConfKeyInfo<Duration> devopsCommandTimeout =
      new ConfKeyInfo<>(
          "yb.devops.command_timeout",
          ScopeType.GLOBAL,
          "Devops command timeout",
          "Devops command timeout",
          ConfDataType.DurationType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Duration> destroyServerCommandTimeout =
      new ConfKeyInfo<>(
          "yb.node_ops.destroy_server_timeout",
          ScopeType.GLOBAL,
          "Node destroy command timeout",
          "Timeout for node destroy command before failing.",
          ConfDataType.DurationType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<String> ybcCompatibleDbVersion =
      new ConfKeyInfo<>(
          "ybc.compatible_db_version",
          ScopeType.GLOBAL,
          "YBC Compatible DB Version",
          "Minimum YBDB version which supports YBC",
          ConfDataType.StringType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Boolean> metricsAuth =
      new ConfKeyInfo<>(
          "yb.metrics.auth",
          ScopeType.GLOBAL,
          "Prometheus auth enabled",
          "Enables basic authentication for Prometheus web UI/APIs access",
          ConfDataType.BooleanType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<String> metricsAuthUsername =
      new ConfKeyInfo<>(
          "yb.metrics.auth_username",
          ScopeType.GLOBAL,
          "Prometheus auth username",
          "Username, used for request authentication against embedded Prometheus",
          ConfDataType.StringType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<String> metricsAuthPassword =
      new ConfKeyInfo<>(
          "yb.metrics.auth_password",
          ScopeType.GLOBAL,
          "Prometheus auth password",
          "Password, used for request authentication against embedded Prometheus",
          ConfDataType.StringType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Boolean> forceYbcShutdownDuringUpgrade =
      new ConfKeyInfo<>(
          "ybc.upgrade.force_shutdown",
          ScopeType.GLOBAL,
          "Force YBC Shutdown during upgrade",
          "For YBC Shutdown during upgrade",
          ConfDataType.BooleanType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Boolean> useNewRbacAuthz =
      new ConfKeyInfo<>(
          "yb.rbac.use_new_authz",
          ScopeType.GLOBAL,
          "New RBAC Authz feature",
          "New RBAC Authz feature with custom role creation",
          ConfDataType.BooleanType,
          ImmutableList.of(ConfKeyTags.INTERNAL, ConfKeyTags.FEATURE_FLAG));
  public static final ConfKeyInfo<Boolean> enableVMOSPatching =
      new ConfKeyInfo<>(
          "yb.provider.vm_os_patching",
          ScopeType.GLOBAL,
          "VM OS Patching",
          "Enables VM OS Patching with image bundles",
          ConfDataType.BooleanType,
          ImmutableList.of(ConfKeyTags.INTERNAL));
  public static final ConfKeyInfo<Boolean> ybaApiStrictMode =
      new ConfKeyInfo<>(
          "yb.api.mode.strict",
          ScopeType.GLOBAL,
          "Enable strict mode to ignore deprecated YBA APIs",
          "Will ignore deprecated APIs",
          ConfDataType.BooleanType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Boolean> ybaApiSafeMode =
      new ConfKeyInfo<>(
          "yb.api.mode.safe",
          ScopeType.GLOBAL,
          "Enable safe mode to ignore preview YBA APIs",
          "Will ignore preview APIs",
          ConfDataType.BooleanType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Boolean> ybcDiagThreadDumpsGCSEnabled =
      new ConfKeyInfo<>(
          "yb.diag.thread_dumps.gcs.enabled",
          ScopeType.GLOBAL,
          "Enable publishing thread dumps to GCS",
          "Enable publishing thread dumps to GCS",
          ConfDataType.BooleanType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Boolean> blockOperatorApiResources =
      new ConfKeyInfo<>(
          "yb.kubernetes.operator.block_api_operator_owned_resources",
          ScopeType.GLOBAL,
          "Operator owned resources api block",
          "A resource controlled by the kubernetes operator cannot be updated using the REST API",
          ConfDataType.BooleanType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<List> devTagsMap =
      new ConfKeyInfo<>(
          "yb.universe.user_tags.dev_tags",
          ScopeType.GLOBAL,
          "Default Dev Tags List",
          "A list of default dev tags during universe creation. "
              + " Ex: [\"yb_task:dev\",\"yb_owner:dev\",\"yb_dept:eng\"]",
          ConfDataType.StringListType,
          ImmutableList.of(ConfKeyTags.INTERNAL));
  public static final ConfKeyInfo<Boolean> granularMetrics =
      new ConfKeyInfo<>(
          "yb.ui.feature_flags.granular_metrics",
          ScopeType.GLOBAL,
          "Granular level metrics",
          "View granular level metrics when user selects specific time period in a chart",
          ConfDataType.BooleanType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Boolean> gflagMultilineConf =
      new ConfKeyInfo<>(
          "yb.ui.feature_flags.gflag_multiline_conf",
          ScopeType.GLOBAL,
          "Enable multiline option for GFlag conf.",
          "Allows user to enter postgres hba rules and ident map rules in multiple rows",
          ConfDataType.BooleanType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Duration> replicationFrequency =
      new ConfKeyInfo<>(
          "yb.ha.replication_frequency",
          ScopeType.GLOBAL,
          "Replication frequency",
          "Replication frequency",
          ConfDataType.DurationType,
          ImmutableList.of(ConfKeyTags.INTERNAL));
  public static final ConfKeyInfo<Boolean> haDisableCertHostValidation =
      new ConfKeyInfo<>(
          "yb.ha.ws.ssl.loose.disableHostnameVerification",
          ScopeType.GLOBAL,
          "Disable hostname cert validation for HA communication",
          "When set, the hostname in https certs will not be validated for HA communication."
              + " Communication will still be encrypted.",
          ConfDataType.BooleanType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Duration> haTestConnectionRequestTimeout =
      new ConfKeyInfo<>(
          "yb.ha.test_request_timeout",
          ScopeType.GLOBAL,
          "HA test connection request timeout",
          "The request to test HA connection to standby will timeout after the specified amount of"
              + " time.",
          ConfDataType.DurationType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Duration> haTestConnectionConnectionTimeout =
      new ConfKeyInfo<>(
          "yb.ha.test_connection_timeout",
          ScopeType.GLOBAL,
          "HA test connection connection timeout",
          "The client will wait for the specified amount of time to make a connection to the remote"
              + " address.",
          ConfDataType.DurationType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Boolean> KubernetesOperatorCrashYbaOnOperatorFail =
      new ConfKeyInfo<>(
          "yb.kubernetes.operator.crash_yba_on_operator_failure",
          ScopeType.GLOBAL,
          "Crash YBA if Kubernentes Operator thread fails",
          "If Kubernetes Operator thread fails, crash YBA",
          ConfDataType.BooleanType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Integer> xclusterBootstrapRequiredRpcMaxThreads =
      new ConfKeyInfo<>(
          "yb.xcluster.is_bootstrap_required_rpc_pool.max_threads",
          ScopeType.GLOBAL,
          "XCluster isBootstrapRequired rpc max parallel threads",
          "Sets the maximum allowed number of threads to be run concurrently for xcluster"
              + " isBootstrapRequired rpc",
          ConfDataType.IntegerType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Boolean> enableOidcAutoCreateUser =
      new ConfKeyInfo<>(
          "yb.security.oidc_enable_auto_create_users",
          ScopeType.GLOBAL,
          "Auto create user on SSO login",
          "Enable user creation on SSO login",
          ConfDataType.BooleanType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  // TODO(bhavin192): this could be moved to customer keys later, and
  // become public.
  public static final ConfKeyInfo<Boolean> enableK8sProviderValidation =
      new ConfKeyInfo<>(
          "yb.provider.kubernetes_provider_validation",
          ScopeType.GLOBAL,
          "Kubernetes provider validation",
          "Enable the Kubernetes provider quick validation",
          ConfDataType.BooleanType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Integer> maxYbcUpgradePollResultTries =
      new ConfKeyInfo<>(
          "ybc.upgrade.poll_result_tries",
          ScopeType.GLOBAL,
          "YBC poll upgrade result tries",
          "YBC poll upgrade result tries count.",
          ConfDataType.IntegerType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Long> ybcUpgradePollResultSleepMs =
      new ConfKeyInfo<>(
          "ybc.upgrade.poll_result_sleep_ms",
          ScopeType.GLOBAL,
          "YBC poll upgrade result Sleep time",
          "YBC poll upgrade result sleep time.",
          ConfDataType.LongType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static ConfKeyInfo<Role> oidcDefaultRole =
      new ConfKeyInfo<>(
          "yb.security.oidc_default_role",
          ScopeType.GLOBAL,
          "OIDC default role",
          "Which role to use incase group memberships are not found",
          ConfDataType.UserRoleEnum,
          ImmutableList.of(ConfKeyTags.INTERNAL));
  public static final ConfKeyInfo<Boolean> enableReleasesRedesign =
      new ConfKeyInfo<>(
          "yb.releases.use_redesign",
          ScopeType.GLOBAL,
          "Use new releases",
          "Enable the new releases design",
          ConfDataType.BooleanType,
          ImmutableList.of(ConfKeyTags.INTERNAL));
  public static final ConfKeyInfo<Boolean> enableAzureProviderValidation =
      new ConfKeyInfo<>(
          "yb.provider.azure_provider_validation",
          ScopeType.GLOBAL,
          "Azure provider validation",
          "Enable Azure Provider quick validation",
          ConfDataType.BooleanType,
          ImmutableList.of(ConfKeyTags.INTERNAL));
  public static final ConfKeyInfo<Integer> haShutdownLevel =
      new ConfKeyInfo<>(
          "yb.ha.shutdown_level",
          ScopeType.GLOBAL,
          "HA Shutdown Level",
          "When to shutdown - 0 for never, 1 for promotion, 2 for promotion and demotion",
          ConfDataType.IntegerType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Boolean> enableGcpProviderValidation =
      new ConfKeyInfo<>(
          "yb.provider.gcp_provider_validation",
          ScopeType.GLOBAL,
          "GCP provider validation",
          "Enables validation for GCP Provider and returns the validation errors json if any",
          ConfDataType.BooleanType,
          ImmutableList.of(ConfKeyTags.INTERNAL));
  public static ConfKeyInfo<String> ldapSearchFilter =
      new ConfKeyInfo<>(
          "yb.security.ldap.ldap_search_filter",
          ScopeType.GLOBAL,
          "LDAP Search Filter",
          "Hidden because this key has dedicated UI",
          ConfDataType.StringType,
          ImmutableList.of(ConfKeyTags.INTERNAL));
  public static final ConfKeyInfo<String> oidcRefreshTokenEndpoint =
      new ConfKeyInfo<>(
          "yb.security.oidcRefreshTokenEndpoint",
          ScopeType.GLOBAL,
          "Endpoint for fetching the access token",
          "YBA will fetch the access token using the refresh token if specified from the endpoint",
          ConfDataType.StringType,
          ImmutableList.of(ConfKeyTags.INTERNAL));
  public static final ConfKeyInfo<Duration> oidcRefreshTokenInterval =
      new ConfKeyInfo<>(
          "yb.security.oidcRefreshTokenInterval",
          ScopeType.GLOBAL,
          "OIDC Refresh Access Token Interval",
          "If configured, YBA will refresh the access token at the specified duration, defaulted to"
              + " 5 minutes.",
          ConfDataType.DurationType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Boolean> allowUsedBundleEdit =
      new ConfKeyInfo<>(
          "yb.edit_provider.new.allow_used_bundle_edit",
          ScopeType.GLOBAL,
          "Allow Editing of in-use Linux Versions",
          "Caution: If enabled, YBA will blindly allow editing the name/AMI associated with the"
              + " bundle, without propagating it to the in-use Universes",
          ConfDataType.BooleanType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Boolean> dbAuditLoggingEnabled =
      new ConfKeyInfo<>(
          "yb.universe.audit_logging_enabled",
          ScopeType.GLOBAL,
          "Enable DB Audit Logging",
          "If this flag is enabled, user will be able to create telemetry providers and"
              + " enable/disable DB audit logging on universes.",
          ConfDataType.BooleanType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Boolean> allowConnectionPooling =
      new ConfKeyInfo<>(
          "yb.universe.allow_connection_pooling",
          ScopeType.GLOBAL,
          "Allow users to enable or disable connection pooling",
          "If this flag is enabled, user will be able to enable/disable connection pooling on"
              + " universes.",
          ConfDataType.BooleanType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Duration> xClusterSyncSchedulerInterval =
      new ConfKeyInfo<>(
          "yb.xcluster.xcluster_sync_scheduler_interval",
          ScopeType.GLOBAL,
          "XCluster Sync Scheduler Interval",
          "Interval at which the XCluster Sync Scheduler runs",
          ConfDataType.DurationType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Duration> xClusterMetricsSchedulerInterval =
      new ConfKeyInfo<>(
          "yb.xcluster.xcluster_metrics_scheduler_interval",
          ScopeType.GLOBAL,
          "XCluster Metrics Scheduler Interval",
          "Interval at which the XCluster Metrics Scheduler runs",
          ConfDataType.DurationType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Integer> ybcClientMaxUnavailableRetries =
      new ConfKeyInfo<>(
          "ybc.client_settings.max_unavailable_retries",
          ScopeType.GLOBAL,
          "Max retries on UNAVAILABLE status",
          "Max client side retries when server returns UNAVAILABLE status",
          ConfDataType.IntegerType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Integer> ybcClientWaitEachUnavailableRetryMs =
      new ConfKeyInfo<>(
          "ybc.client_settings.wait_each_unavailable_retry_ms",
          ScopeType.GLOBAL,
          "Wait( in milliseconds ) between each retries on UNAVAILABLE status",
          "Wait( in milliseconds ) between client side retries when server returns UNAVAILABLE"
              + " status",
          ConfDataType.IntegerType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Integer> ybcClientMaxInboundMsgSize =
      new ConfKeyInfo<>(
          "ybc.client_settings.max_inbound_msg_size_bytes",
          ScopeType.GLOBAL,
          "Max size of YB-Controller RPC response",
          "Max size( in bytes ) of YB-Controller RPC response",
          ConfDataType.IntegerType,
          ImmutableList.of(ConfKeyTags.INTERNAL));
  public static final ConfKeyInfo<Integer> ybcClientDeadlineMs =
      new ConfKeyInfo<>(
          "ybc.client_settings.deadline_ms",
          ScopeType.GLOBAL,
          "Wait( in milliseconds ) for YB-Controller RPC response",
          "Wait( in milliseconds ) for YB-Controller RPC response before throwing client-side"
              + " DEADLINE_EXCEEDED",
          ConfDataType.IntegerType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Integer> ybcClientKeepAlivePingsMs =
      new ConfKeyInfo<>(
          "ybc.client_settings.keep_alive_ping_ms",
          ScopeType.GLOBAL,
          "Wait between each KeepAlive ping to YB-Controller server",
          "Wait( in milliseconds ) between each KeepAlive ping to YB-Controller server",
          ConfDataType.IntegerType,
          ImmutableList.of(ConfKeyTags.INTERNAL));
  public static final ConfKeyInfo<Integer> ybcClientKeepAlivePingsTimeoutMs =
      new ConfKeyInfo<>(
          "ybc.client_settings.keep_alive_ping_timeout_ms",
          ScopeType.GLOBAL,
          "Wait( in milliseconds ) for KeepAlive ping response from YB-Controller server",
          "Wait( in milliseconds ) for KeepAlive ping response from YB-Controller server before"
              + " throwing UNAVAILABLE",
          ConfDataType.IntegerType,
          ImmutableList.of(ConfKeyTags.INTERNAL));
  // Also need rbac to be on for this key to have effect.
  public static final ConfKeyInfo<Boolean> groupMappingRbac =
      new ConfKeyInfo<>(
          "yb.security.group_mapping_rbac_support",
          ScopeType.GLOBAL,
          "Enable RBAC for Groups",
          "Map LDAP/OIDC groups to custom roles defined by RBAC.",
          ConfDataType.BooleanType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Duration> autoMasterFailoverPollerInterval =
      new ConfKeyInfo<>(
          "yb.auto_master_failover.poller_interval",
          ScopeType.GLOBAL,
          "Universe Poller Interval for Master Failover",
          "Poller interval for universes to schedule master failover",
          ConfDataType.DurationType,
          ImmutableList.of(ConfKeyTags.INTERNAL));
  public static final ConfKeyInfo<Boolean> perProcessMetricsEnabled =
      new ConfKeyInfo<>(
          "yb.ui.feature_flags.enable_per_process_metrics",
          ScopeType.GLOBAL,
          "Enable Per Process Metrics",
          "Enable Per Process Metrics",
          ConfDataType.BooleanType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Duration> nodeAgentEnablerScanInterval =
      new ConfKeyInfo<>(
          "yb.node_agent.enabler.scan_interval",
          ScopeType.GLOBAL,
          "Node Agent Enabler Scan Interval",
          "Node agent enabler scan interval",
          ConfDataType.DurationType,
          ImmutableList.of(ConfKeyTags.INTERNAL));
  public static final ConfKeyInfo<Integer> supportBundleDefaultPromDumpRange =
      new ConfKeyInfo<>(
          "yb.support_bundle.default_prom_dump_range",
          ScopeType.GLOBAL,
          "Support bundle prometheus dump range",
          "The start-end duration to collect the prometheus dump inside the support bundle (in"
              + " minutes)",
          ConfDataType.IntegerType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Integer> supportBundlePromDumpBatchDurationInMins =
      new ConfKeyInfo<>(
          "yb.support_bundle.batch_duration_prom_dump_mins",
          ScopeType.GLOBAL,
          "Batch duration for the prometheus dump (in minutes)",
          "For longer time periods of the prometheus dump in the support bundle, the exports can be"
              + " collected batchwise with a specific batch duration",
          ConfDataType.IntegerType,
          ImmutableList.of(ConfKeyTags.INTERNAL));
  public static final ConfKeyInfo<Integer> supportBundlePromDumpStepInSecs =
      new ConfKeyInfo<>(
          "yb.support_bundle.step_prom_dump_secs",
          ScopeType.GLOBAL,
          "Query resolution width for prometheus query",
          "The \"step\" parameter in the query specifies the interval in secs at which data points"
              + " will be evaluated and returned",
          ConfDataType.IntegerType,
          ImmutableList.of(ConfKeyTags.INTERNAL));
  public static final ConfKeyInfo<Integer> numCloudYbaBackupsRetention =
      new ConfKeyInfo<>(
          "yb.auto_yba_backups.num_cloud_retention",
          ScopeType.GLOBAL,
          "Number of cloud YBA backups to retain",
          "When continuous backups feature is enabled only the most recent n backups will be"
              + " retained in the storage bucket",
          ConfDataType.IntegerType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Duration> autoRetryTasksOnYbaRestartTimeWindow =
      new ConfKeyInfo<>(
          "yb.task.auto_retry_on_yba_restart_time_window",
          ScopeType.GLOBAL,
          "Auto Retry Aborted Tasks on YBA Restart Time Window",
          "On YBA startup, retry tasks automatically that were aborted due to YBA shutdown if time"
              + " window is non-zero",
          ConfDataType.DurationType,
          ImmutableList.of(ConfKeyTags.INTERNAL));
  public static final ConfKeyInfo<String> metricScrapeIntervalStandby =
      new ConfKeyInfo<>(
          "yb.metrics.scrape_interval_standby",
          ScopeType.GLOBAL,
          "Standby Prometheus scrape interval",
          "Need to increase it in case federation metrics request takes more time "
              + " than main Prometheus scrape period to complete",
          ConfDataType.StringType,
          ImmutableList.of(ConfKeyTags.PUBLIC));
  public static final ConfKeyInfo<Integer> clusterConsistencyCheckParallelism =
      new ConfKeyInfo<>(
          "yb.health.consistency_check_parallelism",
          ScopeType.GLOBAL,
          "Max Number of Parallel cluster consistency checks",
          "Max Number of Parallel cluster consistency checks",
          ConfDataType.IntegerType,
          ImmutableList.of(ConfKeyTags.INTERNAL));
  public static final ConfKeyInfo<String> oidcGroupClaim =
      new ConfKeyInfo<>(
          "yb.security.oidc_group_claim",
          ScopeType.GLOBAL,
          "OIDC Group Claim",
          "Claim in the ID token containing the list of groups. Default value: \"groups\"",
          ConfDataType.StringType,
          ImmutableList.of(ConfKeyTags.INTERNAL));

  public static final ConfKeyInfo<Boolean> disableV1APIToken =
      new ConfKeyInfo<>(
          "yb.user.disable_v1_api_token",
          ScopeType.GLOBAL,
          "Disable V1 API Token",
          "Disable support for V1 API Token",
          ConfDataType.BooleanType,
          ImmutableList.of(ConfKeyTags.INTERNAL));

  public static final ConfKeyInfo<Boolean> enableRFChange =
      new ConfKeyInfo<>(
          "yb.ui.feature_flags.enable_rf_change",
          ScopeType.GLOBAL,
          "Enable RF Change For Existing Universes",
          "Enable RF change for existing universes through edit universe flow",
          ConfDataType.BooleanType,
          ImmutableList.of(ConfKeyTags.INTERNAL, ConfKeyTags.FEATURE_FLAG));
  public static final ConfKeyInfo<Integer> waitForK8sGFlagSyncSec =
      new ConfKeyInfo<>(
          "yb.kubernetes.wait_for_gflag_sync_sec",
          ScopeType.GLOBAL,
          "Wait for GFlag Sync in K8s universe",
          "Wait for GFlag Sync in K8s universe",
          ConfDataType.IntegerType,
          ImmutableList.of(ConfKeyTags.INTERNAL));
}
