---
title: Create a read replica cluster in YugabyteDB Anywhere
headerTitle: Create a read replica cluster
linkTitle: Add read replica
description: Use YugabyteDB Anywhere to create a read replica cluster.
headcontent: Reduce read latencies in remote regions
menu:
  stable_yugabyte-platform:
    identifier: create-read-replica-cluster
    parent: create-deployments
    weight: 20
type: docs
---

{{< page-finder/head text="Deploy read replicas" subtle="across different products">}}
  {{< page-finder/list icon="/icons/database-hover.svg" text="YugabyteDB" url="../../../deploy/multi-dc/read-replica-clusters/" >}}
  {{< page-finder/list icon="/icons/server-hover.svg" text="YugabyteDB Anywhere" current="" >}}
  {{< page-finder/list icon="/icons/cloud-hover.svg" text="YugabyteDB Aeon" url="/stable/yugabyte-cloud/cloud-clusters/managed-read-replica/" >}}
{{< /page-finder/head >}}

If your user base is geographically distributed, you can add a read replica cluster to the universe to improve read latency in regions that are far from your primary region.

Read replicas are a read-only extension to the primary cluster. With read replicas, the primary data of the cluster is copied to one or more nodes in a different region. Read replicas do not add to write latencies because writes aren't synchronously replicated to replicas - the data is replicated to the replicas asynchronously. To read data from a read replica, you need to enable follower reads.

For more information on read replicas and follower reads in YugabyteDB, see the following:

- [Read replicas](../../../architecture/docdb-replication/read-replicas/)
- [Follower reads](../../../explore/going-beyond-sql/follower-reads-ysql/)

