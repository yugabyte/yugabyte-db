---
title: Configure disaster recovery for a YugabyteDB Anywhere universe
headerTitle: xCluster Disaster recovery
linkTitle: Disaster recovery
description: Enable deployment using transactional (active-standby) replication between universes
headContent: Fail over to a replica universe in case of unplanned outages
cascade:
  earlyAccess: /preview/releases/versioning/#feature-maturity
menu:
  preview_yugabyte-platform:
    parent: back-up-restore-universes
    identifier: disaster-recovery
    weight: 90
type: indexpage
showRightNav: true
---

Use xCluster disaster recovery (DR) to recover from an unplanned outage (failover) or to perform a planned switchover. Planned switchover is commonly used for business continuity and disaster recovery testing, and failback after a failover.

A DR configuration consists of the following:

- a DR primary universe, which serves both reads and writes.
- a DR replica universe, which can also serve reads.

Data from the DR primary is replicated asynchronously to the DR replica (which is read only). Due to the asynchronous nature of the replication, DR failover results in non-zero recovery point objective (RPO). In other words, data not yet committed on the DR replica _can be lost_ during a failover. The amount of data loss depends on the replication lag, which in turn depends on the network characteristics between the universes. By contrast, during a switchover RPO is zero, and data is not lost, because the switchover waits for all data to be committed on the DR replica before switching over.

The recovery time objective (RTO) for failover or switchover is very low, and determined by how long it takes applications to switch their connections from one universe to another. Applications should be designed in such a way that the switch happens as quickly as possible.

DR further allows for the role of each universe to switch during planned switchover and unplanned failover scenarios.

![Disaster recovery](/images/yb-platform/disaster-recovery/disaster-recovery.png)

{{<index/block>}}

  {{<index/item
    title="Set up Disaster Recovery"
    body="Designate a universe to act as a DR replica."
    href="disaster-recovery-setup/"
    icon="/images/section_icons/explore/fault_tolerance.png">}}

  {{<index/item
    title="Unplanned failover"
    body="Fail over to the DR replica in case of an unplanned outage."
    href="disaster-recovery-failover/"
    icon="/images/section_icons/explore/high_performance.png">}}

  {{<index/item
    title="Planned switchover"
    body="Switch over to the DR replica for planned testing and failback."
    href="disaster-recovery-switchover/"
    icon="/images/section_icons/manage/backup.png">}}

  {{<index/item
    title="Add and remove tables and indexes"
    body="Perform DDL changes to databases in replication."
    href="disaster-recovery-tables/"
    icon="/images/section_icons/architecture/concepts/replication.png">}}

{{</index/block>}}

## Limitations

- Currently, replication of DDL (SQL-level changes such as creating or dropping tables or indexes) is not supported. To make these changes requires first performing the DDL operation (for example, creating a table), and then adding the new object to replication in YBA. Refer to [Manage tables and indexes](./disaster-recovery-tables/).

- DR setup (and other operations that require making a full copy from DR primary to DR replica, such as adding tables with data to replication, resuming replication after an extended network outage, and so on) may fail with the error `database "<database_name>" is being accessed by other users`.

    This happens because the operation relies on a backup and restore of the database, and the restore will fail if there are any open connections to the DR replica.

    To fix this, close any open SQL connections to the DR replica, delete the DR configuration, and perform the operation again.

- Setting up DR between a universe upgraded to v2.20.x and a new v2.20.x universe is not supported. This is due to a limitation of xCluster deployments and packed rows. See [Packed row limitations](../../../architecture/docdb/packed-rows/#limitations).

## Upgrading universes in DR

When [upgrading universes](../../manage-deployments/upgrade-software-install/) in DR replication, you should upgrade and finalize the DR replica before upgrading and finalizing the DR primary.

Note that switchover operations can potentially fail if the DR primary and replica are at different versions.

## xCluster DR vs xCluster replication

xCluster refers to all YugabyteDB deployments with two or more universes, and has two major flavors:

- _xCluster DR_. Provides turnkey workflow orchestration for applications using transactional SQL in an active-active single-master manner, with only unidirectional replication configured at any moment in time. xCluster DR uses xCluster Replication under the hood, and adds workflow automation and orchestration, including switchover, failover, resynchronization to make another full copy, and so on.
- _xCluster Replication_. Moves the data from one universe to another. Can be used for CQL, non-transactional SQL, bi-directional replication, and other deployment models not supported by xCluster DR.

xCluster DR targets one specific and common xCluster deployment model: [active-active single-master](../../../develop/build-global-apps/active-active-single-master/), unidirectional replication configured at any moment in time, for transactional YSQL.

- Active-active means that both universes are active - the primary universe for reads and writes, while the replica can handle reads only.

- Single master means that the application writes to only one universe (the primary) at any moment in time.

- Unidirectional replication means that at any moment in time, replication traffic flows in one direction, and is configured (and enforced) to flow only in one direction.

- Transactional SQL means that the application is using SQL (and not CQL), and write-ordering is guaranteed. Furthermore, transactions are guaranteed to be atomic.

xCluster DR adds higher-level orchestration workflows to this deployment to make the end-to-end setup, switchover, and failover of the DR primary to DR replica simple and turnkey. This orchestration includes the following:

- During setup, xCluster DR ensures that both universes have identical copies of the data (using backup and restore to synchronize), and configures the DR replica to be read-only.
- During switchover, xCluster DR waits for all remaining changes on the DR primary to be replicated to the DR replica before switching over.
- During both switchover and failover, xCluster DR also promotes the DR replica from read only to read and write, and demotes (when possible) the original DR primary from read and write to read only.

For all deployment models _other than_ active-active single-master, unidirectional replication configured at any moment in time, for transactional YSQL, use xCluster replication directly instead of xCluster DR.

For example, use xCluster replication for the following:

- Multi-master deployments, where you have two application instances, each one writing to a different universe.
- Active-active single-master deployments in which a single master application can freely write (without coordinating with YugabyteDB for failover or switchover) to either universe, because both accept writes.
- Non-transactional SQL. That is, SQL without write-order guarantees and without transactional atomicity guarantees.
- CQL

Note that a universe configured for xCluster DR cannot be used for xCluster replication, and vice versa. Although xCluster DR uses xCluster replication under the hood, xCluster DR replication is managed exclusively from the **xCluster Disaster Recovery** tab, and not on the **xCluster Replication** tab.

(As an alternative to xCluster DR, you can perform setup, failover, and switchover manually. Refer to [Set up transactional xCluster replication](../../../deploy/multi-dc/async-replication/async-transactional-setup/).)

For more information on xCluster replication in YugabyteDB, see the following:

- [xCluster replication: overview and architecture](../../../architecture/docdb-replication/async-replication/)
- [xCluster replication between universes in YugabyteDB](../../../deploy/multi-dc/async-replication/)
