---
title: What's new in the v2.11 latest release series
headerTitle: What's new in the v2.11 latest release series
linkTitle: v2.11 (latest)
description: Enhancements, changes, and resolved issues in the latest release series.
image: /images/section_icons/quick_start/install.png
aliases:
  - /latest/releases/
menu:
  latest:
    parent: whats-new
    identifier: latest-release
    weight: 2585
isTocNested: true
showAsideToc: true
---

## v2.11.0.1 - December 3, 2021

This is a bug-fix-only release.

**Build:** `2.11.0.1-b1`

### Downloads

<a class="download-binary-link" href="https://downloads.yugabyte.com/releases/2.11.0.1/yugabyte-2.11.0.1-b1-darwin-x86_64.tar.gz">
  <button>
    <i class="fab fa-apple"></i><span class="download-text">macOS</span>
  </button>
</a>
&nbsp; &nbsp; &nbsp;
<a class="download-binary-link" href="https://downloads.yugabyte.com/releases/2.11.0.1/yugabyte-2.11.0.1-b1-linux-x86_64.tar.gz">
  <button>
    <i class="fab fa-linux"></i><span class="download-text">Linux x86</span>
  </button>
</a>
&nbsp; &nbsp; &nbsp;
<a class="download-binary-link" href="https://downloads.yugabyte.com/releases/2.11.0.1/yugabyte-2.11.0.1-b1-el8-aarch64.tar.gz">
  <button>
    <i class="fab fa-linux"></i><span class="download-text">Linux ARM</span>
  </button>
</a>
<br />

### Docker

```sh
docker pull yugabytedb/yugabyte:2.11.0.1-b1
```

### Bug fixes

#### Yugabyte Platform

* [PLAT-2266] application.log is not available for logs on UI
* [PLAT-2400] Universe scaling out by adding nodes keeps new nodes in blacklisted state.

#### Database

N/A

## v2.11.0.0 - November 22, 2021

**Build:** `2.11.0.0-b7`

### Downloads

<a class="download-binary-link" href="https://downloads.yugabyte.com/releases/2.11.0.0/yugabyte-2.11.0.0-b7-darwin-x86_64.tar.gz">
  <button>
    <i class="fab fa-apple"></i><span class="download-text">macOS</span>
  </button>
</a>
&nbsp; &nbsp; &nbsp;
<a class="download-binary-link" href="https://downloads.yugabyte.com/releases/2.11.0.0/yugabyte-2.11.0.0-b7-linux-x86_64.tar.gz">
  <button>
    <i class="fab fa-linux"></i><span class="download-text">Linux x86</span>
  </button>
</a>
&nbsp; &nbsp; &nbsp;
<a class="download-binary-link" href="https://downloads.yugabyte.com/releases/2.11.0.0/yugabyte-2.11.0.0-b7-el8-aarch64.tar.gz">
  <button>
    <i class="fab fa-linux"></i><span class="download-text">Linux ARM</span>
  </button>
</a>
<br />

### Docker

```sh
docker pull yugabytedb/yugabyte:2.11.0.0-b7
```

### New Features

#### Yugabyte Platform

