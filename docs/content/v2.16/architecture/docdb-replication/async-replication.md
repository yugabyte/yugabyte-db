---
title: xCluster
headerTitle: xCluster replication
linkTitle: xCluster
description: xCluster replication between multiple YugabyteDB clusters.
headContent: Asynchronous replication between independent YugabyteDB clusters
menu:
  v2.16:
    identifier: architecture-docdb-async-replication
    parent: architecture-docdb-replication
    weight: 1150
type: docs
---

YugabyteDB provides [synchronous replication](../replication/) of data in clusters dispersed across multiple (three or more) data centers by using the Raft consensus algorithm to achieve enhanced high availability and performance. However, many use cases do not require synchronous replication or justify the additional complexity and operation costs associated with managing three or more data centers. For these needs, YugabyteDB supports two-data-center (2DC) deployments that use cross-cluster (xCluster) replication built on top of [change data capture (CDC)](../change-data-capture) in DocDB.

For details about configuring an xCluster deployment, see [xCluster deployment](../../../deploy/multi-dc/async-replication).

xCluster replication of data works across YSQL and YCQL APIs because the replication is done at the DocDB level.

## Supported deployment scenarios

### Active-Passive

The replication could be unidirectional from a source cluster (also known the producer cluster) to one sink cluster (also known consumer cluster). The sink clusters are typically located in data centers or regions that are different from the source cluster. They are passive because they do not take writes from the higher layer services. Usually, such deployments are used for serving low-latency reads from the sink clusters as well as for disaster recovery purposes.

The following diagram shows the source-sink deployment architecture:

<img src="/images/architecture/replication/2DC-source-sink-deployment.png" style="max-width:750px;"/>

### Active-Active

The replication of data can be bi-directional between two clusters. In this case, both clusters can perform reads and writes. Writes to any cluster are asynchronously replicated to the other cluster with a timestamp for the update. If the same key is updated in both clusters at a similar time window, this results in the write with the larger timestamp becoming the latest write. In this case, the clusters are all active, and this deployment mode is called a multi-master or active-active deployment.

The multi-master deployment is built internally using two source-sink unidirectional replication streams as a building block. Special care is taken to ensure that the timestamps are assigned to guarantee last writer wins semantics and the data arriving from the replication stream is not re-replicated.

The following is the architecture diagram:

<img src="/images/architecture/replication/2DC-multi-master-deployment.png" style="max-width:750px;"/>

## Not currently supported deployment scenarios

### Broadcast

