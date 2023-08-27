---
title: Create a read replica cluster
headerTitle: Create a read replica cluster
linkTitle: Read replica cluster
description: Use YugabyteDB Anywhere to create a read replica cluster.
menu:
  v2.14_yugabyte-platform:
    identifier: create-read-replica-cluster
    parent: create-deployments
    weight: 40
type: docs
---

You can create a universe that includes both a primary cluster and a [read replica](../../../architecture/docdb-replication/read-replicas/) cluster in a hybrid cloud deployment, as well as dynamically add, edit, and remove a read replica cluster. The example presented in this document shows how to deploy a universe with primary cluster in Oregon (US-West) and read replica cluster in Northern Virginia (US-East).

You can add up to 15 read replicas to a universe. The replication factor for the read replica cluster (that is, the number of read replicas) can't exceed the number of nodes in the read replica cluster.

{{< note title="Note" >}}
YugabyteDB Anywhere does not support read replica configuration for Kubernetes and OpenShift cloud providers.
{{< /note >}}

## Create the universe

You start by navigating to **Dashboard** and clicking **Create Universe**. Use the **Primary Cluster > Cloud Configuration** page to enter the following values to create a primary cluster on [GCP](../../configure-yugabyte-platform/set-up-cloud-provider/gcp/) provider:

- Enter a universe name as helloworld3.
- Enter the set of regions as Oregon.
- Set the replication factor to 3.
- Set instance type to n1-standard-8
- Add the configuration flag for YB-Master and YB-TServer as `leader_failure_max_missed_heartbeat_periods` 10. As the data is globally replicated, remote procedure call (RPC) latencies are higher. You can use this flag to increase the failure detection interval in such a high-RPC latency deployment.

  ![Create Primary Cluster on GCP](/images/ee/primary-cluster-creation.png)

The next step is to click **Configure Read Replica** and then specify the following on the **Read Replica > Cloud Configuration** page to create a read replica cluster on [AWS](../../configure-yugabyte-platform/set-up-cloud-provider/aws/):

- Enter the set of regions as US East.
- Set the replication factor to 3.
- Set the instance type to c4.large.

As you do not need to a establish a quorum for read replica clusters, the replication factor can be either even or odd.

To finish the process, click **Create**.

## Examine the universe

While the universe is being created, **Dashboard** should look similar to the following illustration:

![Universe Waiting to Create](/images/ee/universe-waiting.png)

After the universe has been created, **Dashboard** displays the primary and read replica cluster information, as well as shows the distinct clusters on the map.

### Universe nodes

To see a list of nodes, navigate to **Nodes**. Notice that the nodes are grouped into primary cluster and read replicas, and read replica nodes have a `readonly1` identifier appended to their name.

Navigate to the cloud provider's instances page. In GCP, browse to **Compute Engine > VM Instances** and search for instances that have `helloworld3` in their name. The following illustration shows the result corresponding to your primary cluster:

![Primary Cluster Instances](/images/ee/gcp-node-list.png)

In AWS, navigate to **Instances** and perform the same search. The following illustration shows the result corresponding to your read replica cluster:

![Read Replica Instances](/images/ee/aws-node-list.png)

This confirms that you created a hybrid cloud deployment with the primary cluster in GCP and the read replica cluster in AWS.

## Add, remove, edit a read replica cluster

YugabyteDB Anywhere allows you to dynamically add, modify, and remove a read replica cluster from an existing universe.

Create a new universe called helloworld4 with a primary cluster identical to helloworld3 but without any read replica cluster. Click **Create** and wait for the universe to be ready. After this is done, navigate to **Overview** and click **Actions > Add Read Replica**.

Use the **Cloud Configuration** page to enter the same information that you entered for the read replica cluster in helloworld3 and click **Add Read Replica**.

When done, open **Nodes** and verify that you have three new read replica nodes, all in AWS.

To edit the read replica cluster, once again click **Actions > Edit Read Replica**. Add a node to the cluster (availability zones are populated automatically) and click **Edit Read Replica**.

When the universe is ready, open **Nodes** to find the new read replica node for a total of four new nodes.

To delete the read replica cluster, open the **Cloud Configuration** page and click **Delete this configuration**.

Upon completion, navigate back to **Nodes** and verify that you only see the three primary nodes from the initial universe creation.
