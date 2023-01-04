---
title: Use YugabyteDB Anywhere to eliminate an unresponsive node
headerTitle: Eliminate an unresponsive node
linkTitle: Eliminate an unresponsive node
description: Use YugabyteDB Anywhere to eliminate an unresponsive node.
menu:
  v2.14_yugabyte-platform:
    identifier: remove-nodes
    parent: manage-deployments
    weight: 20
type: docs
---

If a virtual machine or a physical server in a universe reaches its end of life and has unrecoverable hardware or other system issues, such as problems with its operating system, disk, and so on, it is detected and displayed in the YugabyteDB Anywhere UI as an unreachable node, as per the following illustration:

![Unreachable Node Actions](/images/ee/node-actions-unreachable.png)

When this happens, new Master leaders are elected for the underlying data shards, but since the universe enters a partially under-replicated state, it would not be able to tolerate additional failures. To remedy the situation, you can eliminate the unreachable node by taking actions in the following sequence:

- Step 1: [Remove node](#remove-node)
- Step 2: [Start a new master](#start-a-new-master) if necessary
- Step 3: [Release instance](#release-instance)
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

1. If the node is running a Master process, the Master is removed from the master quorum. The master process is stopped if the node is still reachable. YBA waits for a new master leader to be elected. Note that, at this point, there are less than RF (replication factor) number of masters running, so the resilience of the master quorum is impacted.
2. The TServer is marked as blacklisted on the Master leader.
3. There is a wait for tablet quorums to remove the blacklisted TServer.
4. Data migration is performed and the TServer process stops only if it is reachable.
5. The node is marked as having neither Master nor TServer processes.
6. DNS entries are updated.

The node removal action results in the instance still being allocated but not involved in any schemas.

## Start a new master

At the end of the Remove Node action above, the master quorum is reduced to (RF - 1) masters, where RF stands for the replication factor of the universe. RF is typically 3 or 5. To restore the resilience of the master quorum, a new master process should be brought up.

To start a new master,
1. Select a node in the same Availability Zone as the removed node in the earlier step.
2. Click its corresponding **Actions > Start Master**. The action to start a master is only available if there are additional nodes in the same availability zone that do not already have a master running.

Taking this action performs the following steps:
1. Sets up the master configuration on this node.
2. Starts a new master process on this node (in shell mode).
3. Adds this new master to the existing master quorum.
4. Updates the master addresses gflag on all other nodes to inform them of this new master.

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