* [CLOUDGA-2875] Add additional permissions to `yb_superuser`
* [CLOUDGA-3033] Grant additional roles to `yb_superuser`
* [PLAT-26] [[9612](https://github.com/yugabyte/yugabyte-db/issues/9612)] Add logs purge threshold option to `zip_purge_yb_logs.sh`
* [PLAT-580] Add new xCluster create/get/edit/delete APIs and data model
* [PLAT-1669] initial OEL 8 support
* [PLAT-1748] Create POST and GET endpoint to generate support bundle and download support bundle v1
* [PLAT-1817] Add support for new certificate creation when rotating certs
* [PLAT-1856] Support pause/resume for GCP
* [PLAT-1870] Add a table view to universe page
* [PLAT-1941] Add status quick filter and table actions
* [PLAT-2071] Implement read-only user functionality for Alert UIs
* [PLAT-2104] Enable/disable Auth in k8s

#### Database

* [[1127](https://github.com/yugabyte/yugabyte-db/issues/1127)] [YSQL] Collation support
* [[2272](https://github.com/yugabyte/yugabyte-db/issues/2272)] [YSQL] Migration framework for YSQL cluster upgrade
* [[7850](https://github.com/yugabyte/yugabyte-db/issues/7850)] [YSQL] Implement GIN (YBGIN) indexes
* [[8242](https://github.com/yugabyte/yugabyte-db/issues/8242)] Enable automatic tablet splitting by default
* [[8422](https://github.com/yugabyte/yugabyte-db/issues/8422)] [YSQL] pg_stat_monitor extension
* [[9370](https://github.com/yugabyte/yugabyte-db/issues/9370)] Set `enable_stream_compression` flag to true by default
* [[9595](https://github.com/yugabyte/yugabyte-db/issues/9595)] [YSQL] Support YBGIN index in `pg_trgm` extension
* [[10019](https://github.com/yugabyte/yugabyte-db/issues/10019)] [DocDB] Add support for ZLib compression
* [[10094](https://github.com/yugabyte/yugabyte-db/issues/10094)] [DocDB] added data_block_key_value_encoding_format option
* [[10094](https://github.com/yugabyte/yugabyte-db/issues/10094)] [DocDB] Implemented advanced delta encoding/decoding optimized for DocDB-specific RocksDB keys
* [[10141](https://github.com/yugabyte/yugabyte-db/issues/10141)] [DocDB] Remove feature gate on savepoints
* [[10150](https://github.com/yugabyte/yugabyte-db/issues/10150)] [YSQL] Add functionality for the `yb_extension` role
* [[10157](https://github.com/yugabyte/yugabyte-db/issues/10157)] [Geo] Added command to create new transaction status tables
* [[10204](https://github.com/yugabyte/yugabyte-db/issues/10204)] [YSQL] Add functionality for the `yb_fdw` role
* [[10473](https://github.com/yugabyte/yugabyte-db/issues/10473)] Implement YSQL Follower reads

### Improvements

#### Yugabyte Platform

* [PLAT-1506] Enhancement: Support to create/mark Alert Definition Group in Active or Inactive state from UI
* [PLAT-1513] Enhance metrics that use sum without to also exclude namespace_name (#9759)
* [PLAT-1580] Implement OOM killer alert
* [PLAT-1585] k8s example for create universe
* [PLAT-1704] Make Platform health checks more scalable
* [PLAT-1731] Add more logging for Platform HA feature
* [PLAT-1740] Make backup utility python3-compatible for different OS.
* [PLAT-1753] Enable taking backups using custom ports
* [PLAT-1760] Add readable type names
* [Plat-1777] Add basic filtering and sorting
* [Plat-1797] Create a pagination component
* [PLAT-1808] [Alert UI] cleanup tasks
* [PLAT-1867] AWS Provider and Universe examples
* [PLAT-1916] Moved default access token key to configuration
* [PLAT-1934] Adding UI to set KUBE_DOMAIN
* [PLAT-1953] Improve performance of /logs endpoint
* [PLAT-1956] Expose preflight check as a standalone action
* [PLAT-1962] Add optional AWS KMS Endpoint field while creating KMS configuration.
* [PLAT-1967] API Add support for k8s provider creation
* [PLAT-1989] Show alert configuration target in page view
* [PLAT-2032] Append number to self-signed certificate labels when rotating certs
* [PLAT-2033] [Alert] [UI] Move seconds in Duration under conditions similar to Threshold in Alert Definition Page
* [PLAT-2034] Specific task type name for TLS toggle
* [PLAT-2093] Replace pause icon with resume icon for resume universe

#### Database

* [[1127](https://github.com/yugabyte/yugabyte-db/issues/1127)] [YSQL] Improve collation upgrade performance.
* [[3745](https://github.com/yugabyte/yugabyte-db/issues/3745)] [DocDB] Added flag for making log cache memory percent-based.
* [[5310](https://github.com/yugabyte/yugabyte-db/issues/5310)] [YSQL] Cherry-pick upstream PostgreSQL commit that performs refactor of ExecUpdate() function.
* [[5310](https://github.com/yugabyte/yugabyte-db/issues/5310)] [YSQL] Support row-level partition UPDATE across partitions
* [[7293](https://github.com/yugabyte/yugabyte-db/issues/7293)] [YSQL] Import Fix tablespace handling for partitioned tables
* [[8242](https://github.com/yugabyte/yugabyte-db/issues/8242)] [DocDB] Update defaults for automatic tablet splitting
* [[8862](https://github.com/yugabyte/yugabyte-db/issues/8862)] [DocDB] Enable CHANGE_CONFIG_OP to be added to raft log for split tablet
* [[9178](https://github.com/yugabyte/yugabyte-db/issues/9178)] [YSQL] Support a way to read from local partitions first
* [[9467](https://github.com/yugabyte/yugabyte-db/issues/9467)] [YSQL] Increase scope of cases where transparent retries are performed
* [[9468](https://github.com/yugabyte/yugabyte-db/issues/9468)] [YSQL] [Part-1] READ COMMITTED isolation level
* [[9512](https://github.com/yugabyte/yugabyte-db/issues/9512)] Add optional bootstrap IDs parameter to AlterUniverseReplication add_tables
* [[9606](https://github.com/yugabyte/yugabyte-db/issues/9606)] [DocDB] Add flag --force for command delete_tablet to set state TABLET_DATA_DELETED for tool yb-ts-cli
* [[9969](https://github.com/yugabyte/yugabyte-db/issues/9969)] [DocDB] Add a gflag for RocksDB block_restart_interval
* [[10038](https://github.com/yugabyte/yugabyte-db/issues/10038)] [YQL] Support for displaying the bind values for a prepared statement(s).
* [[10110](https://github.com/yugabyte/yugabyte-db/issues/10110)] [DocDB] Enables compaction file filter during manual compactions
* [[10136](https://github.com/yugabyte/yugabyte-db/issues/10136)] [YSQL] Import Reject SELECT ... GROUP BY GROUPING SETS (()) FOR UPDATE.
* [[10151](https://github.com/yugabyte/yugabyte-db/issues/10151)] [YSQL] Import Avoid fetching from an already-terminated plan.
* [[10199](https://github.com/yugabyte/yugabyte-db/issues/10199)] [YSQL] Import Reset memory context once per tuple in validateForeignKeyConstraint.
* [[10208](https://github.com/yugabyte/yugabyte-db/issues/10208)] [YSQL] Adding negative caching for types and metrics collection for catalog cache misses
* [[10211](https://github.com/yugabyte/yugabyte-db/issues/10211)] [xCluster] Allow for overriding the default CDCConsumerHandler thread pool size
* [[10266](https://github.com/yugabyte/yugabyte-db/issues/10266)] [YSQL] Import In security-restricted operations, block enqueue of at-commit user code.
* [[10317](https://github.com/yugabyte/yugabyte-db/issues/10317)] [YSQL] Import `Allow users with BYPASSRLS to alter their own passwords.`
* [[10335](https://github.com/yugabyte/yugabyte-db/issues/10335)] [YSQL] Import Avoid lockup of a parallel worker when reporting a long error message.
* [[10343](https://github.com/yugabyte/yugabyte-db/issues/10343)] [DocDB] Adds a GFlag to ignore value-level TTL during SST file expiration
* [[10359](https://github.com/yugabyte/yugabyte-db/issues/10359)] [YCQL] [YSQL] Update dependencies for Java subprojects
* [[10377](https://github.com/yugabyte/yugabyte-db/issues/10377)] [DocDB] Add multi drives to TS servers on ExternalMiniCluster
* [[10381](https://github.com/yugabyte/yugabyte-db/issues/10381)] [DocDB] enhance debug logs for RWCLock
* [[10382](https://github.com/yugabyte/yugabyte-db/issues/10382)] [YSQL] Import Make pg_dump acquire lock on partitioned tables that are to be dumped.
* [[10385](https://github.com/yugabyte/yugabyte-db/issues/10385)] [YSQL] Import Add strict_multi_assignment and too_many_rows plpgsql checks
* [[10386](https://github.com/yugabyte/yugabyte-db/issues/10386)] [YSQL] Import Fix some errhint and errdetail strings missing a period
* [[10406](https://github.com/yugabyte/yugabyte-db/issues/10406)] [YSQL] Create callback for index write
* [[10427](https://github.com/yugabyte/yugabyte-db/issues/10427)] [DocDB] Added transaction_table_num_tablets_per_tserver flag
* [[10487](https://github.com/yugabyte/yugabyte-db/issues/10487)] [[10489](https://github.com/yugabyte/yugabyte-db/issues/10489)] [YSQL] Import Prevent drop of tablespaces used by partitioned relations
* [YSQL] Import Fix cloning of row triggers to sub-partitions

### Bug Fixes

#### Yugabyte Platform

* [CLOUDGA-2800] Fix possible customer removal issue related to persisted metrics
* [PLAT-364] [[9391](https://github.com/yugabyte/yugabyte-db/issues/9391)] Incorrect masters selection leads to universe creation failures
* [PLAT-490] Display timezone with timestamp
* [PLAT-1504] Delete release from `yugaware_property` table and local filesystem.
* [PLAT-1511] Fix legend overflowing in metrics tab
* [PLAT-1607] Upgrade systemd API fix
* [PLAT-1618] Make TLS Enable/Disable UI to display as default instead of under feature flag
* [PLAT-1634] Backup page is not loading because of empty configuration column
* [PLAT-1716] Import releases modal on releases page doesn't work
* [PLAT-1751] [UI] DB Version field setting getting reset to first item in the dropdown on toggling between the Read Replica and Primary cluster tabs
* [PLAT-1752] Potential resource leak in thread pool for subtasks
* [PLAT-1789] [PLAT-1727] Addition/Removal of default platform gflags does not retain the universe's initial preferences.
* [PLAT-1803] Not able to change cert for client to node in TLS enable feature
* [PLAT-1806] Resolve issue in TlsToggle where certs_for_client_dir is set as empty
* [PLAT-1819] [PLAT-1828] Release backup lock when Platform restarts, and update Backup state
* [PLAT-1824] Improve backup retention in case of backup failure
* [PLAT-1829] [ycql/ysql auth password] Wrong error message
* [PLAT-1831] Fix DB version dropdown from being reset when switching between primary and async cluster forms
* [PLAT-1831] Fix when navigating from home page to Create Universe
* [PLAT-1836] Clean up needless releases data from YB nodes
* [PLAT-1837] Change Replication factor field to be editable for async universe form.
* [PLAT-1840] Fix 30 sec customer_controller list API
* [PLAT-1842] Fix long universe list query
* [PLAT-1845] GET /api/v1/customers/cUUID/universes/uniUUID/leader takes 9 seconds on dev portal
* [PLAT-1853] Frequent error log related to health checks on portal.k8s
* [PLAT-1855] Edit Universe example and missing implicit parameters
* [PLAT-1862] Backup Frequency cannot be negative number
* [PLAT-1886] Set locale on GCP Ubuntu universes.
* [PLAT-1887] Fix creation of read-only on-prem universe + code cleanup
* [PLAT-1892] Remove default template for error log + remove error logs from health check report
* [PLAT-1895] Fix backup failure alert in case restore fails
* [PLAT-1897] Make client_max_body_size configurable in replicated
* [PLAT-1897] Take-2. Make client_max_body_size configurable in replicated
* [PLAT-1907] Mismatching address should not cause standby to overwrite local instance
* [PLAT-1907] Missed a rename of assertYWSE to assertPlatformException
* [PLAT-1921] [Backup] [UI] Disappearance of Encrypt backup toggle in 2.9.1 and 2.9.2 portals
* [PLAT-1923] [PLAT-1924] Ability to create universes with AZs count > RF + definition of default region
* [PLAT-1942] Backup/restore failing on KMS enabled universes
* [PLAT-1952] Correctly mark status of root and client root during add operation
* [PLAT-1953] Add /logs sh script to release package
* [PLAT-1969] [UI] Universe creation - Create button is disabled when YSQL/YCQL auth is disabled
* [PLAT-1972] Check certificates existence
* [PLAT-1976] Fix EditUniverse for on-prem
* [PLAT-1987] Only adding `gcp_internal` flag for GCP provider
* [PLAT-1990] Ensure universe size doesn't change dramatically during a full move
* [PLAT-1998] Fix NPE in SystemdUpgrade task for TLS enabled universes
* [PLAT-1999] Update cert directories gflags during cert rotation
* [PLAT-2002] Fixing `zip_purgs_yb_logs` to not error without threshold flag
* [PLAT-2015] Remove Sort functionality from "Target universe" in alert listing
* [PLAT-2018] Fix instance restart alert during universe operation
* [PLAT-2019] Fix permission denied issues during find command
* [PLAT-2030] [UI] [Platform]UI should display the name of the newly created cert instead of "Create new cert" option
* [PLAT-2034] Fix migration version
* [PLAT-2045] Minor fix to release name regex
* [PLAT-2046] Yb-client: Possible race condition while getting Master-Leader
* [PLAT-2049] Fix metric storage + fix health check for development and ARM builds
* [PLAT-2053] Fix the wrong error message in TLS configuration modal
* [PLAT-2068] [UI] Screen going blank when removed regions in Edit Universe
* [PLAT-2069] Hiding systemd upgrade option for read-only users
* [PLAT-2073] [UI] Enable Systemd Services toggle shows wrong status
* [PLAT-2074] Alert configuration active status in page view + activate/deactivate from actions + sorting fixes
* [PLAT-2081] Show Error message when trying to create existing user
* [PLAT-2092] Fix Task list default sorting by create time
* [PLAT-2094] Fix k8s universe certificate expiry checks
* [PLAT-2096] [UI] Restore backup UI refresh issue
* [PLAT-2097] Fix repeated migration V68 : approach 2
* [PLAT-2107] Resolve multiple UI fixes in Encryption-at-Rest modal
* [PLAT-2109] Skip hostname validation in certificate
* [PLAT-2110] Fix wrong default destination migration for multi-tenant platforms.
* [PLAT-2111] Systemd Upgrade failing with read replica
* [PLAT-2113] Fix HA failing with entity_too_large
* [PLAT-2124] [Alert] [UI] Select Alert Metrics doesn't load the template if the metrics is created twice
* [PLAT-2126] Fix stopping periodical tasks in case of failure
* [PLAT-2128] Fix alert message field to print the whole message + alert channel error message fix
* [PLAT-2129] [Alert] Full Alert message is not displayed in Alert listing page on selecting the alert
* [PLAT-2134] Fix beforeValidate migration for the case of empty database
* [PLAT-2138] Fix OOM Kill alert query in DB migration
* [PLAT-2157] Flyway plugin patch for ignoreMissingMigration and default java package issue
* [PLAT-2167] Fix 3000 seconds timeout for IAM profile retrieval operation
* [PLAT-2180] Missing error response logging when demoteInstance fails
* [PLAT-2189] Fix universe creation on airgap install
* [PLAT-2200] [UI] Fix regression with HA "standby" overlay

#### Database

* [[2272](https://github.com/yugabyte/yugabyte-db/issues/2272)] [YSQL] Fix OID generation for initdb migration
* [[2397](https://github.com/yugabyte/yugabyte-db/issues/2397)] [YSQL] Fix wrong results after modification statement failure in procedure block
* [[2866](https://github.com/yugabyte/yugabyte-db/issues/2866)] Add deadline to CdcProducer::GetChanges call
* [[5536](https://github.com/yugabyte/yugabyte-db/issues/5536)] [YSQL] fast-path not used when type conversion from timestamp with & without timezone happens
* [[7092](https://github.com/yugabyte/yugabyte-db/issues/7092)] [[10046](https://github.com/yugabyte/yugabyte-db/issues/10046)] [[10222](https://github.com/yugabyte/yugabyte-db/issues/10222)] [[10224](https://github.com/yugabyte/yugabyte-db/issues/10224)] [[10230](https://github.com/yugabyte/yugabyte-db/issues/10230)] [[10251](https://github.com/yugabyte/yugabyte-db/issues/10251)] [[10295](https://github.com/yugabyte/yugabyte-db/issues/10295)] Enable Clang 12 ASAN build on AlmaLinux 8 and fix relevant bugs
* [[8148](https://github.com/yugabyte/yugabyte-db/issues/8148)] [DocDB] Potential issue on crash after creating post-split tablets
* [[8229](https://github.com/yugabyte/yugabyte-db/issues/8229)] [Backup] repartition table if needed on YSQL restore
* [[8718](https://github.com/yugabyte/yugabyte-db/issues/8718)] [YSQL] Free Bitmapset used in match_index_to_operand()
* [[9541](https://github.com/yugabyte/yugabyte-db/issues/9541)] [YSQL] Restart metrics webserver when postmaster recovers
* [[9645](https://github.com/yugabyte/yugabyte-db/issues/9645)] [YSQL] Cleanup DDL transaction state in case of query failure
* [[9898](https://github.com/yugabyte/yugabyte-db/issues/9898)] [DocDB] Fix queries on system.partitions when unable to resolve some addresses
* [[9936](https://github.com/yugabyte/yugabyte-db/issues/9936)] [YBase] Don't parse RequestHeader protobuf
* [[9936](https://github.com/yugabyte/yugabyte-db/issues/9936)] Fix ysql_dump in encrypted k8s environment
* [[9957](https://github.com/yugabyte/yugabyte-db/issues/9957)] [YSQL] Fix memory usage when translating decimal data into Postgres's datum format.
* [[10044](https://github.com/yugabyte/yugabyte-db/issues/10044)] [DST] [PITR] Fix race in snapshot/schedule cleanup
* [[10071](https://github.com/yugabyte/yugabyte-db/issues/10071)] Fix Locking Issues with DeleteTableInMemory
* [[10077](https://github.com/yugabyte/yugabyte-db/issues/10077)] [DocDB] Compaction file filter factory uses HistoryRetention instead of Schema
* [[10082](https://github.com/yugabyte/yugabyte-db/issues/10082)] Clean up environment on SetupUniverseReplication failure
* [[10116](https://github.com/yugabyte/yugabyte-db/issues/10116)] [YSQL] Starting a new cluster with an old YSQL snapshot fails in debug build
* [[10164](https://github.com/yugabyte/yugabyte-db/issues/10164)] [DocDB] Max file size for compactions should only affect TTL tables
* [[10166](https://github.com/yugabyte/yugabyte-db/issues/10166)] Acquire lock in GetUniverseParamsWithVersion
* [[10167](https://github.com/yugabyte/yugabyte-db/issues/10167)] [DocDB] Save source tablet mutations to sys catalog when splitting
* [[10207](https://github.com/yugabyte/yugabyte-db/issues/10207)] [DocDB] Make read_buffer_memory_limit a percentage of process memory instead of total memory.
* [[10218](https://github.com/yugabyte/yugabyte-db/issues/10218)] CheckLocalHostInMasterAddresses should check all specified RPC addresses
* [[10220](https://github.com/yugabyte/yugabyte-db/issues/10220)] [DocDB] splitting: deprecate TabletForSplitPB.tablets_for_split
* [[10225](https://github.com/yugabyte/yugabyte-db/issues/10225)] [DST] Adhere to the definitions of `partitions_` and `tablets_` during `DeleteTable`
* [[10240](https://github.com/yugabyte/yugabyte-db/issues/10240)] Add IPv6 address filters to default value of net_address_filter
* [[10254](https://github.com/yugabyte/yugabyte-db/issues/10254)] [YSQL] Fix 100% CPU usage regression bug in SELECT with FOR KEY SHARE/IN/missing keys
* [[10259](https://github.com/yugabyte/yugabyte-db/issues/10259)] [YSQL] Allow setting tablegroups on tables using WITH
* [[10304](https://github.com/yugabyte/yugabyte-db/issues/10304)] [DocDB] fix deadlock in ProcessTabletReportBatch
* [[10308](https://github.com/yugabyte/yugabyte-db/issues/10308)] [YSQL] Prevent setting tablespaces for temp tables
* [[10314](https://github.com/yugabyte/yugabyte-db/issues/10314)] [YSQL] remove mention of HaveYouForgottenAboutMigration
* [[10323](https://github.com/yugabyte/yugabyte-db/issues/10323)] [YBase] Fix outbound call timeout handling
* [[10364](https://github.com/yugabyte/yugabyte-db/issues/10364)] [YCQL] Fix issue when dropping col that is not in an existing non-partial secondary index
* [[10374](https://github.com/yugabyte/yugabyte-db/issues/10374)] [YSQL] Cannot start a cluster with `--ysql_pg_conf_csv='statement_timeout=1000'`
* [[10415](https://github.com/yugabyte/yugabyte-db/issues/10415)] [Backup] Backup-restore failures for old backups.
* [[10419](https://github.com/yugabyte/yugabyte-db/issues/10419)] [YSQL] Shorten string to get rid of output truncation warning
* [[10430](https://github.com/yugabyte/yugabyte-db/issues/10430)] [YSQL] Limit to IPv4 for sys catalog initialization
* [[10433](https://github.com/yugabyte/yugabyte-db/issues/10433)] [YSQL] Load pg_depend and `pg_shdepend` on demand
* [[10496](https://github.com/yugabyte/yugabyte-db/issues/10496)] [YSQL] Adjust cost when `enable_seqscan=off`
* [adhoc] [DST] Reword loud log line in raft_consensus.cc to remove the word Failure

### Known Issues

#### Yugabyte Platform

N/A

#### Database

N/A

## Notes

{{< note title="New release versioning" >}}

Starting with v2.2, Yugabyte release versions follow a [new release versioning convention](../../versioning). The latest release series, denoted by `MAJOR.ODD`, incrementally introduces new features and changes and is intended for development and testing only. Revision releases, denoted by `MAJOR.ODD.REVISION` versioning, can include new features and changes that might break backwards compatibility. For more information, see [Supported and planned releases](../../releases-overview).

{{< /note >}}