You can customize the number of read replicas in the read replica cluster. Multiple replicas ensure the availability of the replica in case of a node outage. Replicas do not participate in the primary cluster [Raft](../../../architecture/docdb-replication/replication/#raft-replication) consensus, and do not affect the fault tolerance of the primary cluster or contribute to failover. The number of read replicas can't exceed the number of nodes in the read replica cluster.

You can delete, modify, and scale read replica clusters. Adding or removing nodes incurs a load on the read replica cluster. Perform scaling operations when the cluster isn't experiencing heavy traffic. Scaling during times of heavy traffic can temporarily degrade performance and increase the length of time of the scaling operation.

## Limitations

- Currently, YugabyteDB Anywhere supports only one read replica cluster per universe.
- You can add up to 15 read replicas to the read replica cluster.

## Add a read replica

{{< tip title="Tip" >}}

When creating a geographically distributed universe, add the `leader_failure_max_missed_heartbeat_periods` configuration flag for YB-Master and YB-TServer with a value of 10. As the data is globally replicated, remote procedure call (RPC) latencies are higher. You can use this flag to increase the failure detection interval in such a high-RPC latency deployment.

{{< /tip >}}

{{< tabpane text=true >}}

{{% tab header="New UI" lang="new" %}}

In the New UI, you add a read replica after the primary universe exists. Create the primary universe first (see [Create universes](../create-universes-wizard/)), then use the **Add Read Replica** wizard.

To add a read replica cluster:

1. Navigate to the universe, then open **Settings > Placement**.

1. Click **Advanced Placement options > Add Read Replica**.

1. Follow the **Add Read Replica** wizard.

The wizard has the following pages:

1. [Regions and Availability Zones](#regions-and-availability-zones)
1. [Instance Settings](#instance-settings)
1. [Database Settings](#database-settings)
1. [Summary and Cost](#summary-and-cost)

### Regions and Availability Zones

Specify the placement of read replica nodes:

1. For each region, select the region and one or more availability zones.

1. For each availability zone, set the following:

    - **Nodes** (or **Pods** for Kubernetes) — Number of nodes to place in the zone.
    - **Replication Factor** — Number of copies of primary cluster data to maintain in this availability zone. This determines fault tolerance within the read replica; it does not change the primary cluster replication factor.

1. Optionally click **Add Availability Zone** to place nodes in additional zones in the region, or **Add Region** to place replicas in additional regions.

The total node count for the read replica is shown below the region cards.

### Instance Settings

Configure the instance used for read replica nodes (YB-TServers only):

- Select **Keep read replica instance settings same as primary cluster instance settings** to use the same instance type and storage as the primary cluster.

- To customize, clear that option and set CPU architecture, Linux version, instance type, and volume info as needed. For Kubernetes, you can set cores, memory, and volume info. For AWS, you can also configure EBS volume encryption when that feature is enabled.

For field details that match primary-cluster hardware settings, refer to [Hardware](../create-universes-wizard/#hardware) in Create universes.

### Database Settings

By default, YB-TServer flags from the primary cluster are applied to the read replica. Read replicas do not include YB-Master servers.

To set different flags for the read replica, enable **Customize Database Config Flags for Read Replica** and add or edit YB-TServer flags. You can also change flags later; refer to [Edit configuration flags](../../scale-deployments/edit-config-flags/).

### Summary and Cost

Review the read replica placement, hardware, and cost summary, then click **Create**.

After the task completes, open **Nodes** (or **Pods** for Kubernetes). Nodes are grouped into the primary cluster and read replicas; read replica nodes have a `readonly1` identifier appended to their name.

{{% /tab %}}

{{% tab header="Legacy UI" lang="legacy" %}}

### Create a universe with a read replica cluster

To create a universe with a read replica cluster:

1. Navigate to **Dashboard** and click **Create Universe**.
1. Use the **Primary Cluster** tab to enter the values to create a primary cluster. Refer to [Create a multi-zone universe](../create-universe-multi-zone/).

1. Click **Configure Read Replica**.
1. Specify the following on the **Read Replica** tab to create a read replica cluster:

    - Specify the regions where you want to place replicas.
    - Specify the number of nodes and the number of read replicas. The number of nodes must be greater than or equal to the number of replicas.
    - Customize the availability zones if desired.
    - Choose the Linux version to be provisioned on the nodes of the replica cluster.
    - Configure the instance type to use for your read replica cluster.
    - You can choose to use the same flags as the primary cluster, or set custom flags for the read replica cluster. Read replicas only have YB-TServers. You can also set flags after universe creation. Refer to [Edit configuration flags](../../scale-deployments/edit-config-flags/).

1. To finish the process, click **Create**.

To see a list of nodes, navigate to **Nodes**. Notice that the nodes are grouped into primary cluster and read replicas, and read replica nodes have a `readonly1` identifier appended to their name.

### Add a read replica to an existing universe

1. Navigate to the universe and click **Actions > Add Read Replica**.
1. Use the **Configure read replica** page to enter the read replica details.
1. Click **Add Read Replica**.

{{% /tab %}}

{{< /tabpane >}}

## Edit a read replica

{{< tabpane text=true >}}

{{% tab header="New UI" lang="new" %}}

You can change placement, instance settings, and flags independently.

### Edit placement

1. Navigate to the universe, then open **Settings > Placement**.
1. On the **Read Replica** card, open the menu and choose **Edit Placement**.
1. Update regions, availability zones, node counts, and replication factor as needed.
1. Click **Apply Changes**.

### Edit instance settings

1. Navigate to the universe, then open **Settings > Hardware**.
1. On the **Read Replica Instance** card, click **Edit**.
1. Keep settings the same as the primary cluster, or customize the instance and storage.
1. Confirm and apply the changes.

### Edit configuration flags

1. Navigate to the universe, then open **Settings > Database**.
1. Under **Advanced Config Flags**, select **Read Replica**, then click **Edit** to update YB-TServer flags.

You can also edit flags as described in [Edit configuration flags](../../scale-deployments/edit-config-flags/).

{{% /tab %}}

{{% tab header="Legacy UI" lang="legacy" %}}

1. Navigate to the universe and click **Actions > Edit Read Replica**.
1. Use the **Configure read replica** page to enter the read replica details.
1. Click **Save**.

{{% /tab %}}

{{< /tabpane >}}

## Delete a read replica

{{< tabpane text=true >}}

{{% tab header="New UI" lang="new" %}}

1. Navigate to the universe, then open **Settings > Placement**.
1. On the **Read Replica** card, open the menu and choose **Delete Read Replica**.
1. Confirm by entering the universe name, then click **Yes**.

{{% /tab %}}

{{% tab header="Legacy UI" lang="legacy" %}}

1. Navigate to the universe and click **Actions > Edit Read Replica**.
1. Click **Delete this configuration**.

{{% /tab %}}

{{< /tabpane >}}
