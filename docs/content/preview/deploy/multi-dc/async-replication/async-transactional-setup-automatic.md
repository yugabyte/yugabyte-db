---
title: Set up transactional xCluster
headerTitle: Set up transactional xCluster
linkTitle: Setup
description: Setting up transactional (active-active single-master) replication between two YB universes
headContent: Set up transactional xCluster replication
menu:
  preview:
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
      Manual
    </a>
  </li>
</ul>

{{< note title="Note" >}}
To use automatic transactional xCluster replication, both the Primary and Standby universes must be running v2.25.1 or later.
{{< /note >}}

{{<tags/feature/tp idea="153">}}Automatic transactional xCluster replication handles all aspects of replication for both data and schema changes.

In particular, DDL changes made to the Primary universe are automatically replicated to the Standby universe.

In this mode, xCluster replication operates at the YSQL database granularity. This means you only run xCluster management operations when adding and removing databases from replication, and not when tables in the databases are created or dropped.

## Set up Automatic mode replication

{{% readfile "includes/automatic-setup.md" %}}

## Monitor replication

For information on monitoring xCluster replication, refer to [Monitor xCluster](../../../../launch-and-manage/monitor-and-alert/xcluster-monitor/).

## Add a database to a replication group

{{% readfile "includes/transactional-add-db.md" %}}

## Remove a database from a replication group

{{% readfile "includes/transactional-remove-db.md" %}}

## Drop xCluster replication group

{{% readfile "includes/transactional-drop.md" %}}

## Making DDL changes

DDL operations must only be performed on the Primary universe. All schema changes are automatically replicated to the Standby universe.
