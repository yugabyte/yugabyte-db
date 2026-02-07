---
title: Configure disaster recovery for a YugabyteDB Anywhere universe
headerTitle: xCluster Disaster Recovery
linkTitle: Disaster recovery
description: Enable Disaster recovery for universes
headContent: Fail over to a replica universe in case of unplanned outages
menu:
  v2024.2_yugabyte-platform:
    parent: back-up-restore-universes
    identifier: disaster-recovery
    weight: 90
type: indexpage
showRightNav: true
---

Use xCluster Disaster Recovery (DR) to recover from an unplanned outage (failover) or to perform a planned switchover. Planned switchover is commonly used for business continuity and disaster recovery testing, and failback after a failover.

A DR configuration consists of the following:

- a DR primary universe, which serves both reads and writes.
- a DR replica universe, which can also serve reads.

Data from the DR primary is replicated asynchronously to the DR replica (which is read only). Due to the asynchronous nature of the replication, DR failover results in non-zero recovery point objective (RPO). In other words, data not yet committed on the DR replica _can be lost_ during a failover. The amount of data loss depends on the replication lag, which in turn depends on the network characteristics between the universes. By contrast, during a switchover RPO is zero, and data is not lost, because the switchover waits for all data to be committed on the DR replica before switching over.

The recovery time objective (RTO) for failover or switchover is very low, and determined by how long it takes applications to switch their connections from one universe to another. Applications should be designed in such a way that the switch happens as quickly as possible.

DR further allows for the role of each universe to switch during planned switchover and unplanned failover scenarios.

![Disaster recovery](/images/yb-platform/disaster-recovery/disaster-recovery.png)

