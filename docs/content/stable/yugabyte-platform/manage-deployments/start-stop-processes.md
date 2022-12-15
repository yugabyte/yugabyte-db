---
title: Start and stop processes
headerTitle: Start and stop processes
linkTitle: Start and stop processes
description: Use YugabyteDB Anywhere to start and stop processes.
menu:
  stable_yugabyte-platform:
    identifier: start-stop-processes
    parent: manage-deployments
    weight: 10
type: docs
---

## Start a process

You can restart the node's processes by navigating to **Universes**, selecting your universe, then selecting **Nodes**, and then clicking **Actions > Start Processes** corresponding to the node. This returns the node to the Live state.

In some cases, the system might experience an unrecoverable error. To mitigate, you can use the **Release Instance** option for the stopped node. This also removes the backing instance. For details, see [Remove a node](../remove-nodes/).

## Stop a process

If a node needs the intervention, you can click its associated **Actions > Stop Processes**.

Once the YB-TServer and, possibly, YB-Master server are stopped, the node status is updated and the instance is ready for the planned system changes.

Generally, when a YB-Master is stopped on a node, YugabyteDB Anywhere automatically attempts to start a new YB-Master on another node in the same Availability Zone as the node on which YB-Master is stopped. This ensures that the number of YB-Master servers equals the replication factor (RF) and YB-Master servers are never underreplicated.

It is recommended not to stop more than (RF - 1) / 2 processes at any given time. For example, on an RF=3 cluster with three nodes, there can only be one node with stopped processes to allow the majority of the nodes to perform Raft consensus operations.
