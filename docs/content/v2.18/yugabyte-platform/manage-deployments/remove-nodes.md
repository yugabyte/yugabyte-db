---
title: Use YugabyteDB Anywhere to eliminate an unresponsive node
headerTitle: Eliminate an unresponsive node
linkTitle: Eliminate an unresponsive node
description: Use YugabyteDB Anywhere to eliminate an unresponsive node.
menu:
  v2.18_yugabyte-platform:
    identifier: remove-nodes
    parent: manage-deployments
    weight: 20
type: docs
---

If a virtual machine or a physical server in a universe reaches its end of life and has unrecoverable hardware or other system issues, such as problems with its operating system, disk, and so on, it is detected and displayed in the YugabyteDB Anywhere UI as an unreachable node, as per the following illustration:

![Unreachable Node Actions](/images/ee/node-actions-unreachable.png)

When this happens, new Master leaders are elected for the underlying data shards, but since the universe enters a partially under-replicated state, it would not be able to tolerate additional failures. To remedy the situation, you can eliminate the unreachable node by taking actions in the following sequence:

- Step 1: [Remove node](#remove-node)
- Step 2: [Start a new Master process](#start-a-new-master-process), if necessary
- Step 3: [Release node instance](#release-node-instance)
- Step 4: [Delete node](#delete-node)

{{< note title="Note" >}}

A node status displayed in the UI is not always entirely indicative of the node's internal state. For example, a node whose status is shown as Unreachable can have various internal states, some of which are recoverable and others are not.

{{< /note >}}

## Remove node

To perform the remove action on the **yb-15-aws-ys-n6** node, click its corresponding **Actions > Remove Node**. This changes the value in the **Status** column from **Unreachable** to **Removed** and prevents access to data from this node.

There are no values in the **Master** and **TServer** columns for the **yb-15-aws-ys-n6** node. Leader elections also occur for tablets for which this node was the leader tablet server.

The action to remove a node is available from the following internal states of the node:

- ToJoinCluster

- Live
- Stopped
- ToBeRemoved

Taking this action transfers the node to a Removing and then Removed internal state, as follows:

<!--

Comment by Liza: this bullet point was replaced by Sanketh

1. If the node is a Master leader, it is removed (the Master process is stopped and there is a wait for the new Master leader).

-->

1. If the node is running a Master process, the Master is removed from the Master quorum. The Master process is stopped if the node is still reachable. YugabyteDB Anywhere waits for a new Master leader to be elected. At this point, there is less than a replication factor (RF) number of Masters running, which affects the resilience of the Master quorum. For information on how to restore resilience, see [Start a new Master process](#start-a-new-master-process).
2. The TServer is marked as blacklisted on the Master leader.
3. There is a wait for tablet quorums to remove the blacklisted TServer.
4. Data migration is performed and the TServer process stops only if it is reachable.
5. The node is marked as having neither Master nor TServer processes.
6. DNS entries are updated.

The node removal action results in the instance still being allocated but not involved in any schemas.

## Start a new Master process

A typical universe has an RF of 3 or 5. At the end of the [node removal](#remove-node) action, the Master quorum is reduced to (RF - 1) number of Masters. To restore the resilience of the Master quorum, you can start a new Master process, as follows:

1. Select a node in the same availability zone as the removed node.

2. Click **Actions > Start Master** corresponding to the node, as per the following illustration.

   This action is only available if there are additional nodes in the same availability zone and these nodes do not have a running Master process.

![Start master](/images/yp/start-master.png)

When you execute the start Master action, YugabyteDB Anywhere performs the following:
1. Configures the Master on the subject node.

2. Starts a new Master process on the subject node (in Shell mode).

3. Adds the new Master to the existing Master quorum.

4. Updates the Master addresses g-flag on all other nodes to inform them of the new Master.


## Release node instance

To release the IP address associated with the **yb-15-aws-ys-n6** node, click its corresponding **Actions > Release Instance**. This changes the value in the **Status** column from **Removed** to **Decommissioned**.

The **Release Instance** action releases the node instances in a cloud or frees up on-premise nodes.

The action to release a node instance is available from the Removed internal state of the node.

Taking this action transfers the node to a BeingDecommissioned and then Decommissioned internal state, as follows:

1. There is a wait for Master leader.
2. This server is removed from blacklist on Master leader (blacklisting is described in [Remove node](#remove-node)).
3. The Master and TServer server tasks are stopped, if they have not been stopped already.
4. The instance is destroyed.
5. DNS entries are updated.
6. Prometheus rules are updated and instructed to stop gathering metrics from this instance.

You can recover a node whose **Status** column displays **Decommissioned** by following instructions provided in [Recover a node](../add-nodes/).

## Delete node

You can perform the delete action via **Actions** corresponding to the node.

The action to delete a node is available from the following internal states of the node:

- ToBeAdded
- SoftwareInstalled
- Adding
- Decommissioned

Taking this action completely eliminates the node, as follows:

1. Removes the node record from the universe metadata.
2. Updates metadata in the database only.
