---
title: What's new in the v2.9 latest release series
headerTitle: What's new in the v2.9 latest release series
linkTitle: v2.9 (latest)
description: Enhancements, changes, and resolved issues in the latest release series.
headcontent: Features, enhancements, and resolved issues in the latest release series.
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

{{< tip title="Kubernetes upgrades">}}

To upgrade a pre-version 2.9.0.0 Yugabyte Platform or universe instance deployed on Kubernetes that **did not** specify a storage class override, you need to override the storage class Helm chart value (which is now "", the empty string) and set it to the previous value, "standard".

For Yugabyte Platform, the class is `yugaware.storageClass`. For YugabyteDB, the classes are `storage.master.storageClass` and `storage.tserver.storageClass`.

{{< /tip >}}

## v2.9.1.0 - Oct 29, 2021

**Build:** `2.9.1.0-b140`

### Downloads

<a class="download-binary-link" href="https://downloads.yugabyte.com/yugabyte-2.9.1.0-darwin.tar.gz">
  <button>
    <i class="fab fa-apple"></i><span class="download-text">macOS</span>
  </button>
</a>
&nbsp; &nbsp; &nbsp;
<a class="download-binary-link" href="https://downloads.yugabyte.com/yugabyte-2.9.1.0-linux.tar.gz">
  <button>
    <i class="fab fa-linux"></i><span class="download-text">Linux</span>
  </button>
</a>
<br />

### Docker

```sh
docker pull yugabytedb/yugabyte:2.9.1.0-b140
```

### New Features

#### Yugabyte Platform

#### Database

### Improvements

#### Yugabyte Platform

#### Database

### Bug Fixes

### Known Issues

#### Yugabyte Platform

N/A

#### Database

N/A

## v2.9.0.0 - August 31, 2021

