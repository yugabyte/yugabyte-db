---
title: Place YB-Masters on dedicated nodes
headerTitle: Place YB-Masters on dedicated nodes
linkTitle: Dedicated YB-Masters
description: Use YugabyteDB Anywhere to create a universe with dedicated YB-Master nodes.
menu:
  stable_yugabyte-platform:
    identifier: dedicated-master
    parent: create-deployments
    weight: 30
type: docs
rightNav:
  hideH3: true
---

The default behavior when creating a universe is to locate [YB-Master](../../../architecture/yb-master/) and [YB-TServer](../../../architecture/yb-tserver/) processes on the same node. However, in some situations it's desirable to isolate the two processes on separate nodes, and dedicate additional resources to the YB-Master processes.

You can specify that YB-Masters be placed on dedicated nodes when creating or editing a universe.

Dedicated master placement can be used for universes using AWS, GCP, Azure, and On-Premises [provider configurations](../../configure-yugabyte-platform/); Kubernetes is not supported.

When planning a universe, see also [Dedicated masters](../create-universes-overview/#dedicated-masters).

Dedicated master placement does not apply to read replicas, which have only YB-TServers.

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

You can place YB-Master processes as follows:

- **Place Masters on the same nodes as T-Servers** (Shared): This is the default. In this mode, 15% of the total memory available on the node goes to YB-Master and 85% goes to YB-TServer.

    You can override the memory allocation using the [--default_memory_limit_to_ram_ratio](../../../reference/configuration/yb-tserver/#default-memory-limit-to-ram-ratio) flag for non-Kubernetes deployments. For Kubernetes deployments, memory limits are controlled via Kubernetes resource specifications in the Helm chart, and `--default_memory_limit_to_ram_ratio` does not apply.

- **Place Masters on dedicated nodes** (Dedicated Masters): In this mode, nodes dedicated to Master processes are selected when the universe is created (or equivalently, during the `/universe_configure` REST API call). Placing YB-Masters on dedicated nodes eliminates the need to configure or share memory.

For an existing universe, assigning new YB-Masters will start the new YB-Master nodes and stop any existing ones.

## Enable dedicated YB-Master nodes

You can enable dedicated YB-Master nodes when [deploying a universe](../../create-deployments/create-universes-wizard/). You can also enable and disable dedicated masters after deployment.

{{< tabpane text=true >}}

{{% tab header="New UI" lang="new" %}}

{{<tags/ui/new>}}To modify dedicated masters for a universe, navigate to the universe and do the following:

1. Click **Settings > Placement**, click **Advanced Placement Options** and **Master Server Node Allocation**.

1. Select the **Allocate dedicated nodes to master servers** option.

1. If you want to use a different instance type for master nodes (from that used for TServers), select **Edit Master Server Instance** and select an instance type for the master nodes.

{{% /tab %}}

{{% tab header="Classic UI" lang="classic" %}}

{{<tags/ui/classic>}}To modify dedicated masters for a universe, navigate to the universe and do the following:

1. Click **Actions > Edit Universe**.

1. Select the **Place Masters on dedicated nodes** option.

1. In the **Total Nodes** field, enter the number of **TServer** nodes. The **Master** field is always disabled because the number of master nodes is always equal to the **Replication Factor**.

1. For **Instance Configuration**, you can choose different instance types and volume sizes for the TServers and Masters.

{{% /tab %}}

{{< /tabpane >}}

YugabyteDB Anywhere provisions your new universe in a dedicated mode where you will be able to view separate YB-Master and YB-TServer nodes.