{{<lead link="https://www.yugabyte.com/blog/yugabytedb-xcluster-for-postgresql-dr-in-azure/">}}
Blog: [Using YugabyteDB xCluster DR for PostgreSQL Disaster Recovery in Azure](https://www.yugabyte.com/blog/yugabytedb-xcluster-for-postgresql-dr-in-azure/)
{{</lead>}}

{{<lead link="https://www.youtube.com/watch?v=q6Yq4xlj-wk">}}
Video: [Disaster Recovery With xCluster DR and Two Cloud Regions](https://www.youtube.com/watch?v=q6Yq4xlj-wk)
{{</lead>}}

&nbsp;

{{<index/block>}}

  {{<index/item
    title="Set up Disaster Recovery"
    body="Designate a universe to act as a DR replica."
    href="disaster-recovery-setup/"
    icon="fa-thin fa-umbrella">}}

  {{<index/item
    title="Unplanned failover"
    body="Fail over to the DR replica in case of an unplanned outage."
    href="disaster-recovery-failover/"
    icon="fa-thin fa-cloud-bolt-sun">}}

  {{<index/item
    title="Planned switchover"
    body="Switch over to the DR replica for planned testing and failback."
    href="disaster-recovery-switchover/"
    icon="fa-thin fa-toggle-on">}}

  {{<index/item
    title="Add and remove tables and indexes"
    body="Perform DDL changes to databases in replication."
    href="disaster-recovery-tables/"
    icon="fa-thin fa-plus-minus">}}

{{</index/block>}}

## Schema change modes

xCluster DR can be set up to perform schema changes in the following ways:

| Mode | Description | GA | Deprecated |
| :--- | :--- | :--- | :--- |
| Automatic | Handles all aspects of replication for both data and schema changes. <br>Automatic will be available as Early Access in v2025.1. | v2025.2.1 | |
| [Semi-automatic](#semi-automatic-mode) {{<tags/feature/ea idea="1186">}} | Compared to manual mode, provides operationally simpler setup and management of replication, and fewer steps for performing DDL changes. | v2025.1.0 | |
| [Manual](#manual-mode) | Manual setup and management of replication. DDL changes require manually updating the xCluster configuration. | v2024.2 | v2025.1 |

### Semi-automatic mode

{{<tags/feature/ea idea="1186">}}In this mode, table and index-level schema changes must be performed in the same order as follows:

1. The DR primary universe.
2. The DR replica universe.

You don't need to make any changes to the DR configuration.

{{<lead link="https://www.youtube.com/watch?v=vYyn2OUSZFE">}}
To learn more, watch [Simplified schema management with xCluster DB Scoped](https://www.youtube.com/watch?v=vYyn2OUSZFE)
{{</lead>}}

Semi-automatic mode is recommended for all new DR configurations. When possible, you should delete existing DR configurations and re-create them using semi-automatic mode to reduce the operational burden of DDL changes.

Semi-automatic mode is used for any xCluster DR configuration when the following pre-requisites are met at setup time:

- Both DR primary and replica are running YugabyteDB {{<release "2024.1.3">}} or later.
- Semi-automatic mode is enabled. While in {{<tags/feature/ea>}}, the feature is not enabled by default. To enable it, set the **DB scoped xCluster replication creation** Global runtime configuration option (config key `yb.xcluster.db_scoped.creationEnabled`) to true. Refer to [Manage runtime configuration settings](../../administer-yugabyte-platform/manage-runtime-config/). Note that only a Super Admin user can modify Global runtime configuration settings.

### Manual mode

In manual mode, table and index-level schema changes must be performed on the DR primary universe and the DR replica universe, and, in some cases, they must also be updated on the DR configuration.

The exact sequence of these operations for each type of schema change (DDL) is described in [Manage tables and indexes](./disaster-recovery-tables/).

## Upgrading universes in DR

Use the same version of YugabyteDB on both the DR primary and DR replica.

When [upgrading universes](../../manage-deployments/upgrade-software-install/) in DR replication, you should upgrade and finalize the DR replica before upgrading and finalizing the DR primary.

Note that switchover operations can potentially fail if the DR primary and replica are at different versions.

## xCluster DR vs xCluster Replication

xCluster refers to all YugabyteDB deployments with two or more universes, and has two major flavors:

- _xCluster DR_. Provides turnkey workflow orchestration for applications using transactional SQL in an active-active single-master manner, with only unidirectional replication configured at any moment in time. xCluster DR uses xCluster Replication under the hood, and adds workflow automation and orchestration, including switchover, failover, resynchronization to make another full copy, and so on.
- _xCluster Replication_. Moves the data from one universe to another. Can be used for CQL, non-transactional SQL, bi-directional replication, and other deployment models not supported by xCluster DR.

xCluster DR targets one specific and common xCluster deployment model: [active-active single-master](/stable/develop/build-global-apps/active-active-single-master/), unidirectional replication configured at any moment in time, for transactional YSQL.

- Active-active means that both universes are active - the primary universe for reads and writes, while the replica can handle reads only.

- Single master means that the application writes to only one universe (the primary) at any moment in time.

- Unidirectional replication means that at any moment in time, replication traffic flows in one direction, and is configured (and enforced) to flow only in one direction.

- Transactional SQL means that the application is using SQL (and not CQL), and write-ordering is guaranteed for reads on the target. Furthermore, transactions are guaranteed to be atomic.

xCluster DR adds higher-level orchestration workflows to this deployment to make the end-to-end setup, switchover, and failover of the DR primary to DR replica simple and turnkey. This orchestration includes the following:

- During setup, xCluster DR ensures that both universes have identical copies of the data (using backup and restore to synchronize), and configures the DR replica to be read-only.
- During switchover, xCluster DR waits for all remaining changes on the DR primary to be replicated to the DR replica before switching over.
- During both switchover and failover, xCluster DR promotes the DR replica from read only to read and write; during switchover, xCluster DR demotes (when possible) the original DR primary from read and write to read only.

For all deployment models _other than_ active-active single-master, unidirectional replication configured at any moment in time, for transactional YSQL, use xCluster Replication directly instead of xCluster DR.

For example, use xCluster Replication for the following deployments:

- Multi-master (bidirectional), where you have two application instances, each one writing to a different universe.
- Active-active single-master, in which a single master application can freely write (without coordinating with YugabyteDB for failover or switchover) to either universe, because both accept writes.
- Non-transactional SQL. That is, SQL without write-order guarantees and without transactional atomicity guarantees.
- CQL.

Note that a universe configured for xCluster DR cannot be used for xCluster Replication, and vice versa. Although xCluster DR uses xCluster Replication under the hood, xCluster DR replication is managed exclusively from the **xCluster Disaster Recovery** tab, and not on the **xCluster Replication** tab.

(As an alternative to xCluster DR, you can perform setup, failover, and switchover manually. Refer to [Set up transactional xCluster Replication](../../../deploy/multi-dc/async-replication/async-transactional-setup-semi-automatic/).)

{{<lead link="../../../architecture/docdb-replication/async-replication/">}}
[xCluster Replication: overview and architecture](../../../architecture/docdb-replication/async-replication/)
{{</lead>}}

{{<lead link="../../../deploy/multi-dc/async-replication/">}}
[xCluster replication between universes in YugabyteDB](../../../deploy/multi-dc/async-replication/)
{{</lead>}}

## Limitations

- Currently, automatic replication of DDL (SQL-level changes such as creating or dropping tables or indexes) is not supported. For more details on how to propagate DDL changes from the DR primary to the DR replica, see [Schema change modes](#schema-change-modes). This is tracked by [GitHub issue #11537](https://github.com/yugabyte/yugabyte-db/issues/11537).

- If a database operation requires a full copy, any application sessions on the database on the DR target will be interrupted while the database is dropped and recreated. Your application should either retry connections or redirect reads to the DR primary.

- Setting up DR between a universe upgraded to v2.20.x and a new v2.20.x universe is not supported. This is due to a limitation of xCluster deployments and packed rows. See [Packed row limitations](../../../architecture/docdb/packed-rows/#limitations).

For more information on the YugabyteDB xCluster implementation and its limitations, refer to [xCluster implementation limitations](../../../architecture/docdb-replication/async-replication/#limitations).
