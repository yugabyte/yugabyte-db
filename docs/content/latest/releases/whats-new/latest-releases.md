---
title: What's new in the v2.5 latest release series
headerTitle: What's new in the v2.5 latest release series
linkTitle: v2.5 (latest)
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
showAsideToc: false 
---

Included here are the release notes for all releases in the v2.5 latest release series.

{{< note title="New release versioning" >}}

Starting with v2.2, Yugabyte release versions follow a [new release versioning convention](../../versioning). The latest release series, denoted by `MAJOR.ODD`, incrementally introduces new features and changes and is intended for development and testing only. Revision releases, denoted by `MAJOR.ODD.REVISION` versioning, can include new features and changes that might break backwards compatibility. For more information, see [Supported and planned releases](../../releases-overview).

{{< /note >}}

{{< note title="Upgrading from 1.3" >}}

Prior to v2.0, YSQL was still in beta. Upon release of v2.0, a backward-incompatible file format change was made for YSQL. For existing clusters running pre-2.0 release with YSQL enabled, you cannot upgrade to v2.0 or later. Instead, export your data from existing clusters and then import the data into a new cluster (v2.0 or later).

{{< /note >}}

## Notable features and changes (cumulative for the v2.5 latest release series)

Note: Content will be added as new notable features and changes are available in the patch releases of the v2.5 latest release series. For the latest v2.5 release notes, see [Release notes](#release-notes) below.

## Release notes

### v2.5.0 - November 12, 2020

**Build:** `2.5.0.0-b2`

#### Downloads

<a class="download-binary-link" href="https://downloads.yugabyte.com/yugabyte-2.5.0.0-darwin.tar.gz">
  <button>
    <i class="fab fa-apple"></i><span class="download-text">macOS</span>
  </button>
</a>
&nbsp; &nbsp; &nbsp;
<a class="download-binary-link" href="https://downloads.yugabyte.com/yugabyte-2.5.0.0-linux.tar.gz">
  <button>
    <i class="fab fa-linux"></i><span class="download-text">Linux</span>
  </button>
</a>
<br />

#### Docker

```sh
docker pull yugabytedb/yugabyte:2.5.0.0-b2
```

### **New features**

**Yugabyte Platform**

*   **Azure Cloud integration for Yugabyte Platform (in beta):** 

    [Yugabyte Platform is natively integrated with Azure cloud](http://blog.yugabyte.com/introducing-yugabyte-platform-on-azure-beta-release/) to simplify deploying, monitoring, and managing YugabyteDB deployments. This feature automates a number of operations including orchestration of instances, secure deployments, online software upgrades, and scheduled backups, as well as monitoring and alerting. (6094, 6020)

*   Yugabyte Platform operations now allow promoting a Yugabyte TServer only node to run Yugabyte Master and TServer process (5831)

**Core Database**

*   **Enhanced multi-region capabilities with geo-partitioning and follower reads**

    The YugabyteDB 2.5 release adds [row-level geo-partitioning capabilities](https://blog.yugabyte.com/geo-partitioning-of-data-in-yugabytedb/) as well as follower reads to the extensive set of multi-region features that YugabyteDB already had.

*   **Enterprise-grade security features:** 

    Authentication using the highly secure SCRAM-SHA-256 is now supported to limit security risks from brute force attacks and sniffing, including LDAP support for better user management and the ability to audit all database operations. 

*   **Table-level partitions** allow users to split what is logically one large table into smaller sub-tables, using the following types of table partitioning schemes that PostgreSQL supports: _range partitioning_, _list partitioning,_ and _hash partitioning_. Read more about [table partitioning in YugabyteDB](https://github.com/yugabyte/yugabyte-db/blob/master/architecture/design/ysql-row-level-partitioning.md). 
*   **Event triggers** are now supported in addition to regular table-level triggers in YSQL. While regular triggers are attached to a single table and capture only DML events, event triggers are global to a particular database and are capable of capturing DDL events. The event-based trigger framework enables detecting changes made to the data, and automating any subsequent tasks that need to be performed, which are useful in a number of use cases such as implementing a consolidated, central audit table (2379)
*   **Simplified cluster administration:** 
    *   **Online rebuild of indexes** is supported for both the YSQL and YCQL APIs. This means that new indexes can be added to tables with pre-existing data while concurrent updates are happening on the cluster. The online index rebuild  process creates the newly added index in the background, and transactionally enables the index once the rebuild of all the data is completed. This feature allows flexibility of adding indexes as the application needs evolve to keep queries efficient.
    *   **Cluster execution statistics and running queries** can be analyzed in detail, allowing administrators to gain insights into how the database is performing. The <code>[pg_stat_statements](https://www.postgresql.org/docs/11/pgstatstatements.html)</code> extension, which enables tracking execution statistics of all SQL statements executed by the cluster, is supported and enabled by default.  Support for <code>pg_stat_activity</code> has also been added, which shows information related to the activity performed by each connection. Yet another useful feature in this category is the ability to view all the live queries being executed by the cluster at any point in time.
    *   <strong>Detailed query plan and execution analysis </strong>can now be performed with commands such as <code>EXPLAIN</code> and <code>EXPLAIN ANALYZE</code>. These commands display the execution plan generated by the planner for a given SQL statement. The execution plan shows details for any SQL statement such as how tables will be scanned (plain sequential scan, index scan), what join algorithms will be used to fetch required rows from the different tables, etc.


### <strong>Improvements</strong>

**Yugabyte Platform**



*   Enhancements to on-prem deployment workflows 
    *   Do not fail universe creation if cronjobs can't be created for on-prem (5939)
    *   Remove pre-provision script requirement for air-gapped installations (5929)
    *   "Sudo passwordless" in on-prem cloud provider configuration toggle is renamed
    *   Allow yugabyte user to belong to other user groups in Linux (5943)
    *   Added a new "Advanced" section  in on-prem cloud provider configuration which includes
        *    Use hostnames
        *   Desired home directory
        *    Node exporter settings
    *   Improvements to installation of Prometheus Node Exporter utility workflow (5926)
        *   Prometheus Node exporter option is now available in the cloud configuration under advanced settings
        *   Supports bringing your own node exporter user
*   Make YEDIS API optional for new Universes and no change in behavior of existing universes (5207)
*   Yugabyte Platform now provides alerts for backup tasks (5556)
*   UI/UX improvements for YB Platform
    *   Add visual feedback when backup or restore is initiated from modal (5908)
    *   Minor fixes to primary cluster widget text issue (5988)
    *   Show pre-provision script in UI for non-passwordless sudo on-prem provider (5550)
    *   Update backup target and backup pages (5917)
*   For Yugabyte Universes with Replication Factor(RF) > 3, change the default min_num replicas for even distribution of AZs across all regions (5426)
*   Added functionality to create IPv6 enabled Universe in Kubernetes (5309, 5235)

**Core Database**



*   Support for SQL/JSON Path Language( jsonb_path_query) (5408)
*   Incorrect index update if used expression result is still unchanged (5898)
*   As part of the Tablet Splitting feature
    *    Implemented cleanup of the tablet for which all replicas have been split for (4929)
    *   Compaction improvements (5523)
*   Improve performance for sequences by using higher cache value by default. Controlled by a tserver gflag `ysql_sequence_cache_minval`  (6041)
*   Added compatibility mode in the yb_backup script for Yugabyte version &lt; 2.1.4 (5810)
*   Stability improvements: Make exponential backoff on lagging RAFT followers send NOOP instead of reading 1 op from disk (5527)
*   Added use of separate metrics objects for RegularDB and IntentsDB (5640)

**Bug Fixes**

**Yugabyte Platform**

*   Fix for Universe disk usage shows up empty on the universe page ([5548](https://github.com/yugabyte/yugabyte-db/issues/5548))
*   Fix on on-prem backup failures due to file owned by the root user (6062)
*   Fix for a bug where user operation to perform a change to nodes count by AZ was doing a full move (5335)
*   Fixes for Yugabyte Platform data backup script for Replicated based installations
*   Fixes to Client Certificate start time to use UTC during download (6118)
*   Fixes for migration if no access keys exist yet (6099)
*   Fix to resolve issues caused by starting a Yugabyte TServer Node when another Yugabyte Master node is down in the Universe (5739)
*   Use the correct disk mount point while calculating disk usage of logs (5983)
*   Fixes to delete backups for TLS Enabled Universes (5980)

**Core Database**

*   Fix for bug with the duplicate row detection that allows a unique index to get created when the table is not unique on the index column(s) (5811)
*   Improve fault tolerance of DDLs and reduce version mismatch errors in YSQL (3979, 4360)
*   Fixes to incorrect column-ids in the restored table if the original table was altered (5958)
*   Fixes timeout bug in YB Platform when there are Read Replicas. This fix will ignore read replica TServers when running AreLeadersOnPreferredOnly (6081)
*   Fixes to restore of YSQL Backups after dropping and recreating a database (5651)
*   Fixes to 2DC(x-cluster replication) by adding TLS support for cleaning up cdc_state table (5905)

**Known Issues**

**Yugabyte Platform**

*   Azure IaaS orchestration -
    *   No pricing information provided  (5624) 
    *   No support for regions with zero Availability Zones(AZs) (5628)
