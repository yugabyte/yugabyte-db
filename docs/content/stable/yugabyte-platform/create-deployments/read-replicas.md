---
title: Create a read replica cluster in YugabyteDB Anywhere (New experience)
headerTitle: Create a read replica cluster
linkTitle: Add read replica
description: Use YugabyteDB Anywhere to create a read replica cluster.
headcontent: Reduce read latencies in remote regions
menu:
  stable_yugabyte-platform:
    identifier: create-read-replica-1-new
    parent: create-deployments
    weight: 20
type: docs
---

{{< page-finder/head text="Deploy read replicas" subtle="across different products">}}
  {{< page-finder/list icon="/icons/database-hover.svg" text="YugabyteDB" url="../../../deploy/multi-dc/read-replica-clusters/" >}}
  {{< page-finder/list icon="/icons/server-hover.svg" text="YugabyteDB Anywhere" current="" >}}
  {{< page-finder/list icon="/icons/cloud-hover.svg" text="YugabyteDB Aeon" url="/stable/yugabyte-cloud/cloud-clusters/managed-read-replica/" >}}
{{< /page-finder/head >}}

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li>
    <a href="../read-replicas/" class="nav-link active">
      New UI
    </a>
  </li>

  <li>
    <a href="../read-replicas-classic/" class="nav-link">
      Classic UI
    </a>
  </li>
</ul>

{{<tags/ui/new>}} If your user base is geographically distributed, you can add a read replica cluster to the universe to improve read latency in regions that are far from your primary region.

{{% includeMarkdown "includes/read-replicas-intro.md" %}}

For how read replicas fit with other multi-region topologies, refer to [Topology](../create-universes-overview/#topology).

## Add a read replica

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

{{< tip title="Geographically distributed universes" >}}
When creating a geographically distributed universe, add the `leader_failure_max_missed_heartbeat_periods` configuration flag for YB-Master and YB-TServer with a value of 10. As the data is globally replicated, remote procedure call (RPC) latencies are higher. You can use this flag to increase the failure detection interval in high-RPC latency deployments.
{{< /tip >}}

### Summary and Cost

Review the read replica placement, hardware, and cost summary, then click **Create**.

After the task completes, open **Nodes** (or **Pods** for Kubernetes). Nodes are grouped into the primary cluster and read replicas; read replica nodes have a `readonly1` identifier appended to their name.

## Edit a read replica

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

## Delete a read replica

1. Navigate to the universe, then open **Settings > Placement**.
1. On the **Read Replica** card, open the menu and choose **Delete Read Replica**.
1. Confirm by entering the universe name, then click **Yes**.
