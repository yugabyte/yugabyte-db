---
title: Create a universe with dedicated node placement
headerTitle: Create a universe with dedicated node placement
linkTitle: Dedicated node placement
description: Use YugabyteDB Anywhere to create a YugabyteDB universe with dedicated YB-Master nodes.
menu:
  preview_yugabyte-platform:
    identifier: dedicated-master
    parent: create-deployments
    weight: 60
type: docs
---

A YugabyteDB cluster requires a [replication factor](../../../architecture/docdb-replication/replication/#replication-factor)(RF) of the number of [YB-Master](../../../architecture/concepts/yb-master/) instances and at least the same RF of [YB-TServer](../../../architecture/concepts/yb-tserver/) instances for proper operation. For example, an RF-3 universe requires 3 YB-Master instances and 3 or more YB-TServer instances for proper operation.

By default, YugabyteDB Anywhere places YB-Master and YB-TServer instances on the same nodes in a universe. However, there are certain [use cases](#use-cases) where it is preferred to assign dedicated nodes for YB-Masters. To create a universe with this configuration, you can choose the number of YB-Master nodes based on the replication factor.

## Colocated to dedicated node placement

Currently it is possible to select the Master placement mode at universe creation time, as well as switch between modes for existing universes. Following are the two modes:

- **Place Masters on the same nodes as T-Servers** (Colocated): In this mode, 15% of the total memory available on the node goes to YB-Master and 85% goes to YB-TServer. The memory allocation can be overridden using the `default_memory_limit_to_ram_ratio` flag.

- **Place Masters on dedicated nodes** (Dedicated Masters): In this mode, nodes being dedicated to Master processes are selected at the time of "Create Universe" (or equivalently, during the `/universe_configure` REST API call). Assigning dedicated YB-Master and YB-TServer nodes to the universe eliminates the need to configure or share memory.

For an existing universe, assigning new YB-Masters will start the new YB-master nodes and stop any existing ones.

{{< note >}}
The dedicated master placement feature:

- applies to all cloud providers, On-Premises, but not Kubernetes.
- does not apply to Read Replica clusters as it can have only YB-TServers.
{{< /note >}}

### Use cases

YB-Master instances keeps track of system metadata, coordinates DDL operations, handles tablet placement, coordinates data load balancing, and so on. While these are lightweight operations, there are use cases where YB-Master needs more resources, and placing YB-Masters on dedicated nodes is important for scalability reasons.

Following are some of the use cases where you can create a universe with dedicated masters:

- A Multi-tenant cluster comprising of thousands of databases.
- A single database with 60000+ tables.
- A TPC-C benchmark exercise with large number of warehouses.

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

## Run the TPC-C benchmark and verify metrics

To run the TPC-C benchmark on your universe, use commands similar to the following (with your own IP addresses):

```sh
./tpccbenchmark -c config/workload_all.xml \
    --create=true \
    --nodes=10.9.4.142,10.14.16.8,10.9.13.138,10.14.16.9,10.152.0.14,10.152.0.32 \
    --warehouses 50 \
    --loaderthreads 10
./tpccbenchmark -c config/workload_all.xml \
    --load=true \
    --nodes=10.9.4.142,10.14.16.8,10.9.13.138,10.14.16.9,10.152.0.14,10.152.0.32 \
    --warehouses 50 \
    --loaderthreads 10
./tpccbenchmark -c config/workload_all.xml \
    --load=true \
    --nodes=10.9.4.142,10.14.16.8,10.9.13.138,10.14.16.9,10.152.0.14,10.152.0.32 \
    --warehouses 50
```

Refer to [TPC-C](../../../benchmark/tpcc-ysql/) for details.

You can verify the overall performance of the dedicated nodes universe by navigating to [Metrics](../../../yugabyte-platform/troubleshoot/universe-issues/#use-metrics).

![Dedicated universe metrics](/images/ee/dedicated-universe-metrics.png)
