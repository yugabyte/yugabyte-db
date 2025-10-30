---
title: Set up transactional xCluster
headerTitle: Set up transactional xCluster
linkTitle: Setup
description: Setting up transactional (active-active single-master) replication between two YB universes
headContent: Set up transactional xCluster replication
menu:
  v2.25:
    parent: async-replication-transactional
    identifier: async-transactional-setup-1-automatic
    weight: 10
tags:
  other: ysql
type: docs
---


<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li >
    <a href="../async-transactional-setup-automatic/" class="nav-link active">
      Automatic
    </a>
  </li>
  <li >
    <a href="../async-transactional-setup-semi-automatic/" class="nav-link">
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
To use automatic-mode transactional xCluster replication, both the Primary and Standby universes must be running v2025.1, v2.25.1, or later.
{{< /note >}}

{{<tags/feature/ea idea="153">}}Automatic transactional xCluster replication handles all aspects of replication for both data and schema changes.

In particular, DDL changes made to the Primary universe are automatically replicated to the Standby universe.

{{< warning title="Warning" >}}

Not all DDLs can be automatically replicated yet; see [XCluster Limitations](../../../../architecture/docdb-replication/async-replication/#limitations).

{{< /warning >}}

In this mode, xCluster replication operates at the YSQL database granularity. This means you only run xCluster management operations when adding and removing databases from replication, and not when tables in the databases are created or dropped.

## Set up Automatic mode replication

{{% readfile "includes/automatic-setup.md" %}}

## Monitor replication

For information on monitoring xCluster replication, refer to [Monitor xCluster](../../../../launch-and-manage/monitor-and-alert/xcluster-monitor/).

## Add a database to a replication group

{{% readfile "includes/transactional-add-db.md" %}}

## Remove a database from a replication group

{{% readfile "includes/transactional-remove-db.md" %}}

{{< warning title="Warning" >}}

If you want the databases being removed from replication on the target
to be usable after dropping replication, you need to stop your workload
(including performing DDLs) to them and wait for the replication lag to
reach zero before dropping the replication group.

If you take no precautions then the target databases may be unusable; we
strongly recommend dropping such databases rather than attempting to use
them.

{{< /warning >}}

## Drop xCluster replication group

{{% readfile "includes/transactional-drop.md" %}}

{{< warning title="Be careful using this outside of the switchover or failover workflows" >}}

If you want the databases being replicated to on the target to be usable
after dropping replication, you need to stop your workload (including
performing DDLs) and wait for the replication lag to reach zero before
dropping the replication group.

Alternatively, you can follow the failover workflow to ensure the target
cuts over to a consistent time.

If you take no precautions then the target databases may be unusable; we
strongly recommend dropping such databases rather than attempting to use
them.

{{< /warning >}}

## Making DDL changes

{{< warning title="Warning" >}}

Not all DDLs can be automatically replicated yet; see [XCluster Limitations](../../../../architecture/docdb-replication/async-replication/#limitations).

{{< /warning >}}

DDL operations must only be performed on the Primary universe. All schema changes are automatically replicated to the Standby universe.
