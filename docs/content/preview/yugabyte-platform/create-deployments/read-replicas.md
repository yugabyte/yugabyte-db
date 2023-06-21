---
title: Create a read replica cluster in YugabyteDB Anywhere
headerTitle: Create a read replica cluster
linkTitle: Read replica cluster
description: Use YugabyteDB Anywhere to create a read replica cluster.
menu:
  preview_yugabyte-platform:
    identifier: create-read-replica-cluster
    parent: create-deployments
    weight: 40
type: docs
---

You can create a universe that includes both a primary cluster and a [read replica](../../../architecture/docdb-replication/read-replicas/) cluster, as well as dynamically add, edit, and remove a read replica cluster.

You can add up to 15 read replicas to a universe. The number of read replicas can't exceed the number of nodes in the read replica cluster.

{{< note title="Note" >}}
YugabyteDB Anywhere does not support read replica configuration for Kubernetes and OpenShift cloud providers.
{{< /note >}}

## Create a universe with a read replica

To create a universe with a read replica cluster, do the following:

1. Navigate to **Dashboard** and click **Create Universe**.
1. Use the **Primary Cluster** tab to enter the values to create a primary cluster. Refer to [Create a multi-zone universe](../create-universe-multi-zone/).

    {{< tip title="Tip" >}}

Add the `leader_failure_max_missed_heartbeat_periods` configuration flag for YB-Master and YB-TServer with a value of 10. As the data is globally replicated, remote procedure call (RPC) latencies are higher. You can use this flag to increase the failure detection interval in such a high-RPC latency deployment.

    {{< /tip >}}

1. Click **Configure Read Replica**.
1. Specify the following on the **Read Replica** tab to create a read replica cluster:

    - Specify the regions where you want to place replicas.
    - Specify the number of nodes and the number of read replicas. The number of nodes must be greater than or equal to the number of replicas.
    - Customize the availability zones if desired.
    - Configure the instance type to use for your read replica cluster.
    - You can choose to use the same flags as the primary cluster, or set custom flags for the read replica cluster. Read replicas only have YB-TServers. You can also set flags after universe creation. Refer to [Edit configuration flags](../../manage-deployments/edit-config-flags/).

1. To finish the process, click **Create**.

To see a list of nodes, navigate to **Nodes**. Notice that the nodes are grouped into primary cluster and read replicas, and read replica nodes have a `readonly1` identifier appended to their name.

## Add, remove, edit a read replica cluster

YugabyteDB Anywhere allows you to dynamically add, modify, and remove a read replica cluster from an existing universe.

To add a read replica to a universe, do the following:

1. Navigate to the universe and click **Actions > Add Read Replica**.
1. Use the **Configure read replica** page to enter the read replica details.
1. Click **Add Read Replica**.

To edit a read replica, do the following:

1. Navigate to the universe and click **Actions > Edit Read Replica**.
1. Use the **Configure read replica** page to enter the read replica details.
1. Click **Save**.

To delete a read replica cluster, do the following:

1. Navigate to the universe and click **Actions > Edit Read Replica**.
1. Click **Delete this configuration**.
