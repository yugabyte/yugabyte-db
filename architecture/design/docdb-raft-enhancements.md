## NOTE: This document is still a work in progress.


# Raft Enhancements in DocDB

Recall that the storage layer of YugaByte DB is a distributed document store called DocDB. The architecture of DocDB is inspired by Google Spanner. This design doc outlines the various Raft enhancements in DocDB.

YugaByte DB uses Raft consensus without any atomic clocks in order to achieve single-key linearizability while maintaining high read performance. **Linearizability** is one of the strongest single-key consistency models, and implies that every operation appears to take place atomically and in some total linear order that is consistent with the real-time ordering of those operations. In other words, the following should be true of operations on a single key:
* Operations can execute concurrently, but the state of the database at any point in time must appear to be the result of some totally ordered, sequential execution of operations.
* If operation A completes before operation B begins, then B should logically take effect after A.


| Feature/Enhancement Name | Purpose       |
| ------------------------ | ------------- |
| Leader leases            | Improves read performance by decreasing latency of read queries. |
| Group commits            | Improves write performance by increasing write throughput. |
| Raft leader balancing    | Improves read and write performance with optimal utilization of nodes in the cluster. |
| Affinitized Raft Leaders | Improves read and write performance in geo-distributed scenarios by allowing placing leaders closer to the the location of the app. |
| Configurable missed heartbeats    | Enables multi-region and hybrid-cloud deployments where network latency between nodes is high. |
| Integrating Hybrid Logical Clocks | Enables cross-shard transactions as a building block for a software-defined atomic clock for a cluster. |
| MVCC Fencing             | Guarantee safety of writes in leader failure scenarios. |
| Non-Voting Observer Nodes         | Enable multi-region deployments that require low-latency writes and low-latency follower reads. |

## Leader Leases

In order to serve reads, the Raft consensus algorithm requires the current Raft-leader to successfully heartbeat to a majority of peers after it receives the query but before responding to it. This is done to ensure that it is still the Raft-leader, especially in the presence of network-partitions. One possible sequence of operations is shown below.

```
                                       ╔════════════╗     ╔════════════╗
                                       ║ 3a) Heart- ║     ║ 4b) Heart- ║
                                       ║     beat   ║     ║     beat   ║
                                       ║    request ║     ║   response ║
                                       ║    from B  ║     ║    from A  ║ 
Node A                                 ╚═══════════╦╝     ╚═══╦════════╝             Increasing Time
---------------------------------------------------^----------|------------------------------------->
                                                   |           \
╔══════════════╗  ╔═══════════╗ ╔════════════╗    /             \      ╔═════════════════════════╗
║ 1) Node wins ║  ║ 2) Read   ║ ║ 3) Heart-  ║   /               |     ║ 5) Got majority heart-  ║
║    leader    ║  ║    query  ║ ║   beat to  ║  /                |     ║    beat, can respond    ║
║    election  ║  ║   arrives ║ ║   majority ║ /                 |     ║    to client read query ║
╚═════╦════════╝  ╚═════╦═════╝ ╚═══════════╦╝/                  |     ╚═════╦═══════════════════╝  
------V-----------------V-------------------V|---------------^---V-----------V---------^------------>
Node B                                        \           ╔══╩════════════╗            |   
                                               \          ║ 4a) Heart-    ║           /
                                                \         ║     beat resp ║          /
                                                |         ║     from self ║         /
                                                |         ╚═══════════════╝        /  Increasing Time
------------------------------------------------V---------------------------------|----------------->
Node C                                 ╔════════╩═══╗                      ╔══════╩═════╗
                                       ║ 3b) Heart- ║                      ║ 6) Heart-  ║
                                       ║     beat   ║                      ║     beat   ║
                                       ║    request ║                      ║   response ║
                                       ║    from B  ║                      ║     from C ║
                                       ╚════════════╝                      ╚════════════╝

```

### What causes the high latency?

In the figure above, the cause of the high latency is the step 4b, which introduces a network hop in the read path by forcing the Raft leader to wait for the heartbeat response from  one of the Raft followers.

> **NOTE**: If the nodes A, B and C are in different regions, the network latency between them is often very large. In this scenario, the read latencies would be very high. 


## Group Commits

Per the Raft algorithm, each new entry being appended into the Raft log is assigned a monotonically increasing operation id. This operation id is a tuple consisting of (term, index). If each client issued update operation is treated as a separate Raft record entry, this would lead to a lot of RPCs between the Raft members. This means that the network would not be optimally utilized since there would be a lot of small packets.

To utilize the network better, DocDB batches multiple outstanding updates into a single record. This batching of updates in order to commit them to the Raft log is referred to as a **group commit**.

## Raft Leader Balancing

YugaByte DB uses the Raft algorithm to implement a consistent and fault-tolerant write-ahead log. Per the Raft consensus algorithm, the various nodes that host the different Raft replicas (called tablet peers) first elect a leader. This results in one of the nodes becoming the leader of the tablet, and the others become followers. There could now arise scenarios where the distribution of Raft leaders and followers per node is uneven.

YugaByte DB tries to balance the Raft leaders and followers evenly across the nodes of the cluster. This is done as a background operation in a throttled manner so as to not impact foreground latencies or throughput.

> **NOTE**: This is the default behavior. There are user defined policies that change the distribution of leaders, see the *affinitized leaders* section for more details.



## Affinitized Raft Leaders

## Configurable missed heartbeats


## Integrating Hybrid Logical Clocks


## MVCC Fencing

## Non-Voting Observer Nodes

[![Analytics](https://yugabyte.appspot.com/UA-104956980-4/architecture/design/docdb-raft-enhancements.md?pixel&useReferer)](https://github.com/YugaByte/ga-beacon)
