---
date: 2016-03-09T20:08:11+01:00
title: Universe Creation
weight: 30
---

When creating a YugaByte universe, the first step is to bring up sufficient YB-Masters (as many as
the replication factor) with each being told about the others. These YB-Masters are brought up for
the first time in the cluster_create mode. This causes them to initialize themselves with a unique
UUID, learn about each other and perform a leader election. Note that subsequent restarts of the
YB-Master, such as after a server crash/restart, do not require the cluster_create option. At the
end of this step, one of the masters establishes itself as the leader.

The next step is to start as many YB-TServers as there are nodes, with the master addresses being
passed to them on startup. They start heart-beating to the masters, communicating the fact that they
are alive. The heartbeats also communicate the tablets the YB-TServers are currently hosting and
their load, but no tablets would exist in the system yet.

Let us illustrate this with our usual example of creating a 4-node YugaByte universe with a
replication factor of 3. In order to do so, first the three masters are started in the create mode
instructing them to that this is a brand new universe create. This is done explicitly to prevent
accidental errors in creating a universe while it is already running.

![create_universe_masters](/images/create_universe_masters.png)

The next step, the masters learn about each other and elect one leader.

![create_universe_master_election](/images/create_universe_master_election.png)

The YB-TServers are then started, and they all heartbeat to the YB-Master.

![create_universe_tserver_heartbeat](/images/create_universe_tserver_heartbeat.png)
