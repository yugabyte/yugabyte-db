---
title: Universe creation
headerTitle: Universe creation
linkTitle: Universe creation
description: Learn how YugabyteDB creates a universe with YB-TServer and YB-Master nodes.
menu:
  stable:
    identifier: universe-creation
    parent: core-functions
    weight: 1182
type: docs
---

Creation of a YugabyteDB universe involves a number of steps.

## Start YB-Masters

When creating a YugabyteDB universe, the first step is to bring up a sufficient number of YB-Masters (as many as the replication factor), with each being made aware of the others. These YB-Masters initialize themselves with a universally unique identifier (UUID), learn about each other and perform a leader election. At the end of this step, one of the masters establishes itself as the leader.

## Start YB-TServers

You need to start as many YB-TServers as there are nodes, with the master addresses being passed to them on startup. They start sending heartbeats to the masters, communicating the fact that they are alive. The heartbeats also communicate about the tablets the YB-TServers are currently hosting as well as their load information, however the tablets do not yet exist in the system.

## Examples

Suppose a table is created in a YugabyteDB universe with four nodes. In addition, suppose that the table has a replication factor of 3. First the three masters are started in create mode. This is done explicitly to prevent accidental errors in creating a universe while it is already running. The following diagram depicts the start of the  universe creation process: 

![create_universe_masters](/images/architecture/create_universe_masters.png)

The following diagram depicts the process of the masters learning about each other and electing one leader:

![create_universe_master_election](/images/architecture/create_universe_master_election.png)

Finally, the YB-TServers are started, and they all send heartbeats to the YB-Master, as per the following diagram:

![create_universe_tserver_heartbeat](/images/architecture/create_universe_tserver_heartbeat.png)
