---
title: Create a multi-zone universe using YugabyteDB Anywhere
headerTitle: Create a multi-zone universe
linkTitle: Multi-zone universe
description: Use YugabyteDB Anywhere to create a YugabyteDB universe that spans multiple availability zones.
menu:
  preview_yugabyte-platform:
    identifier: create-multi-zone-universe
    parent: create-deployments
    weight: 20
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">

  <li>
    <a href="../create-universe-multi-zone/" class="nav-link active">
      <i class="fa-solid fa-building" aria-hidden="true"></i>
Generic</a>
  </li>

  <li>
    <a href="../create-universe-multi-zone-kubernetes/" class="nav-link">
      <i class="fa-regular fa-dharmachakra" aria-hidden="true"></i>
      Kubernetes
    </a>
  </li>

</ul>

YugabyteDB Anywhere allows you to create a universe in one geographic region across multiple availability zones using one of the cloud providers.

For specific scenarios such as creating large numbers of tables, high rates of DDL change, and so on, consider creating a universe with dedicated nodes for YB-Master processes. Refer to [Create a universe with dedicated nodes](../dedicated-master/) for more details.

## Prerequisites

Before you start creating a universe, ensure that you performed steps applicable to the cloud provider of your choice, as described in [Configure cloud provider](../../configure-yugabyte-platform/set-up-cloud-provider/aws/).

## Create a universe

If no universes have been created yet, the **Dashboard** does not display any.

Click **Create Universe** to create a universe and then enter your intent.

The **Provider**, **Regions**, and **Instance Type** fields are initialized based on the [configured cloud providers](../../configure-yugabyte-platform/set-up-cloud-provider/). When you provide the value in the **Nodes** field, the nodes are automatically placed across all the availability zones to guarantee the maximum availability.

To create a multi-zone universe using [Google Cloud provider (GCP)](../../configure-yugabyte-platform/set-up-cloud-provider/gcp/), perform the following:

- Enter a universe name (**helloworld1**).

- Enter the region (**Oregon**).

- Accept default values for all of the remaining fields (Master Placement,replication factor = 3, Total nodes = 3), as per the following illustration:

  ![Create Universe on GCP](/images/yp/create-uni-multi-zone-1.png)

- For **Instance Configuration**, change the instance type (**n1-standard-8**).

- Optionally, add configuration flags for your YB-Master and YB-TServer nodes. You can also set flags after universe creation. Refer to [Edit configuration flags](../../manage-deployments/edit-config-flags/).

- Click **Create**.

After the universe is ready, its **Overview** tab should appear similar to the following illustration:

![Multi-zone universe ready](/images/yp/multi-zone-universe-ready-1.png)

## Examine the universe

The **Universes** view allows you to examine various aspects of the universe:

- **Overview** provides the information on the current YugabyteDB Anywhere version, the number of nodes included in the primary cluster, the cost associated with running the universe, the CPU and disk usage, the geographical location of the nodes, the operations per second and average latency, the number of different types of tables, as well as the health monitor.
- **Tables** provides details about YSQL, YCQL, and YEDIS tables included in the universe. Table sizes are calculated across all the nodes in the cluster.
- **Nodes** provide details on nodes included in the universe and allows you to perform actions on a specific node (connect, stop, remove, display live and slow queries, download logs). You can also use **Nodes** to open the cloud provider's instances page. For example, in case of GCP, if you navigate to **Compute Engine > VM Instances** and search for instances that contain the name of your universe in the instances name, you should see a list of instances.
- **Metrics** displays graphs representing information on operations, latency, and other parameters for each type of node and server.
- **Queries** displays details about live and slow queries that you can filter by column and text.
- **Replication** provides information about any [asynchronous replication](../../create-deployments/async-replication-platform/) (also known as xCluster) in the universe.
- **Tasks** provides details about the state of tasks running on the universe, as well as the tasks that have run in the past against this universe.
- **Backups** displays information about scheduled backups, if any, and allows you to create, restore, and delete backups.
- **Health** displays the detailed performance status of the nodes and components involved in their operation. **Health** also allows you to pause health check alerts.