This topology involves one source cluster sending data to many sink clusters. This is currently not officially supported [#11535](https://github.com/yugabyte/yugabyte-db/issues/11535).

### Consolidation

This topology involves many source clusters sending data to one central sink cluster. This is currently not officially supported [#11535](https://github.com/yugabyte/yugabyte-db/issues/11535).

### More complex topologies

Outside of our traditional 1:1 topology and the above 1:N and N:1, there are many other sensible configurations one might want to set up this replication feature with. However, none of these are currently officially supported. For example:

- Daisy chaining - connecting a series of clusters, as both source and target, for example: `A<>B<>C`
- Ring - connecting a series of clusters, in a loop, for example: `A<>B<>C<>A`

Note that some of these might become naturally unblocked as soon as both Broadcast and Consolidation use cases are unblocked, thus allowing a cluster to simultaneously be both a source and a sink to several other clusters. These are tracked in [#11535](https://github.com/yugabyte/yugabyte-db/issues/11535).

## Features and limitations

### Features

- The sink cluster has at-least-once semantics. This means every update on the source will eventually be replicated to the sink.
- Updates will be timeline consistent. That is, target data center will receive updates for a row in the same order in which they occurred on the source.
- Multi-shard transactions are also supported, but with relaxed atomicity and global ordering semantics, as per [Limitations](#limitations).
- For Active-Active deployments, there could be updates to the same rows, on both clusters. Underneath, a last-writer-wins conflict resolution semantic could be used. The deciding factor here is the underlying HybridTime of the updates, from each cluster.

### Impact on application design

Because 2DC replication is done asynchronously and by replicating the WAL (and thereby bypassing the query layer), application design needs to follow these patterns:

- **Avoid UNIQUE indexes / constraints (only for active-active mode):** Because replication is done at the WAL level, we don't have a way to check for unique constraints. It's possible to have two conflicting writes on separate universes which will violate the unique constraint and will cause the main table to contain both rows but the index to contain just 1 row, resulting in an inconsistent state.

- **Avoid triggers:** Because we bypass the query layer for replicated records, DB triggers will not be fired for those and can result in unexpected behavior.

- **Avoid serial columns in primary key (only for active-active mode):** Because both universes will generate the same sequence numbers, this can result in conflicting rows. It is better to use UUIDs instead.

### Limitations

#### Transactional semantics

- Transactions from the source are not applied atomically on the target. That is, some changes in a transaction may be visible before others.
- Transactions from the source might not respect global ordering on the target. While transactions affecting the same shards, are guaranteed to be timeline consistent even on the target, transactions affecting different shards might end up being visible on the target in a different order than they were committed on the source.

This is tracked in [#10976](https://github.com/yugabyte/yugabyte-db/issues/10976).

#### Bootstrapping sink clusters

- Currently, it is the responsibility of the end user to ensure that a sink cluster has sufficiently recent updates so that replication can safely resume. In the future, bootstrapping the sink cluster can be automated [#11538](https://github.com/yugabyte/yugabyte-db/issues/11538).
- Bootstrap currently relies on the underlying backup and restore (BAR) mechanics of YugabyteDB. This means it also inherits all of the limitations of BAR. For YSQL, currently the scope of BAR is at a database level, while the scope of replication is at table level. This implies that when bootstrapping a sink cluster, you will automatically bring in any tables from source database, in the sink database, even ones you might not plan to actually configure replication on. This is tracked in [#11536](https://github.com/yugabyte/yugabyte-db/issues/11536).

#### DDL changes

- Currently, DDL changes are not automatically replicated. Applying commands such as CREATE TABLE, ALTER TABLE, CREATE INDEX to the sink clusters is the responsibility of the user.
- `DROP TABLE` is not supported. The user must first disable replication for this table.
- `TRUNCATE TABLE` is not supported. This is an underlying limitation, due to the level at which the two features operate. That is, replication is implemented on top of raft WAL files, while truncate is implemented on top of RocksDB SST files.
- In the future, it will be possible to propagate DDL changes safely to other clusters [#11537](https://github.com/yugabyte/yugabyte-db/issues/11537).

#### Safety of DDL and DML in active-active

- Currently, certain potentially unsafe combinations of DDL/DML are allowed. For example, in having a unique key constraint on a column in an active-active last writer wins mode is unsafe because a violation could be introduced by inserting different values on the two clusters, since each of these operations is legal in itself. The ensuing replication can, however, violate the unique key constraint. This will cause the two clusters to permanently diverge and the replication to fail.
- In the future, allowing detection of such unsafe combinations and warn the user. Such combinations should possibly be disallowed by default [#11539](https://github.com/yugabyte/yugabyte-db/issues/11539).

#### Kubernetes

- Technically replication can be set up with Kubernetes deployed universes. However, the source and sink must be able to communicate by directly referencing the pods in the other universe. In practice, this either means that the two universes must be part of the same Kubernetes cluster, or that two Kubernetes clusters must have DNS and routing properly setup amongst themselves.
- Being able to have two YugabyteDB clusters, each in their own standalone Kubernetes cluster, talking to each other via a LoadBalancer, is not currently supported, as per [#2422](https://github.com/yugabyte/yugabyte-db/issues/2422).

### Cross-feature interactions

#### Supported

- TLS is supported for both client and internal RPC traffic. Universes can also be configured with different certificates.
- RPC compression is supported. Note that both clusters must be on a version that supports compression, before a compression algorithm is enabled.
- Encryption at rest is supported. Note that the clusters can technically use different KMS configurations. However, for bootstrapping a sink cluster, the reliance is on the backup and restore flow. As such, a limitation from that is inherited, which requires that the universe being restored has at least access to the same KMS as the one in which the backup was taken. This means both the source and the sink must have access to the same KMS configurations.
- YSQL colocation is supported.
- YSQL geo-partitioning is supported. Note that you must configure replication on all new partitions manually, as DDL changes are not replicated automatically.
- Source and target universes can have different number of tablets.
- Tablet splitting is supported on both source and target universes.

#### Not currently supported

- Tablegroups are not supported, as per [#11157](https://github.com/yugabyte/yugabyte-db/issues/11157).
- [Savepoints](../../../explore/ysql-language-features/advanced-features/savepoints/) are not supported, as per [#14308](https://github.com/yugabyte/yugabyte-db/issues/14308).

## Transactional guarantees

<!--

### Atomicity of transactions

This implies one can never read a partial result of a transaction on the sink cluster.

-->

### Not globally ordered

Transactions on non-overlapping rows may be applied in a different order on the sink cluster, than they were on the source cluster.

### Last writer wins

In case of active-active configurations, if there are conflicting writes to the same key, then the update with the larger timestamp is considered the latest update. Thus, the deployment is eventually consistent across the two data centers.
