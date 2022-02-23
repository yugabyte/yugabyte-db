---
title: What's new in the v2.12 stable release series
headerTitle: What's new in the v2.12 stable release series
linkTitle: v2.12 (stable)
description: Enhancements, changes, and resolved issues in the current stable release series recommended for production deployments.
aliases:
  - /latest/releases/whats-new/stable-releases/
menu:
  latest:
    identifier: stable-release
    parent: whats-new
    weight: 2586
isTocNested: true
showAsideToc: true
---

Included here are the release notes for all releases in the v2.12 stable release series. Content will be added as new notable features and changes are available in the patch releases of the v2.12 stable release series.

For an RSS feed of the release notes for the latest and stable releases, point your feed reader to [https://docs.yugabyte.com/latest/releases/whats-new/index.xml](../index.xml).

## v2.12.1.0 - February 22, 2022 {#v2.12.1.0}

**Build:** `2.12.1.0-b41`

### Downloads

<a class="download-binary-link" href="https://downloads.yugabyte.com/releases/2.12.1.0/yugabyte-2.12.1.0-b41-darwin-x86_64.tar.gz">
  <button>
    <i class="fab fa-apple"></i><span class="download-text">macOS</span>
  </button>
</a>
&nbsp; &nbsp; &nbsp;
<a class="download-binary-link" href="https://downloads.yugabyte.com/releases/2.12.1.0/yugabyte-2.12.1.0-b41-linux-x86_64.tar.gz">
  <button>
    <i class="fab fa-linux"></i><span class="download-text">Linux x86</span>
  </button>
</a>
&nbsp; &nbsp; &nbsp;
<a class="download-binary-link" href="https://downloads.yugabyte.com/releases/2.12.1.0/yugabyte-2.12.1.0-b41-el8-aarch64.tar.gz">
  <button>
    <i class="fab fa-linux"></i><span class="download-text">Linux ARM</span>
  </button>
</a>
<br />

### Docker

```sh
docker pull yugabytedb/yugabyte:2.12.1.0-b41
```

### New features

#### Yugabyte Platform

* [PLAT-1885] Hashicorp Vault Integration is now GA
* [PLAT-2341] UI support for Multiple on-prem provider instances
* [PLAT-2362] Maintenance windows API and backend
* [PLAT-2382] New backups API
* [PLAT-2401] [PLAT-2404] New GFlag form component and validation for user entries
* [PLAT-2402] UI form for user to add/edit Gflags in more accurate way
* [PLAT-2403] Add Multiple G-flags from free text (JSON)
* [PLAT-2479] LDAP Integration with Platform
* [PLAT-2535] Add API endpoint for aborting tasks
* [PLAT-2553] Node Comparison Feature in Metrics Page (Backend)
* [PLAT-2562] UI support for abort task capability
* [PLAT-2658] Integrate t3 instance type support
* [PLAT-2750] YSQL_DUMP based backup-create and backup-restore
* [PLAT-2766] Can now delete paused GCP instances
* [PLAT-2996] Added Service Account Support
* [PLAT-3020] [PLAT-3021] [PLAT-3022] LDAP UI & User Listing Page migration
* [PLAT-3021] Pre Requisite for LDAP UI & Migrate User Listing from Profile Page to User Management Tab
* [PLAT-3060] Add LDAPS and StartTls Support

#### Database

* [[8032](https://github.com/yugabyte/yugabyte-db/issues/8032)] [YSQL] Add support for ALTER TYPE .. RENAME TO
* [[10451](https://github.com/yugabyte/yugabyte-db/issues/10451)] [ysql] Add support for ADD CONSTRAINT .. UNIQUE .. USING INDEX
* [[10509](https://github.com/yugabyte/yugabyte-db/issues/10509)] [[10510](https://github.com/yugabyte/yugabyte-db/issues/10510)] [YSQL] Add support for CREATE, DROP, and REFRESH MATERIALIZED VIEW

### Improvements

#### Yugabyte Platform

* [PLAT-2588] Make Backup task clean up processes on abort
* [PLAT-2637] Add follower_lag_ms metric to priority regex list (replicated.yml)
* [PLAT-2854] [PLAT-2873] Gflag UI improvements-2, disabled releases should not appear while creating universe
* [PLAT-2891] [PLAT-2890] Add 2.12 major DB version Gflags metadata
* [PLAT-2865] [UI] Filters and sorting on alert configuration page
* [PLAT-2883] Add HTTP_PROXY, HTTPS_PROXY, NO_PROXY environment variable to replicated.yml
* [PLAT-131] Added custom cert validation for preflight checks
* [PLAT-580] Allow multiple xcluster between same source/target pair
* [PLAT-580] Improve xcluster lag metric error handling
* [PLAT-580] Include lag metric data in xcluster GET response
* [PLAT-2164] Improvement - Make ReadOnlyClusterCreate retryable
* [PLAT-2794] Move OIDC to runtime config
* [PLAT-2917] Preflight checks for Azure backup config creation
* [PLAT-2982] [PLAT-2963] Filter gflags in add gflag dropdown.
* [PLAT-3002] [PLAT-3048] feat: Deprecate Equinix KMS support in Platform
* [PLAT-3036] Improvement - Improve task listing API performance
* [PLAT-3042] [PLAT-3043] [PLAT-3044] [PLAT-3045] [PLAT-3076] feat: gflag ui enhancements and fix usability issues.
* [PLAT-3090] Improvement - Universe is in locked state after GFlagsUpgrade fails in ModifyBlackListTask subtask
* [PLAT-3095] [LDAP] [UI] Label and Tooltip Changes
* [PLAT-3095] Label and Tooltip Changes
* [PLAT-3101] [PLAT-3102] [PLAT-3103] [PLAT-3104] [PLAT-3105] [PLAT-3123] Xcluster UI Improvements
* [PLAT-3112] Improvement - Wrong Task Type on UI for add read replica retry after an abort
* [Platform] Updated the azcopy and node-exporter version (#11449)

#### Database

* [[8023](https://github.com/yugabyte/yugabyte-db/issues/8023)] [[11142](https://github.com/yugabyte/yugabyte-db/issues/11142)] [YQL] Enable DocDB to process lookups on a subset of the range key
* [[8732](https://github.com/yugabyte/yugabyte-db/issues/8732)] [YSQL] [Backups] [Geo] Storing Geo-Partitioned Backups to Regional Object Storage Buckets
* [[10513](https://github.com/yugabyte/yugabyte-db/issues/10513)] [DocDB] Adding file deletion option to universal compaction picker (TTL expiry)
* [[10536](https://github.com/yugabyte/yugabyte-db/issues/10536)] [[10828](https://github.com/yugabyte/yugabyte-db/issues/10828)] [Geo] Consolidate TS txn status tablet cache update into one RPC
* [[10605](https://github.com/yugabyte/yugabyte-db/issues/10605)] [DocDB] Refactor Tablet Split Manager to rebuild its state of existing splits periodically.
* [[10730](https://github.com/yugabyte/yugabyte-db/issues/10730)] [xCluster] Add force option to delete_CDC_streams CLI
* [[11263](https://github.com/yugabyte/yugabyte-db/issues/11263)] [DocDB] Disable automatic tablet splitting in 2.12 release.

### Bugs

#### Yugabyte Platform

* [PLAT-2241] Fix for the error: 'ascii' codec can't encode character
* [PLAT-2366] [UI] Suspend alerts during maintenance window
* [PLAT-2508] Missing graph view at Replication tab UI
* [PLAT-2518] Fixing ulimits for systemd universes
* [PLAT-2523] Onprem: Remove/Add node leaves the platform "node_instance" Postgres DB table in a corrupted state
* [PLAT-2585] Fix the metrics inconsistency
* [PLAT-2622] allow adding instance with trailing slash in address
* [PLAT-2673] Validating custom keypair with AWS
* [PLAT-2689] Missing backslash (\) in Prometheus YAML for expired_transactions (replicated)
* [PLAT-2705] Platform restart can leave some tasks in incomplete stuck state
* [PLAT-2708] Some fixes for parallel tests execution
* [PLAT-2763] DestroyUniverse leaks instance in cloud provider if Platform exits before the node IP is updated.
* [PLAT-2767] Exception is logged repeatedly for paused universe due to attempt to send universe keys to nodes.
* [PLAT-2772] [PLAT-2860] Accept additional parameters in CreateUniverse API for custom AMI flow from Cloud
* [PLAT-2813] Investigate why local changes in Universe object in transaction in lockUniverse is overwritten by another fetch of the Universe from DB into a different object.
* [PLAT-2858] Restore fails as universe key cannot be decrypted using new universe.
* [PLAT-2859] [PLAT-2617] Adding DB name additionally while connecting ysqlsh
* [PLAT-2863] GP: Backups handling
* [PLAT-2866] xCluster Add remove tables model is incorrect
* [PLAT-2869] GP: Correct data replicas allocation when default region is defined
* [PLAT-2870] [UI] Unable to create Read Replica until reselect provider (no accessKeyCode in payload)
* [PLAT-2888] [PLAT-2224] Disable renaming of table during restore flow.
* [PLAT-2889] [HA] [UI] Change the button to "paste" instead of "copy" in Key field of Standby Nodes
* [PLAT-2910] fix: UI breaks if the value for a flag is a large string
* [PLAT-2912] [PLAT-2913] Hide change password fields and disable role editing for ldap users
* [PLAT-2920] [Alert] UI Issues in New Alert filter in Alert Policies tab
* [PLAT-2935] Bump up timeouts for API calls which need master leader to be available.
* [PLAT-2941] fix: Remove the email check for LDAP users
* [PLAT-2947] [Alerts] [UI] UI Issues in Alert Maintenance Screen
* [PLAT-2974] Delete Windows AD user when user does not exist on login attempt
* [PLAT-2978] Change Hashicorp vault KEK to have name such that it can be used between multiple configs as long as the config params matches.
* [PLAT-2989] Universe upgrade failed due to "Error running
* [PLAT-3006] [UI] xcluster Replication tab crash
* [PLAT-3047] Fix runtime config logging for LDAP and OIDC
* [PLAT-3056] [UI] Universe Metrics -> Replication graph is empty
* [PLAT-3084] UI doesn't send the payload with "false" when we set ldap_enable_start_tls and enable_ldaps
* [PLAT-3097] Fix preflight check for manual provisioned nodes
* [PLAT-3101] [xCluster] Timezone mismatch in different tabs of xCluster(Replication Tab)
* [PLAT-3133] Azure backups failure because of SSH commands bundling

#### Database

* [[4692](https://github.com/yugabyte/yugabyte-db/issues/4692)] [YSQL] Stop scan before the client timeout
* [[10347](https://github.com/yugabyte/yugabyte-db/issues/10347)] [DocDB] Only call ShouldSplitValidCandidate for automatic splits.
* [[10818](https://github.com/yugabyte/yugabyte-db/issues/10818)] [DocDB] Fix max metrics aggregation metadata to match the entry with the max value
* [[10879](https://github.com/yugabyte/yugabyte-db/issues/10879)] [Geo] Fix TS crash when accessing nonlocal data from a local transaction
* [[10912](https://github.com/yugabyte/yugabyte-db/issues/10912)] Send truncate colocated requests for the indexes associated with the table
* [[10995](https://github.com/yugabyte/yugabyte-db/issues/10995)] Release the memtable mutex before going to sleep.
* [[11038](https://github.com/yugabyte/yugabyte-db/issues/11038)] [YSQL] Check return status for PG gate functions
* [[11047](https://github.com/yugabyte/yugabyte-db/issues/11047)] [[11072](https://github.com/yugabyte/yugabyte-db/issues/11072)] [YSQL] Fix two issues with large OID
* [[11090](https://github.com/yugabyte/yugabyte-db/issues/11090)] [YSQL] Fix incorrect scan result due to scan key pushdown
* [[11094](https://github.com/yugabyte/yugabyte-db/issues/11094)] [YSQL] Fix Postgres exception handling
* [[11167](https://github.com/yugabyte/yugabyte-db/issues/11167)] [YSQL] Release resources on YbScanDesc freeing
* [[11195](https://github.com/yugabyte/yugabyte-db/issues/11195)] [DST] [PITR] Disallow consecutive restores guarded by a flag
* [[11198](https://github.com/yugabyte/yugabyte-db/issues/11198)] [DocDB] Restores should not fail if tablet is moved/deleted off a tserver
* [[11206](https://github.com/yugabyte/yugabyte-db/issues/11206)] [YSQL] [Upgrade] Make YSQL upgrade do nothing when YSQL is not enabled
* [[11230](https://github.com/yugabyte/yugabyte-db/issues/11230)] [YSQL] Block planner peeking at YB indexes
* [[11262](https://github.com/yugabyte/yugabyte-db/issues/11262)] [YSQL] Fix assertion failure on where clause with < any operator
* [[11335](https://github.com/yugabyte/yugabyte-db/issues/11335)] [DocDB] Restore should return an error if it hits max number of retries
* [[11346](https://github.com/yugabyte/yugabyte-db/issues/11346)] [YSQL] Fix bug in YBCIsSingleRowUpdateOrDelete
* [[11347](https://github.com/yugabyte/yugabyte-db/issues/11347)] [YSQL] Fix bug in ALTER TABLE ADD PRIMARY KEY
* [[11440](https://github.com/yugabyte/yugabyte-db/issues/11440)] [YSQL] Drop temp table when session terminates
* [YCQL] Move batch ID generator as field of AuditLogger
