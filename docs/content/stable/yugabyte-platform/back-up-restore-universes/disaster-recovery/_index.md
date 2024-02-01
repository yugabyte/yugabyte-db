---
title: Configure disaster recovery for a YugabyteDB Anywhere universe
headerTitle: Disaster recovery
linkTitle: Disaster recovery
description: Enable deployment using transactional (active-standby) replication between universes
headContent: Fail over to a replica universe in case of unplanned outages
image: /images/section_icons/manage/enterprise/upgrade_universe.png
menu:
  stable_yugabyte-platform:
    parent: back-up-restore-universes
    identifier: disaster-recovery
    weight: 90
type: indexpage
showRightNav: true
---

Use disaster recovery (DR) to recover from an unplanned outage (failover) or to perform a planned switchover. Planned switchover is commonly used for business continuity and disaster recovery testing and failback after a failover.

A DR configuration consists of the following:

- a primary universe, which serves both reads and writes.
- a DR replica universe, which can also serve reads.

Data from the primary is replicated asynchronously to the DR replica (which is read only). Due to the asynchronous nature of the replication, DR failover results in non-zero recovery point objective (RPO). In other words, data left behind on the original DR Primary can be lost during a failover. The amount of data loss depends on the replication lag, which in turn depends on the network characteristics between the universes. By contrast, during a switchover, RPO is zero and data is not lost, because the switchover waits for all to-be-replicated data to drain from the DR primary to the DR replica before switching over.

The recovery time objective (RTO) is very low, and determined by how long it takes applications to switch their connections from one universe to another. Applications should be designed in such a way that the switch happens as quickly as possible.

DR further allows for the role of each universe to switch during planned switchover and unplanned failover scenarios.

![Disaster recovery](/images/yb-platform/disaster-recovery/disaster-recovery.png)

<div class="row">

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="disaster-recovery-setup/">
      <div class="head">
        <img class="icon" src="/images/section_icons/explore/fault_tolerance.png" aria-hidden="true" />
        <div class="title">Set up Disaster Recovery</div>
      </div>
      <div class="body">
        Designate a universe to act as a DR replica.
      </div>
    </a>
  </div>

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="disaster-recovery-failover/">
      <div class="head">
        <img class="icon" src="/images/section_icons/explore/high_performance.png" aria-hidden="true" />
        <div class="title">Unplanned failover</div>
      </div>
      <div class="body">
        Fail over to the DR replica in case of an unplanned outage.
      </div>
    </a>
  </div>

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="disaster-recovery-switchover/">
      <div class="head">
        <img class="icon" src="/images/section_icons/manage/backup.png" aria-hidden="true" />
        <div class="title">Planned switchover</div>
      </div>
      <div class="body">
        Switch over to the DR replica for planned testing and failback.
      </div>
    </a>
  </div>

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="disaster-recovery-tables/">
      <div class="head">
        <img class="icon" src="/images/section_icons/architecture/concepts/replication.png" aria-hidden="true" />
        <div class="title">Add and remove tables and indexes</div>
      </div>
      <div class="body">
        Perform DDL changes to databases in replication.
      </div>
    </a>
  </div>

</div>

## Limitations

- Currently, DDL replication (that is, automatically handling SQL-level DDL changes such as creating or dropping tables or indexes) is not supported. To make these changes requires first performing the DDL operation (for example, creating a table), and then adding the new object to replication in YBA. Refer to [Manage tables and indexes](./disaster-recovery-tables/).

- DR setup (and other operations that require making a full copy from primary to DR replica, such as adding tables with data to replication, resuming replication after an extended network outage, and so on) may fail with the error `database "<database_name>" is being accessed by other users`.

    This happens because the operation relies on a backup and restore of the database, and the restore will fail if there are any open connections to the DR replica.

    To fix this, close any open SQL connections to the DR replica, delete the DR configuration, and perform the operation again.

## DR vs xCluster replication

Under the hood, YBA DR uses xCluster replication. xCluster is a deployment topology that provides asynchronous replication across two universes. xCluster replication can be set up to move data from one universe to another in one direction (for example, from primary to replica), or in both directions (bi-directional replication).

DR targets one specific and common xCluster deployment model: [active-active single-master](../../../develop/build-global-apps/active-active-single-master/), unidirectional replication configured at any moment in time, for transactional YSQL.

- Active-active means that both universes are active - the primary universe for reads and writes, while the replica can handle reads only.

- Single master means that the application writes to only one universe (the primary) at any moment in time.

- Unidirectional replication means that at any moment in time, replication traffic flows in one direction, and is configured (and enforced) to flow only in one direction.

- Transactional SQL means that the application is using SQL (and not CQL) and  write-ordering is guaranteed. Furthermore, transactions are guaranteed to be  atomic.

DR adds higher-level orchestration workflows to this deployment to make the end-to-end setup, switchover, and failover of a primary universe to a replica simple and turnkey. This orchestration includes the following:

- During setup, DR ensures that both clusters have identical copies of the data (using backup and restore to synchronize), and configures the DR replica to be read-only.
- During switchover, DR waits for the DR primary to drain before switching over.
- During both switchover and failover, DR also promotes the DR replica from read only to read and write, and demotes (when possible) the original DR Primary from read and write to read only.

For all deployment models other than active-active single-master, unidirectional replication configured at any moment in time, for transactional YSQL, use xCluster replication directly instead of DR.

For example, use xCluster replication for the following:

- Multi-master deployments, where you have two application instances, each one writing to a different universe.
- Active-active single-master deployments in which a single master application can freely write (without coordinating with YugabyteDB for failover or switchover) to either universe, because both accept writes.
- Non-transactional SQL. That is, SQL without write-order guarantees and without transactional atomicity guarantees.
- CQL

Note that a universe configured for DR cannot be used for xCluster replication, and vice versa. Although DR uses xCluster replication under the hood, DR replication configurations are hidden in the **xCluster Replication** tab (and visible only via the **Disaster Recovery** tab).

(As an alternative to DR, you can perform setup, failover, and switchover manually. Refer to [Set up transactional xCluster replication](../../../deploy/multi-dc/async-replication/async-transactional-setup/).)

For additional information on xCluster replication in YugabyteDB, see the following:

- [xCluster replication: overview and architecture](../../../architecture/docdb-replication/async-replication/)
- [xCluster replication between universes in YugabyteDB](../../../deploy/multi-dc/async-replication/)
