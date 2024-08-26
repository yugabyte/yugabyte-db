---
title: Configure xCluster Replication for a YugabyteDB Anywhere universe
headerTitle: xCluster Replication
linkTitle: xCluster Replication
description: Enable xCluster Replication between universes
headContent: Active universe with standby using xCluster deployment
menu:
  stable_yugabyte-platform:
    parent: manage-deployments
    identifier: xcluster-replication
    weight: 90
aliases:
  - /stable/yugabyte-platform/create-deployments/async-replication-platform
type: indexpage
showRightNav: true
rightNav:
  hideH3: true
---

xCluster Replication is an asynchronous replication feature in YugabyteDB that allows you to replicate data between independent YugabyteDB universes. You can set up unidirectional (master-follower) or bidirectional (multi-master) replication between two data centers:

- Source - contains the original data that is subject to replication.
- Target - recipient of the replicated data.

![xCluster asynchronous replication](/images/architecture/replication/active-standby-deployment-new.png)

You can use xCluster Replication to implement disaster recovery for YugabyteDB. This is a good option where you have only two regions available, or the higher write latency of a [global database](../../../develop/build-global-apps/global-database/) is a problem. You do need to tolerate some small possibility of data loss due to asynchronous replication. xCluster Disaster Recovery (DR) adds high-level orchestration workflows to xCluster Replication to make end-to-end setup, switchover, and failover for disaster recovery simple and turnkey. For more details on using xCluster for disaster recovery, see [xCluster Disaster Recovery](../../back-up-restore-universes/disaster-recovery/).

xCluster Replication can be used to move data from one YugabyteDB universe to another for purposes other than disaster recovery. For example, downstream YugabyteDB universes used for reporting or "green" deployments of blue-green deployments can be kept asynchronously up to date with the main YugabyteDB universe.

You can use YugabyteDB Anywhere to set up xCluster Replication, monitor the status of replication, and manage changes to the replication when new databases or tables are added to the replication.

- For more information on how YugabyteDB xCluster Replication works, see [xCluster Replication architecture](../../../architecture/docdb-replication/async-replication/).
- For a comparison between xCluster DR and xCluster Replication in YugabyteDB Anywhere, see [xCluster DR vs xCluster Replication](../../back-up-restore-universes/disaster-recovery/#xcluster-dr-vs-xcluster-replication).
- For an example of unidirectional (master-follower) xCluster Replication, see [Active-active single-master](../../../develop/build-global-apps/active-active-single-master/).
- For an example of bidirectional (multi-master) xCluster Replication, see [Active-active multi-master](../../../develop/build-global-apps/active-active-multi-master/).

## xCluster configurations

xCluster Replication supports the following replication configurations:

- Transactional YSQL
- Non-transactional YCQL/YSQL
- Non-transactional bidirectional

For YSQL databases, transactional is recommended. This mode guarantees atomicity and consistency of transactions. The target universe is made read-only in this mode. If the target universe needs to support write operations, YSQL replication can be configured to use the non-transactional mode. However, this comes at the expense of SQL ACID guarantees. For more information on the inconsistencies that can arise with non-transactional YSQL, refer to [Inconsistencies affecting transactions](../../../architecture/docdb-replication/async-replication/#inconsistencies-affecting-transactions).

For YCQL databases, only non-transactional replication is supported.

Bidirectional replication refers to setting up xCluster Replication between two YSQL databases or YCQL tables on different universes in both directions, so that writes on either database or table can be replicated to the other database or table. Certain xCluster management operations need special attention in this case. See [Bidirectional replication](bidirectional-replication/).

For more information about transactional and non-transactional modes, see [Asynchronous replication modes](../../../architecture/docdb-replication/async-replication/#asynchronous-replication-modes).

{{<lead link="https://www.yugabyte.com/blog/distributed-database-transactional-consistency-async-standby">}}
Blog: [Can Distributed Databases Achieve Transactional Consistency on Async Standbys? Yes, They Can.](https://www.yugabyte.com/blog/distributed-database-transactional-consistency-async-standby/)
{{</lead>}}

{{<lead link="https://www.youtube.com/watch?v=lI6gw7ncBs8/">}}
Video: [YFTT - Transactional xCluster](https://www.youtube.com/watch?lI6gw7ncBs8)
{{</lead>}}

{{<index/block>}}

  {{<index/item
    title="Set up xCluster Replication"
    body="Designate a universe to act as a source."
    href="xcluster-replication-setup/"
    icon="fa-light fa-copy">}}

  {{<index/item
    title="Manage tables and indexes"
    body="Perform DDL changes to databases in replication."
    href="xcluster-replication-ddl/"
    icon="fa-light fa-table">}}

  {{<index/item
    title="Bidirectional"
    body="Configure bidirectional replication."
    href="bidirectional-replication/"
    icon="fa-light fa-arrows-left-right">}}

{{</index/block>}}

## Limitations

- Currently, replication of DDL (SQL-level changes such as creating or dropping tables or indexes) is not supported. To make these changes requires first performing the DDL operation (for example, creating a table), and then adding the new object to replication in YugabyteDB Anywhere. Refer to [Manage tables and indexes](./xcluster-replication-ddl/).

- xCluster Replication setup (and other operations that require making a [full copy](xcluster-replication-setup/#full-copy-during-xcluster-setup) from source to target) forcefully drop the tables on the target if they exist before performing the restore.

    If there are any open SQL connections to the database on the target, they will be interrupted and you should retry the connection.

- You can't add individual YSQL tables to replication; when adding YSQL tables or during xCluster setup, you must select the YSQL database with the tables you want, and _all_ tables in a selected database are added to the replication.

    To be eligible for xCluster Replication, YSQL tables must not already be in replication. That is, the table can't already be in another xCluster configuration between the same two universes in the same direction. If a YSQL database includes tables considered ineligible for replication, the database as a whole cannot be replicated.

- Setting up xCluster Replication between a universe earlier than or upgraded to v2.20.x, and a new v2.20.x universe is not supported. This is due to a limitation of xCluster deployments and packed rows. See [Packed row limitations](../../../architecture/docdb/packed-rows/#limitations).

- You can set up [change data capture](../../../explore/change-data-capture/) on a source universe in xCluster Replication, but not a target.

- xCluster Replication is not supported for [materialized views](../../../explore/ysql-language-features/advanced-features/views/#materialized-views).

For more information, refer to [xCluster implementation limitations](../../../architecture/docdb-replication/async-replication/#limitations).
