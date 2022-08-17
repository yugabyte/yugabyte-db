# Business continuity and disaster recovery

This document describes supported options for cross-region disaster recovery when using YugabyteDB.

## Option 1: three data centers with synchronous replication

Deploying a single cluster across [three or more regions](https://docs.yugabyte.com/preview/deploy/multi-dc/3dc-deployment/) provides out-of-the-box disaster recovery in case one of the regions becomes unavailable. The advantages of this approach are:

- No additional configuration needed
- Operationally simple (automatic failover and switchover, automatic DDL propagation)
- Zero RPO

The major tradeoff, however, is that update latencies are increased due to replication across regions. The second option, which is described below, allows you to minimize the latencies by paying the price of non-zero RPO.

## Option 2: two data centers with asynchronous replication

This option is based on the following requirements:

1. Presence of the disaster recovery solution should not negatively increase update latencies.
1. RPO should be minimized, however non-zero RPO is acceptable.
1. RTO should be as small as possible.

The solution is to deploy two independent clusters in different regions with uni-directional asynchronous data replication (implemented by xCluster feature).

The initial configuration required for the solution is the following:

* There are two clusters (A and B) deployed in different regions.
* xCluster is configured bi-directionally.
* Cluster A is the source and accepts updates from applications.
* Cluster B is the target and do not accept updates (read-only).
* Both A->B and B->A replication is active.
* PITR is enabled for both A and B (used for unplanned failover - see below).

With such configuration, cluster B is on stand-by, so applications can switch to it at any time with minimal RTO. Async nature of replication guarantees that update latencies are not affected by the presence of the stand-by cluster.

### Failover scenarios

There are two distinct cases for failover:

* Planned - switching apps from the source cluster to the target cluster with **zero RPO**. Planned failover is typically initiated during a maintenance window.
* Unplanned - switching apps from the source cluster to the target cluster in case of source cluster failure. Non-zero RPO is expected in this scenario.

#### Planned failover flow

Assuming cluster A is the source and cluster B is the target, the following steps are required to perform planned failover process:

1. Stop applications.
1. Wait for pending updates to propagate to B.
1. Switch application connections to B.
1. Resume applications.

As a result, cluster B becomes the source that replicates updates to cluster A. There is no data loss, so the RPO is zero.

#### Unplanned failover flow

Assuming cluster A is the source, cluster B is the target, and cluster A becomes unavailable, the following steps are required to perform failover process:

1. Stop applications.
1. Pause A->B replication.
1. Pause B->A replication (phase 2 only, see below).
1. Get the latest consistent time on B.
1. Restore B to the latest consistent time using PITR.
1. Switch application connections to B.
1. Resume applications.

##### Restoring the failed cluster

When cluster A comes back up, it needs to be restarted as a new target cluster and restored to a consistent state. Support for this will be provided in two phases.

In phase 1, the process will be the following:

1. Drop all databases in A for which asynchronous replication is used.
1. Restart A and bootstrap its data from B.

Phase 2 will lift the requirement of repopulating the databases by addressing the clock skew concerns. The process will be the following:

1. Restore A to the latest consistent time using PITR.
1. Resume both A->B and B->A replication.

At this point, B is the source cluster and A is the target cluster. To switch back to the initial configuration, the planned failover flow can be used.

#### Failover flow APIs

The following APIs are required to support failover flows described above.

##### Pause replication

Pauses replication by preventing the target cluster from pulling data from the source cluster.

Invoked on either **source** or **target** cluster.

##### Resume replication

Resumes replication by allowing the target cluster to pull data from the source cluster.

Invoked on either **source** or **target** cluster.

##### Wait for drain

Waits for all pending updates to be replicated from the source cluster to the target cluster.

Invoked on the **target** cluster.

##### Get latest consistent time

Gets the latest consistent time for the target cluster.

Invoked on the **target** cluster.

### DDL propagation

TBD

### Github issues

The work is tracked here: [#13532](https://github.com/yugabyte/yugabyte-db/issues/13532)

### Open questions

- Current design relies on the user to make sure that applications issue updates only to one cluster at a time, which is error-prone. Accidentally issuing updates to both clusters concurrently can lead to data conflicts and inconsistent state. Can this be fixed by introducing a concept of a read-only cluster that rejects any updates? One of the challenges with this is that we can't easily switch the source cluster into read-only mode during unplanned failover, due to this cluster being unavailable.
- Do we need an API to explicitly specify whether a cluster is currently used as a source or as a target? This knowledge might be needed, so that we can correctly issue reads based on either the current timestamp, or the xCluster latest consistent timestamp.
