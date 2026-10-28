---
title: Create a read replica cluster in YugabyteDB Anywhere
headerTitle: Create a read replica cluster
linkTitle: Add read replica
description: Use YugabyteDB Anywhere to create a read replica cluster (Classic UI).
headcontent: Reduce read latencies in remote regions
menu:
  stable_yugabyte-platform:
    identifier: create-read-replica-2-classic
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
    <a href="../read-replicas/" class="nav-link">
      New UI
    </a>
  </li>

  <li>
    <a href="../read-replicas-classic/" class="nav-link active">
      Classic UI
    </a>
  </li>
</ul>

{{<tags/ui/classic>}} If your user base is geographically distributed, you can add a read replica cluster to the universe to improve read latency in regions that are far from your primary region.

{{% includeMarkdown "includes/read-replicas-intro.md" %}}

For how read replicas fit with other multi-region topologies, refer to [Topology](../create-universes-overview/#topology).

## Create a universe with a read replica cluster

To create a universe with a read replica cluster:

1. Navigate to **Dashboard** and click **Create Universe**.
1. Use the **Primary Cluster** tab to enter the values to create a primary cluster. Refer to [Create universes](../create-universe-multi-zone/).

1. Click **Configure Read Replica**.
1. Specify the following on the **Read Replica** tab to create a read replica cluster:

    - Specify the regions where you want to place replicas.
    - Specify the number of nodes and the number of read replicas. The number of nodes must be greater than or equal to the number of replicas.
    - Customize the availability zones if desired.
    - Choose the Linux version to be provisioned on the nodes of the replica cluster.
    - Configure the instance type to use for your read replica cluster.
    - You can choose to use the same flags as the primary cluster, or set custom flags for the read replica cluster. Read replicas only have YB-TServers. You can also set flags after universe creation. Refer to [Edit configuration flags](../../scale-deployments/edit-config-flags/).

        {{< tip title="Geographically distributed universes" >}}
When creating a geographically distributed universe, add the `leader_failure_max_missed_heartbeat_periods` configuration flag for YB-Master and YB-TServer with a value of 10. As the data is globally replicated, remote procedure call (RPC) latencies are higher. You can use this flag to increase the failure detection interval in high-RPC latency deployments.
        {{< /tip >}}


1. To finish the process, click **Create**.

To see a list of nodes, navigate to **Nodes**. Notice that the nodes are grouped into primary cluster and read replicas, and read replica nodes have a `readonly1` identifier appended to their name.

## Add a read replica to an existing universe

1. Navigate to the universe and click **Actions > Add Read Replica**.
1. Use the **Configure read replica** page to enter the read replica details.
1. Click **Add Read Replica**.

## Edit a read replica

1. Navigate to the universe and click **Actions > Edit Read Replica**.
1. Use the **Configure read replica** page to enter the read replica details.
1. Click **Save**.

## Delete a read replica

1. Navigate to the universe and click **Actions > Edit Read Replica**.
1. Click **Delete this configuration**.
