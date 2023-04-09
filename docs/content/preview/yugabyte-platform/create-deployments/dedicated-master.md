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

A YugabyteDB cluster requires a [RF](../../../architecture/docdb-replication/replication/#replication-factor) of YB-Master instances and at least the same RF of YB-TServer instances for proper operation. For example, a RF-3 universe requires 3 YB-Master instances and 3 or more YB-TServer instances for proper operation.

By default, YugabyteDB Anywhere places [YB-Master](../../../architecture/concepts/yb-master/) and [YB-TServer](../../../architecture/concepts/yb-tserver/) instances on the same nodes in a universe. However, there are certain [use cases](#use-cases) when it is preferred to assign dedicated nodes for YB-Masters. To create a universe with this configuration, you can choose the number of YB-Master nodes based on the [replication factor].

## Switching to dedicated node placement

Creating or modifying an existing universe with dedicated nodes for YB-Masters involves the following changes:

- Every node is assigned a specific process type (YB-Master or YB-TServer).
- You can choose different instance types and disk configurations for YB-Master and YB-TServer nodes.
- In case of a universe where YB-Master and YB-TServers are on the same nodes, 10% of the total memory available on the node goes to YB-Master and 85% goes to YB-TServer. The memory allocation can be overridden using the `default_memory_limit_to_ram_ratio` flag, however, assigning dedicted YB-Master and YB-TServer nodes to the universe eliminates the need to configure/share memory.

Note that currently it is possible to select modes at universe creation time, as well as switch between modes for existing universes. For an existing universe, assigning new YB-Masters will start the new YB-master nodes and stop any existing ones.

## Use cases

YB-Master instances keeps track of system metadata, coordinates DDL operations, handles tablet placement, coordinates data load balancing, and so on. While these are lightweight operations, there are use cases where YB-Master needs more resources, and placing YB-Masters on dedicated nodes is important for scalability reasons.

Following are some of the use cases where you can create a universe with dedicated node placement:

- A Multi-tenant cluster comprising of thousands of databases.
- A single database with 60000+ tables.
- A TPC-C benchmark exercise with large number of warehouses.

## Create a dedicated nodes universe

After you have configured a cloud provider, such as, for example [Google Cloud Provider](../../configure-yugabyte-platform/set-up-cloud-provider/gcp/) (GCP), you can use the YugabyteDB Anywhere UI and perform the following:

1. Navigate to **Admin** and select the **Global Configuration** tab.

1. Search for "dedicated" from the search box; the search result outputs **Enable dedicated nodes**.

1. Select **Actions** dropdown beside **Enable dedicated nodes**, click **Edit** and select **True** from the dropdown.

1. Navigate to **Universes**, click **Create Universe**, and enter the following configuration details in the universe creation form:

    - In the **Name** field, enter **helloworld2**.

    - In the **Provider** field, select the cloud provider you configured.

    - Use the **Regions** field to enter **Oregon** and **South Carolina**.

    - In the **Master Placement** field, select the **Place Masters on dedicated nodes** checkbox.

    - In the **Total Nodes** field, enter **3** for **YB-TSever** and select

    - In the **Replication Factor** field, enter **3**.

    - For **Instance configuration**,

        - Select similar instance type between YB-TServer and YB-Master.

        - Select similar Volume info between YB-TServer and YB-Master.

        - Select similar EBS Type between YB-TServer and YB-Master.

    - In the **YSQL password** field, enter a password and the same for **Confirm Password** field.

    - Disable the **Enable YCQL Auth** field.

    - In the **DB version** field, select the appropriate database version.

    - 1. Click **Create**.

At this point, YugabyteDB Anywhere begins to provision your new universe in a dedicated mode where you will be able to view separate YB-Master and YB-Tserver nodes. When the universe is provisioned, it appears on the **Dashboard** and **Universes**. You can click the universe name to open its **Overview**.
