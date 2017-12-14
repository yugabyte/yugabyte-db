---
title: ACID Transactions
weight: 971
---

YugaByte DB's replication and transaction models are inspired by [Google Spanner](https://cloud.google.com/spanner/docs/transactions).

## Replication Model

### Strong Consistency at the Core

As detailed in the [Replication](/architecture/concepts/replication/) concepts section, mutations (such as inserts and updates) in YugaByte DB are replicated using using [Raft](https://raft.github.io/), a distributed consensus algorithm that is proven to be theoretically correct under various failure conditions. Google Spanner uses a similar consensus algorithm called [Paxos](https://cloud.google.com/spanner/docs/whitepapers/life-of-reads-and-writes). Raft was [designed](https://ramcloud.stanford.edu/~ongaro/userstudy/) to be easier to understand and hence easier implement in practice as compared to Paxos. Today, many popular highly reliable distributed systems including [etcd](https://coreos.com/etcd/) and [consul](https://www.consul.io/docs/internals/consensus.html) are based on Raft.

Raft is used to achieve strong consistency between replicas when mutations occur. This guarantees zero data loss if the failures are within the limits tolerated by the system. For example, if Replication Factor is 5, then YugaByte DB can tolerate failure of up to two nodes without any data loss. The system doesn't have to resort to a stale copy of the data (unlike eventually consistent or async replication solutions).

### Async Replication Option

In addition to the core distributed consensus based replication, YugaByte extends Raft to add observer nodes that do not participate in writes but get a timeline consistent copy of the data in an asynchronous manner. Nodes in remote  datacenters can thus be added in "async" mode. This is primarily for cases where latency of doing a distributed consensus based write is not tolerable for some workloads. This "async" mode (or timeline consistent mode) is still strictly better than eventual consistency, because with the latter the application's view of the data can move back and forth in time and is hard to program to.

### Tunable Read Consistency

YugaByte DB's read semantics are "strongly consistent" by default. Reads are routed to the leaders of the Raft group to ensure the latest values are returned. However, an application may override this setting for individual requests. For example, this is a useful mode if an app can tolerate slightly stale reads from a follower, for much lower latencies in geo-distributed use cases.

## Transaction Model

### Single Row and Single Shard ACID Transactions

YugaByte DB currently offers ACID semantics for mutations involving a single-row or rows that fall within the same shard (partition). These mutations incur only one network roundtrip between the distributed consensus peers. 

Even read-modify-write operations within a single row or single shard, such as the following incur only 1 round trip in YugaByte DB.

```sql
   UPDATE table SET x = x + 1 WHERE ... 
   INSERT .. IF NOT EXISTS... 
   UPDATE .. IF EXISTS 
```

This is unlike Apache Cassandra, which uses a concept called lightweight transactions to achieve correctness for these read-modify-write operations and incurs [4-network round trip latency](https://docs.datastax.com/en/cassandra/3.0/cassandra/dml/dmlLtwtTransactions.html).

### Distributed ACID Transactions

Distributed ACID transactions involve multi-row as well as multi-shard operations. As a logical next step, YugaByte DB is working on support for distributed ACID transactions to support operations that need ACID semantics across mutations to rows or tables that fall on different shards.  This will allow us to support features like strongly consistent secondary indexes, multi-table/row ACID operations in both Cassandra (CQL) context as well as in the PostgreSQL context (which is the 3rd API that YugaByte DB intends to support). More details about the design & implementation of distributed transactions will be shared on the [YugaByte Community Forum](https://forum.yugabyte.com/) in the near future.
