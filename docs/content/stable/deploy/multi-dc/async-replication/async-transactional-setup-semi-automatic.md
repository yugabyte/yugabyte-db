---
title: Set up transactional xCluster (Semi-automatic)
headerTitle: Set up transactional xCluster
linkTitle: Setup
description: Semi-automatic setup of transactional (active-active single-master) replication between two YB universes
headContent: Set up transactional xCluster replication
aliases:
  - /stable/deploy/multi-dc/async-replication/async-transactional-setup-dblevel/
menu:
  stable:
    parent: async-replication-transactional
    identifier: async-transactional-setup-2-semi-automatic
    weight: 10
tags:
  other: ysql
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li >
    <a href="../async-transactional-setup-automatic/" class="nav-link">
      Automatic
    </a>
  </li>
  <li >
    <a href="../async-transactional-setup-semi-automatic/" class="nav-link active">
      Semi-Automatic
    </a>
  </li>
  <li >
    <a href="../async-transactional-setup-manual/" class="nav-link">
      Fully Manual
    </a>
  </li>
</ul>

{{< note title="Note" >}}
For v2025.2.1 and later, use Automatic mode.

To use semi-automatic transactional xCluster replication, both the Primary and Standby universes must be running v2024.1.2 or later.
{{< /note >}}

Semi-automatic transactional xCluster replication simplifies the operational complexity of managing replication and making DDL changes.

In this mode, xCluster replication operates at the YSQL database granularity. This means you only run xCluster management operations when adding and removing databases from replication, and not when tables in the databases are created or dropped.

In particular, [DDL changes](#making-ddl-changes) don't require the use of yb-admin. This means DDL changes can be made by any database administrator or user with database permissions, and don't require SSH access or intervention by an IT administrator.

## Set up Semi-automatic mode replication

{{% readfile "includes/semi-automatic-setup.md" %}}

## Monitor replication

For information on monitoring xCluster replication, refer to [Monitor xCluster](../../../../launch-and-manage/monitor-and-alert/xcluster-monitor/).

## Add a database to a replication group

{{% readfile "includes/transactional-add-db.md" %}}

## Remove a database from a replication group

{{% readfile "includes/transactional-remove-db.md" %}}

## Drop xCluster replication group

{{% readfile "includes/transactional-drop.md" %}}

## Making DDL changes

When performing any DDL operation on databases using semi-automatic transactional xCluster replication (such as creating, altering, or dropping tables, indexes, or partitions), do the following:

1. Execute the DDL on Primary.
1. Execute the DDL on Standby.

The xCluster configuration is updated automatically. You can insert data into the table as soon as it is created on Primary.

When new tables are created with CREATE TABLE, CREATE INDEX, or CREATE TABLE PARTITION OF on the Primary universe, new streams are automatically created. Because this happens alongside the DDL, the new tables are checkpointed at the start of the WAL.

When the same DDL is run on the Standby, the stream info is automatically fetched from the Primary and the table is added to replication using the pre-created stream.

Similarly on table drop, after both sides drop the table, the streams are automatically removed.

When making DDL changes, keep in mind the following:

- DDLs have to be executed on the Standby universe in the same order in which they were executed on the Primary universe.
- When executing multiple schema changes for a given table, each DDL has to be run on both universes before the next DDL/schema change can be performed.
