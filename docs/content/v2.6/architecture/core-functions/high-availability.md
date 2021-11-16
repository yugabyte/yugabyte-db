---
title: High availability
headerTitle: High availability
linkTitle: High availability
description: Learn how YugabyteDB maintains continuous availability using multiple replicas.
menu:
  v2.6:
    identifier: high-availability
    parent: core-functions
    weight: 1190
isTocNested: true
showAsideToc: true
---

As discussed before, YugabyteDB is a CP database (consistent and partition tolerant), but achieves very high availability (HA). It achieves this HA by having an active replica that is ready to take over as a new leader in a matter of seconds after the failure of the current leader and serve requests.

If a node fails, it causes the outage of the servers running on it. These would be a YB-TServer and the YB-Master (if one was running on that node). Let us look at what happens in each of these cases.

## YB-TServer failure

A YB-TServer hosts the YQL layer and a bunch of tablets. Some of these tablets are tablet-peer leaders that actively serve IOs, and other tablets are tablet-peer followers that replicate data and are active standbys to their corresponding leaders.

Let us look at how the failures of each of the YQL layer, tablet-peer followers and tablet-peer leaders are handled.

### YQL failure

Recall that from the application’s perspective, YQL is stateless. Hence the client that issued the request just sends the request to YQL on a different node. In the case of smart-clients, they lookup the ideal YB-TServer location based on the tablet owning the keys, and send the request directly to that node.

### Tablet-peer follower failure

The tablet-peer followers are not in the critical path. Their failure does not impact availability of the user requests.

### Tablet-peer leader failure

The failure of any tablet-peer leader automatically triggers a new RAFT level leader election within seconds, and another tablet-peer on a different YB-TServer takes its place as the new leader. The unavailability window is in the order of a couple of seconds (assuming the default heartbeat interval of 500 ms) in the event of a failure of the tablet-peer leader.

## YB-Master failure

The YB-Master is not in the critical path of normal IO operations, so its failure will not affect a functioning universe. Nevertheless, the YB-Master is a part of a Raft group with the peers running on different nodes. One of these peers is the active master and the others are active stand-bys. If the active master (the YB-Master leader) fails, these peers detect the leader failure and re-elect a new YB-Master leader which now becomes the active master within seconds of the failure.
