<!--
+++
private = true
block_indexing = true
+++
-->

Read replicas are a read-only extension to the primary cluster. With read replicas, the primary data of the cluster is copied to one or more nodes in a different region. Read replicas do not add to write latencies because writes aren't synchronously replicated to replicas - the data is replicated to the replicas asynchronously. To read data from a read replica, you need to enable follower reads.

For more information on read replicas and follower reads in YugabyteDB, see the following:

- [Read replicas](../../../architecture/docdb-replication/read-replicas/)
- [Follower reads](../../../explore/going-beyond-sql/follower-reads-ysql/)

You can customize the number of read replicas in the read replica cluster. Multiple replicas ensure the availability of the replica in case of a node outage. Replicas do not participate in the primary cluster [Raft](../../../architecture/docdb-replication/replication/#raft-replication) consensus, and do not affect the fault tolerance of the primary cluster or contribute to failover. The number of read replicas can't exceed the number of nodes in the read replica cluster.

You can delete, modify, and scale read replica clusters. Adding or removing nodes incurs a load on the read replica cluster. Perform scaling operations when the cluster isn't experiencing heavy traffic. Scaling during times of heavy traffic can temporarily degrade performance and increase the length of time of the scaling operation.

## Limitations

- Currently, YugabyteDB Anywhere supports only one read replica cluster per universe.
- You can add up to 15 read replicas to the read replica cluster.
