---
title: Create a universe with dedicated nodes for YB-Master processes
headerTitle: Create a universe with dedicated nodes
linkTitle: Dedicated Master processes
description: Use YugabyteDB Anywhere to create a universe with dedicated YB-Master nodes.
menu:
  preview_yugabyte-platform:
    identifier: dedicated-master
    parent: create-deployments
    weight: 60
type: docs
---

While the default behavior when creating a universe is to colocate [YB-Master](../../../architecture/concepts/yb-master/) and [YB-TServer](../../../architecture/concepts/yb-tserver/) processes on the same node, there are some situations when it's desirable to isolate the two processes on separate nodes, and dedicate additional resources to the YB-Master processes.

The dedicated master nodes feature accomplishes this isolation, and is accessible via the [Place Masters on dedicated nodes](#colocated-to-dedicated-node-placement) option when creating a universe.

## Use cases

Following are some of the use cases where you may choose to place masters on dedicated nodes:

- A multi-tenant cluster comprising thousands of databases.
- A single database with 60000+ tables.
- A TPC-C benchmark exercise with a large number of warehouses.

YB-Master processes handle database metadata and coordinate operations across YB-TServers. For example, YB-Masters keep track of system metadata, coordinate DDL operations, handle tablet placement, coordinate data sharding and load balancing, and so on.

While these are normally lightweight operations and by default operate on shared hardware with the data-intensive YB-TServer processes, the preceding use cases are situations when a YB-Master process needs more resources, and it's desirable to dedicate nodes to YB-Masters.

### How many dedicated master nodes are required?

The number dedicated master nodes is dependent on the universe's [replication factor](../../../architecture/docdb-replication/replication/#replication-factor) (RF). A YugabyteDB universe with an RF of `N` requires `N` YB-Master process instances, and therefore `N` dedicated nodes for YB Masters.

## Colocated to dedicated node placement

Currently at universe creation time, two options are available for Master process placement:

- **Place Masters on the same nodes as T-Servers** (Colocated): In this mode, 15% of the total memory available on the node goes to YB-Master and 85% goes to YB-TServer. The memory allocation can be overridden using the `default_memory_limit_to_ram_ratio` flag.

- **Place Masters on dedicated nodes** (Dedicated Masters): In this mode, nodes being dedicated to Master processes are selected at the time of "Create Universe" (or equivalently, during the `/universe_configure` REST API call). Assigning dedicated YB-Master and YB-TServer nodes to the universe eliminates the need to configure or share memory.

For an existing universe, assigning new YB-Masters will start the new YB-master nodes and stop any existing ones.

{{< note >}}
The dedicated master placement feature:

- applies to universes created via most cloud providers (such as AWS, GCP, Azure, and On-Premises), except the Kubernetes cloud provider.
- does not apply to Read Replica clusters as it can have only YB-TServers.
{{< /note >}}

## Create a dedicated nodes universe

After you have configured a cloud provider, such as, for example [Google Cloud Provider](../../configure-yugabyte-platform/set-up-cloud-provider/gcp/) (GCP), you can use the YugabyteDB Anywhere UI and perform the following:

1. Navigate to **Admin** and select the **Global Configuration** tab.

1. Search for "dedicated" from the search box; the search result outputs **Enable dedicated nodes**.

1. Select **Actions** dropdown beside **Enable dedicated nodes**, click **Edit Configuration** and select **True** from the dropdown for **Config Value**.

1. Navigate to **Universes**, click **Create Universe**, and enter the following configuration details in the universe creation form:

    - In the **Name** field, enter a name for your cluster.

    - In the **Provider** field, select the cloud provider you configured.

    - In the **Regions** field, select the regions where you want to deploy TServer nodes.

    - In the **Master Placement** field, select the **Place Masters on dedicated nodes** checkbox.

    - In the **Total Nodes** field, enter **3** for **TServer**. The **Master** field is always disabled beacuse the number of master nodes is always equal to the Replication Factor.

    ![Create dedicated universe](/images/ee/create-dedicated-universe.png)

    - For **Instance Configuration**,

        - Select similar **Instance Type** for **TServer** and **Master**.

        - Select similar **Volume Info** for **TServer** and **Master**.

        - Select similar **EBS Type** for **TServer** and **Master**.

    - In the **YSQL password** field, enter a password and the same for **Confirm Password** field.

    - For **Authentication Settings**, disable the **Enable YCQL Auth** field.

    - In the **DB version** field, select the appropriate database version.

1. Click **Create**.

At this point, YugabyteDB Anywhere begins to provision your new universe in a dedicated mode where you will be able to view separate YB-Master and YB-Tserver nodes. When the universe is provisioned, it appears on the **Dashboard** and **Universes**. You can click the universe name to open its **Overview**.

![Dedicated universe overview](/images/ee/dedicated-universe-overview.png)

## Examine the universe

When the universe is created, you can access it via **Universes** or **Dashboard**.

To see a list of nodes that belong to this universe, select **Nodes**. You can also filter the nodes by selecting an option from the **Type** dropdown.

![Dedicated universe nodes](/images/ee/dedicated-universe-nodes.png)

You can verify the overall performance of the dedicated nodes universe by navigating to [Metrics](../../../yugabyte-platform/troubleshoot/universe-issues/#use-metrics).

![Dedicated universe metrics](/images/ee/dedicated-universe-metrics.png)