Version 2.9 introduces many new features and refinements. To learn more, check out the [Announcing YugabyteDB 2.9: Pushing the Boundaries of Relational Databases](https://blog.yugabyte.com/announcing-yugabytedb-2-9/) blog post.

Yugabyte release 2.9 builds on our work in the 2.7 series.

**Build:** `2.9.0.0-b4`

### Downloads

<a class="download-binary-link" href="https://downloads.yugabyte.com/yugabyte-2.9.0.0-darwin.tar.gz">
  <button>
    <i class="fab fa-apple"></i><span class="download-text">macOS</span>
  </button>
</a>
&nbsp; &nbsp; &nbsp;
<a class="download-binary-link" href="https://downloads.yugabyte.com/yugabyte-2.9.0.0-linux.tar.gz">
  <button>
    <i class="fab fa-linux"></i><span class="download-text">Linux</span>
  </button>
</a>
<br />

### Docker

```sh
docker pull yugabytedb/yugabyte:2.9.0.0-b4
```

### New Features

(Refer to the [release announcement](https://blog.yugabyte.com/announcing-yugabytedb-2-9/) for new-feature details for this release!)

### Improvements

#### Yugabyte Platform

* [[3452](https://github.com/yugabyte/yugabyte-db/issues/3452)] [Platform] Allow TLS encryption to be enabled on existing universes
* [[3452](https://github.com/yugabyte/yugabyte-db/issues/3452)] [Platform] Wrapper API handling both TLS Toggle and Cert Rotation
* [[8296](https://github.com/yugabyte/yugabyte-db/issues/8296)] [Platform] Ability to stop backups from admin console. (#9310)
* [[8489](https://github.com/yugabyte/yugabyte-db/issues/8489)] [Platform] Update CertsRotate upgrade task to support rootCA rotation
* [[8637](https://github.com/yugabyte/yugabyte-db/issues/8637)] [Platform] Adding APIs to schedule External user-defined scripts.
* [[9236](https://github.com/yugabyte/yugabyte-db/issues/9236)] Replace cron jobs with systemd services for yb-master, yb-tserver, clean_cores, and zip_purge_yb_logs.
* [[9237](https://github.com/yugabyte/yugabyte-db/issues/9237)] Upgrade cron based universes to systemd universes.
* [[9238](https://github.com/yugabyte/yugabyte-db/issues/9238)] Changed AWS Default storage type from GP2 to GP3
* [[9272](https://github.com/yugabyte/yugabyte-db/issues/9272)] Add connection strings for JDBC, YSQL and YCQL in connect Dialog (#9473)
* [[9302](https://github.com/yugabyte/yugabyte-db/issues/9302)] Adding custom machine image option for GCP
* [[9325](https://github.com/yugabyte/yugabyte-db/issues/9325)] updated aws pricing data by running aws utils.py script. pricing data now includes t2 data. t2 instances can now be used when launching universes.
* [[9370](https://github.com/yugabyte/yugabyte-db/issues/9370)] Add Snappy and LZ4 traffic compression algorithms
* [[9370](https://github.com/yugabyte/yugabyte-db/issues/9370)] Implement network traffic compression
* [[9372](https://github.com/yugabyte/yugabyte-db/issues/9372)] Add RPC call metrics
* [[9497](https://github.com/yugabyte/yugabyte-db/issues/9497)] [Platform] Add more regions to GCP metadata
* [PLAT-1501] [Platform] Support downloading YB tarball directly on the DB nodes
* [PLAT-1522] [Platform] Support downloading releases directly from GCS
* AWS disk modification wait method updated to return faster

#### Database

* [[2272](https://github.com/yugabyte/yugabyte-db/issues/2272)] [YSQL] Creating system views during YSQL cluster upgrade
* [[2272](https://github.com/yugabyte/yugabyte-db/issues/2272)] [YSQL] Support INSERT with OID and ON CONFLICT or cluster upgrade
* [[3375](https://github.com/yugabyte/yugabyte-db/issues/3375)] [DocDB] Drive aware LBing when removing tablets
* [[4421](https://github.com/yugabyte/yugabyte-db/issues/4421)] [YCQL] Enable LDAP based authentication
* [[6470](https://github.com/yugabyte/yugabyte-db/issues/6470)] [YSQL] Enable ALTER SCHEMA RENAME
* [[6719](https://github.com/yugabyte/yugabyte-db/issues/6719)] [YSQL] YSQL support for tablet splits by preparing requests along with tablet boundaries
* [[7327](https://github.com/yugabyte/yugabyte-db/issues/7327)] [YSQL] Enable concurrent transactions on ALTER TABLE [DROP & ADD COLUMN] DDL Statement
* [[7612](https://github.com/yugabyte/yugabyte-db/issues/7612)] [DocDB] Improves TTL handling by removing a file completely if all data is expired
* [[7889](https://github.com/yugabyte/yugabyte-db/issues/7889)] [[5326](https://github.com/yugabyte/yugabyte-db/issues/5326)] [YBase] Implement chunking/throttling in Tablet::BackfillIndexForYSQL
* [[7889](https://github.com/yugabyte/yugabyte-db/issues/7889)] [YSQL] Set up infrastructure for index backfill pagination
* [[8452](https://github.com/yugabyte/yugabyte-db/issues/8452)] [YBase] Support for YSQL DDL restores for PITR
* [[8718](https://github.com/yugabyte/yugabyte-db/issues/8718)] [YSQL] Implement function to compute internal hash code for hash-split tables
* [[8756](https://github.com/yugabyte/yugabyte-db/issues/8756)] [YSQL] Enable statistic collection by ANALYZE command
* [[8804](https://github.com/yugabyte/yugabyte-db/issues/8804)] [YSQL] [backup] Support in backups the same table name across different schemas
* [[8846](https://github.com/yugabyte/yugabyte-db/issues/8846)] [DocDB] [PITR] allow data-only rollback from external backups
* [[9036](https://github.com/yugabyte/yugabyte-db/issues/9036)] Ability to verify Index entries for a range of rows on an indexed table/tablet
* [[9073](https://github.com/yugabyte/yugabyte-db/issues/9073)] [[9597](https://github.com/yugabyte/yugabyte-db/issues/9597)] [YSQL] pg_inherits system table must be cached
* [[9219](https://github.com/yugabyte/yugabyte-db/issues/9219)] [YSQL] Add superficial client-side support for SAVEPOINT and RELEASE commands
* [[9317](https://github.com/yugabyte/yugabyte-db/issues/9317)] [YBase] Introduce mutex for permissions manager
* [[9333](https://github.com/yugabyte/yugabyte-db/issues/9333)] [backup] Improve internal PB structure to store backup metadata into SnapshotInfoPB file.
* [[9583](https://github.com/yugabyte/yugabyte-db/issues/9583)] [YSQL] log failed DocDB requests on client side
* [YSQL] Merge user provided shared_preload_libraries to enable custom PSQL extensions (#9576)
* [YSQL] Pass Postgres port to yb_servers function

### Bug Fixes

#### Yugabyte Platform

* [[7456](https://github.com/yugabyte/yugabyte-db/issues/7456)] [Platform] Add Labels to GCP Instances and disks
* [[8152](https://github.com/yugabyte/yugabyte-db/issues/8152)] [Platform] Enforce configured password policy (#9210)
* [[8798](https://github.com/yugabyte/yugabyte-db/issues/8798)] Add version numbers to UpgradeUniverse task info
* [[8950](https://github.com/yugabyte/yugabyte-db/issues/8950)] [Platform] Use matching helm chart version to operate on db k8s pods
* [[9098](https://github.com/yugabyte/yugabyte-db/issues/9098)] [Platform] Fix sample apps command syntax for k8s universes
* [[9213](https://github.com/yugabyte/yugabyte-db/issues/9213)] [Platform] [UI] Leading or trailing spaces are not removed from username field on login console
* [[9245](https://github.com/yugabyte/yugabyte-db/issues/9245)] [Platform] Add empty check before adding tags
* [[9245](https://github.com/yugabyte/yugabyte-db/issues/9245)] [Platform] Tag AWS Volumes and Network Interfaces
* [[9260](https://github.com/yugabyte/yugabyte-db/issues/9260)] [Platform] Fix Health Check UI not rendering
* [[9278](https://github.com/yugabyte/yugabyte-db/issues/9278)] changing kubernetes provider config labels
* [[9331](https://github.com/yugabyte/yugabyte-db/issues/9331)] [Platform] Allow editing "Configuration Name" for backup storage provider without security credentials
* [[9363](https://github.com/yugabyte/yugabyte-db/issues/9363)] fixing metric graph line labels
* [[9363](https://github.com/yugabyte/yugabyte-db/issues/9363)] removing TServer references from graph titles
* [[9365](https://github.com/yugabyte/yugabyte-db/issues/9365)] [Platform] Optimise CertificateInfo.getAll by populating universe details in batch rather than individually
* [[9377](https://github.com/yugabyte/yugabyte-db/issues/9377)] Improving system load graph labels
* [[9403](https://github.com/yugabyte/yugabyte-db/issues/9403)] [UI] Add submit type to submit button in YBModal
* [[9403](https://github.com/yugabyte/yugabyte-db/issues/9403)] Fix form submission causing refresh for confirmation modal
* [[9417](https://github.com/yugabyte/yugabyte-db/issues/9417)] [[9662](https://github.com/yugabyte/yugabyte-db/issues/9662)] Set enable_log_retention_by_op_idx to true by default and bump update_metrics_interval_ms to 15000
* [[9425](https://github.com/yugabyte/yugabyte-db/issues/9425)] [Platform] Slow Query Calls using custom username/password
* [[9580](https://github.com/yugabyte/yugabyte-db/issues/9580)] [Platform] Added a restore_time field in backup restore flow for AWS portal only using Feature Flags.
* [[9628](https://github.com/yugabyte/yugabyte-db/issues/9628)] [Platform] Increase wait for yum lockfile to be released during preprovisioning
* [[9635](https://github.com/yugabyte/yugabyte-db/issues/9635)] [Platform] "None" in zone field observed on tserver status page in platform
* [[9692](https://github.com/yugabyte/yugabyte-db/issues/9692)] Fix initialization of async cluster form values for existing universes without read-replica
* [[9713](https://github.com/yugabyte/yugabyte-db/issues/9713)] [Platform] Do not perform version checks if HA is not set
* [[9854](https://github.com/yugabyte/yugabyte-db/issues/9854)] Disable drive aware LB logic by default
* [PLAT-1520] [Platform] Stop displaying external script schedule among Backup Schedules
* [PLAT-1524] [Platform] Fix password policy validation
* [PLAT-1540] [Platform] Make health check use both possible client to node CA cert location
* [PLAT-1559] [Platform] Stop the external script scheduler if the universe is not present
* [Platform] Disable "Pause Universe" operation for Read-Only users (#9308)
* [Platform] Extends ToggleTLS with ClientRootCA and General Certificates Refactor
* Delete associated certificates while deleting universe
* Make backup configuration name unique for each customer

#### Database

* [[2922](https://github.com/yugabyte/yugabyte-db/issues/2922)] [[5036](https://github.com/yugabyte/yugabyte-db/issues/5036)] [YSQL] Avoid redundant key locking in case of update operations
* [[4992](https://github.com/yugabyte/yugabyte-db/issues/4992)] Add application.conf setting to dump output of cluster_health.py
* [[6667](https://github.com/yugabyte/yugabyte-db/issues/6667)] [DocDB] Add a limit on number of outstanding tablet splits
* [[8034](https://github.com/yugabyte/yugabyte-db/issues/8034)] [DocDB] fixed tablet split vs table deletion race
* [[8256](https://github.com/yugabyte/yugabyte-db/issues/8256)] [DocDB] Tablet splitting: Disable automatic splitting for 2DC enabled tables
* [[8592](https://github.com/yugabyte/yugabyte-db/issues/8592)] Check capability before sending graceful cleanup
* [[8683](https://github.com/yugabyte/yugabyte-db/issues/8683)] [DocDB] fixed CassandraBatchTimeseries failures loop with tablet splitting
* [[9032](https://github.com/yugabyte/yugabyte-db/issues/9032)] [YCQL] Honour token() conditions for all partition keys from IN clause
* [[9219](https://github.com/yugabyte/yugabyte-db/issues/9219)] [DocDB] Ignore intents from aborted subtransactions during reads and writes of the same transaction
* [[9219](https://github.com/yugabyte/yugabyte-db/issues/9219)] [DocDB] Persist SubTransactionId with intent value
* [[9248](https://github.com/yugabyte/yugabyte-db/issues/9248)] [DocDB] reworked global_skip_buffer TSAN suppression
* [[9259](https://github.com/yugabyte/yugabyte-db/issues/9259)] Block PITR when there were DDL changes in restored YSQL database
* [[9279](https://github.com/yugabyte/yugabyte-db/issues/9279)] [YSQL] Enable -Wextra on pggate
* [[9301](https://github.com/yugabyte/yugabyte-db/issues/9301)] Default to logging DEBUG logs on stdout
* [[9314](https://github.com/yugabyte/yugabyte-db/issues/9314)] [PITR] Cleanup sys catalog snapshots
* [[9319](https://github.com/yugabyte/yugabyte-db/issues/9319)] [DocDB] fixed std::string memory usage tracking for gcc9
* [[9320](https://github.com/yugabyte/yugabyte-db/issues/9320)] [YSQL] Import Make index_set_state_flags() transactional
* [[9323](https://github.com/yugabyte/yugabyte-db/issues/9323)] [YSQL] address infinite recursion when analyzing system tables
* [[9334](https://github.com/yugabyte/yugabyte-db/issues/9334)] Fix provider creation in yugabundle by using correct version of python
* [[9335](https://github.com/yugabyte/yugabyte-db/issues/9335)] Use proper initial time to avoid signed integer overflow
* [[9430](https://github.com/yugabyte/yugabyte-db/issues/9430)] [DocDB] Added success message for all tablets and single tablet compaction/flushes
* [[9451](https://github.com/yugabyte/yugabyte-db/issues/9451)] Fixed diskIops and throughput issue.
* [[9452](https://github.com/yugabyte/yugabyte-db/issues/9452)] Fix access key equals method
* [[9476](https://github.com/yugabyte/yugabyte-db/issues/9476)] [DocDB] Use EncryptedEnv instead of Env on MiniCluster
* [[9492](https://github.com/yugabyte/yugabyte-db/issues/9492)] Limit VERIFY_RESULT macro to accept only Result's rvalue reference
* [[9501](https://github.com/yugabyte/yugabyte-db/issues/9501)] [DocDB] fixed Log::AllocateSegmentAndRollOver
* [[9550](https://github.com/yugabyte/yugabyte-db/issues/9550)] [YSQL] output NOTICE when CREATE INDEX in txn block
* [[9553](https://github.com/yugabyte/yugabyte-db/issues/9553)] Update TSAN suppression after RedisInboundCall::Serialize rename
* [[9563](https://github.com/yugabyte/yugabyte-db/issues/9563)] [YSQL] Import jit: Don't inline functions that access thread-locals.
* [[9586](https://github.com/yugabyte/yugabyte-db/issues/9586)] [DocDB] Ignore intents from aborted subtransactions during transaction apply
* [[9593](https://github.com/yugabyte/yugabyte-db/issues/9593)] [DocDB] Move client-side subtransaction state to YBTransaction
* [[9600](https://github.com/yugabyte/yugabyte-db/issues/9600)] [YSQL] Smart driver: Incorrect host value being return in Kubernetes environment
* [[9601](https://github.com/yugabyte/yugabyte-db/issues/9601)] Cleanup intents after bootstrap
* [[9605](https://github.com/yugabyte/yugabyte-db/issues/9605)] [YBase] PITR - Fix auto cleanup of restored hidden tablets
* [[9616](https://github.com/yugabyte/yugabyte-db/issues/9616)] Fix master crash when restoring snapshot schedule with deleted namespace
* [[9654](https://github.com/yugabyte/yugabyte-db/issues/9654)] [xCluster] Limit how often ViolatesMaxTimePolicy and ViolatesMinSpacePolicy are logged
* [[9657](https://github.com/yugabyte/yugabyte-db/issues/9657)] [CQL] Show static column in the output of DESC table
* [[9677](https://github.com/yugabyte/yugabyte-db/issues/9677)] [YSQL] Import Fix mis-planning of repeated application of a projection.
* [[9678](https://github.com/yugabyte/yugabyte-db/issues/9678)] [YQL] Use shared lock for GetYsqlTableToTablespaceMap
* [[9750](https://github.com/yugabyte/yugabyte-db/issues/9750)] Initialise shared memory when running postgres from master
* [[9758](https://github.com/yugabyte/yugabyte-db/issues/9758)] [DocDB] Fix race between split tablet shutdown and tablet flush
* [[9768](https://github.com/yugabyte/yugabyte-db/issues/9768)] [YSQL] Import Fix incorrect hash table resizing code in simplehash.h
* [[9769](https://github.com/yugabyte/yugabyte-db/issues/9769)] [YBase] Use shared lock in GetMemTracker()
* [[9776](https://github.com/yugabyte/yugabyte-db/issues/9776)] [YSQL] Import Fix check_agg_arguments' examination of aggregate FILTER clauses.
* [YSQL] free string in untransformRelOptions()
* [YSQL] Import Fix division-by-zero error in to_char() with 'EEEE' format.
* [YSQL] Import Fix thinkos in LookupFuncName() for function name lookups
* [YSQL] Import Lock the extension during ALTER EXTENSION ADD/DROP.

### Known Issues

#### Yugabyte Platform

N/A

#### Database

N/A

## Notes

{{< note title="New release versioning" >}}

Starting with v2.2, Yugabyte release versions follow a [new release versioning convention](../../versioning). The latest release series, denoted by `MAJOR.ODD`, incrementally introduces new features and changes and is intended for development and testing only. Revision releases, denoted by `MAJOR.ODD.REVISION` versioning, can include new features and changes that might break backwards compatibility. For more information, see [Supported and planned releases](../../releases-overview).

{{< /note >}}
