---
title: Create a universe with dedicated nodes for YB-Master processes
headerTitle: Place YB-Masters on dedicated nodes
linkTitle: Dedicated YB-Masters
description: Use YugabyteDB Anywhere to create a universe with dedicated YB-Master nodes.
menu:
  v2025.1_yugabyte-platform:
    identifier: dedicated-master
    parent: create-deployments
    weight: 60
type: docs
rightNav:
  hideH3: true
---

The default behavior when creating a universe is to locate [YB-Master](../../../architecture/yb-master/) and [YB-TServer](../../../architecture/yb-tserver/) processes on the same node. However, in some situations it's desirable to isolate the two processes on separate nodes, and dedicate additional resources to the YB-Master processes.

To place YB-Masters on dedicated nodes, you use the [Place Masters on dedicated nodes](#shared-and-dedicated-node-placement) option when creating a universe.

## Use cases

YB-Master processes handle database metadata and coordinate operations across YB-TServers. This includes keeping track of system metadata, coordinating DDL operations, handling tablet placement, coordinating data sharding and load balancing, and so on.

While these are normally lightweight operations and by default operate on shared hardware with the data-intensive YB-TServer processes, some situations require more resources for the YB-Master process, and you may want to dedicate nodes to the YB-Masters.

For example, the following use cases may benefit from placing masters on dedicated nodes:

- A multi-tenant cluster comprising thousands of databases.
- A single database with 60000+ tables.
- A TPC-C benchmark exercise with a large number of warehouses.

### How many dedicated master nodes are required?

A YugabyteDB universe requires a number of YB-Master servers equal to the [replication factor](../../../architecture/docdb-replication/replication/#replication-factor) (RF). A YugabyteDB universe with an RF of `N` requires `N` YB-Masters, and therefore `N` dedicated nodes for YB-Masters.

## Shared and dedicated node placement

When creating a universe, you have the following two options for YB-Master process placement:

- **Place Masters on the same nodes as T-Servers** (Shared): This is the default. In this mode, 15% of the total memory available on the node goes to YB-Master and 85% goes to YB-TServer. (You can override the memory allocation using the [--default_memory_limit_to_ram_ratio](../../../reference/configuration/yb-tserver/#default-memory-limit-to-ram-ratio) flag.)

- **Place Masters on dedicated nodes** (Dedicated Masters): In this mode, nodes dedicated to Master processes are selected when the universe is created (or equivalently, during the `/universe_configure` REST API call). Placing YB-Masters on dedicated nodes eliminates the need to configure or share memory.

For an existing universe, assigning new YB-Masters will start the new YB-Master nodes and stop any existing ones.

Dedicated master placement can be used when creating universes using AWS, GCP, Azure, and On-Premises [provider configurations](../../configure-yugabyte-platform/); Kubernetes is not supported.

Dedicated master placement does not apply to read replicas, which have only YB-TServers.

## Create a universe with dedicated YB-Master nodes

To create a universe with dedicated YB-Master nodes, you create a universe as you would normally (refer to [Create a multi-zone universe](../create-universe-multi-zone/)), with the following differences:

1. For **Master Placement**, select the **Place Masters on dedicated nodes** option.

    ![Create dedicated universe](/images/yp/create-deployments/create-dedicated-universe.png)

1. In the **Total Nodes** field, enter the number of **TServer** nodes. The **Master** field is always disabled because the number of master nodes is always equal to the **Replication Factor**.

1. For **Instance Configuration**, you can choose different instance types and volume sizes for the TServers and Masters.

YugabyteDB Anywhere provisions your new universe in a dedicated mode where you will be able to view separate YB-Master and YB-TServer nodes.

## Examine the universe

When the universe is created, you can access it via **Universes** or **Dashboard**.

To see a list of nodes that belong to this universe, select **Nodes**. You can also filter the nodes by selecting an option from the **Type** dropdown.

![Dedicated universe nodes](/images/yp/create-deployments/dedicated-universe-nodes.png)

You can verify the overall performance of the dedicated nodes universe by navigating to [Metrics](../../alerts-monitoring/anywhere-metrics/).

![Dedicated universe metrics](/images/yp/create-deployments/dedicated-universe-metrics.png)
