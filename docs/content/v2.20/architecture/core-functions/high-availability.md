---
title: High availability
headerTitle: High availability
linkTitle: High availability
description: Learn how YugabyteDB maintains continuous availability using multiple replicas.
menu:
  v2.20:
    identifier: high-availability
    parent: core-functions
    weight: 1190
type: docs
---

YugabyteDB is a consistent and partition-tolerant database that at the same time achieves very high availability (HA) by having an active replica which is ready to take over as a new leader immediately after a failure of the current leader and serve requests.

If a node fails, it causes the outage of the servers running on it. These would be a YB-TServer and the YB-Master (if one was running on that node).

## YB-TServer failure

A YB-TServer hosts the YQL layer and tablets, some of which are tablet peer leaders that actively serve I/O, whereas other tablets are tablet peer followers that replicate data and are active standbys to their corresponding leaders.

Failures of each of the YQL layer, tablet peer followers, and tablet peer leaders are handled in specific ways.

### YQL failure

From the application's perspective, YQL is stateless. Hence the client that issued the request simply sends the request to YQL on a different node. In the case of smart clients, they search for the ideal YB-TServer location based on the tablet owning the keys, and then send the request directly to that node.

### Tablet peer follower failure

The tablet peer followers are not in the critical path. Their failure does not impact availability of the user requests.

### Tablet peer leader failure

The failure of any tablet peer leader automatically triggers a new Raft-level leader election within seconds, and another tablet peer on a different YB-TServer takes its place as the new leader. The unavailability window is approximately 3 seconds (assuming the default heartbeat interval of 500 ms) in the event of a failure of the tablet peer leader.

## YB-Master failure

The YB-Master is not in the critical path of normal I/O operations, therefore its failure does not affect a functioning universe. Nevertheless, the YB-Master is a part of a Raft group with the peers running on different nodes. One of these peers is the active master and the others are active standbys. If the active master (the YB-Master leader) fails, these peers detect the leader failure and re-elect a new YB-Master leader which becomes the active master within seconds of the failure.
