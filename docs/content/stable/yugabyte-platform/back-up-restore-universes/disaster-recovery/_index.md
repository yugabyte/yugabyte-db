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

![Disaster recovery](/images/deploy/xcluster/xcluster-transactional.png)

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

- If there are any connections open against the DR replica databases and DR is being set up for a DR primary database that already has some data, then DR setup will fail with an expected error. This is because setting up DR requires backing up the primary database and restoring the backup to the target database after cleaning up any pre-existing data on the target side. Close any connections to the DR replica database and retry the setup operation.

    ```output
    Error ERROR:  database "database_name" is being accessed by other users
    DETAIL:  There is 1 other session using the database.
    ```

- Currently, DDL replication (that is, automatically handling SQL-level DDL changes such as creating or dropping tables or indexes) is not supported. To make these changes requires first performing the DDL operation (for example, creating a table), and then adding the new object to replication in YBA. Refer to [Manage tables and indexes](./disaster-recovery-tables/).

## DR vs xCluster replication

DR is a superset of xCluster functionality. xCluster focuses on moving data from a primary universe to a replica. DR adds higher-level orchestration workflows to make the end-to-end setup, switchover, and failover of a primary universe to a replica simpler. Note that a universe configured for DR can't be used for xCluster replication, and although DR uses xCluster replication, DR replications are not shown in the **xCluster Replication** tab.

(When in transactional YSQL mode, you can perform setup, failover, and switchover manually using a combination of xCluster replication and yb-admin CLI commands. Refer to [Set up transactional xCluster replication](../../../deploy/multi-dc/async-replication/async-transactional-setup/).)

For additional information on xCluster replication in YugabyteDB, see the following:

- [xCluster replication: overview and architecture](../../../architecture/docdb-replication/async-replication/)
- [xCluster replication between universes in YugabyteDB](../../../deploy/multi-dc/async-replication/)

DR does have some limitations versus xCluster - DR handles a narrower set of scenarios, while xCluster is more flexible. Whether you use DR or xCluster will depend on your use case.

### SQL applications

| Application Behavior | Scenario | xCluster | Disaster Recovery |
| :------------------- | :------- | :------- | :---------------- |
| Uni-directional&nbsp;replication | No transactions or write-order guarantees | Yes | No |
| | Transactions and/or write-order guarantees | Yes | Yes |
| Bi-directional replication<br>For active/active applications writing to both sides | No transactions or write-order guarantees<ul><li>last writer wins semantics</li><li>application must handle inconsistencies</li></ul> | Yes | Yes, with special instructions for handling failovers (switchovers are irrelevant) |
| | Transactions and/or write-order guarantees | No<sup>1</sup> | No<sup>1</sup> |

<sup>1</sup>YugabyteDB doesn't support transactions and write-ordering for bi-directional SQL.

### CQL applications

| Application Behavior | Scenario | xCluster | Disaster&nbsp;Recovery |
| :------------------- | :------- | :------- | :---------------- |
| Uni-directional&nbsp;replication | No transactions or write-order guarantees | Yes | No (DR only supports SQL) |
| | Transactions and/or write-order guarantees | N/A<sup>1</sup> | N/A<sup>1</sup> |
| Bi-directional replication<br>For active/active applications<br>writing to both sides, with last writer wins semantics | | Yes | N/A<sup>2</sup> |

<sup>1</sup>CQL doesn't support transactions.
<sup>2</sup>Failover and switchover doesn't require any of the additional orchestration steps that DR provides.
