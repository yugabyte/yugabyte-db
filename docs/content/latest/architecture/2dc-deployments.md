---
title: 2DC deployments [beta]
linkTitle: 2DC deployments [beta]
description: 2-data center (2DC) deployments
beta: /faq/product/#what-is-the-definition-of-the-beta-feature-tag
menu:
  latest:
    parent: architecture
    identifier: 2dc-deployments
    weight: 652
type: page
isTocNested: true
showAsideToc: true
---

This page includes an overview about the Yugabyte DB support for 2-data center (2DC) deployments, supported scenarios, and the life cycle of replication.
For details on deploying a 2DC deployment, including one-way and two-way replication, see [Deploy two data centers with change data capture (CDC)](../2dc-replication).

## Overview

{{< note title="Note" >}}

The terms "cluster" and "universe" will be used interchangeably. For simplicity in the following sections, each Yugabyte DB universe is assumed to be deployed in a single data center.

{{< /note >}}

Yugabyte DB supports two data center (2DC) deployments, which is built on top of [Change Data Capture (CDC)](../cdc). Yugabyte DB support for two data center (2DC) deployments includes the following:

* Replication works across both the YSQL and YCQL APIs because replication is done at the DocDB level.

* Replication of single-key updates and multi-key, distributed transactions are supported.

* Schema changes run independently on each universe. Eventually, this will be automated as much as possible. Because some combinations of schema changes and update operations are inherently unsafe, we plan to harden this feature by identifying the unsafe operations and make the database safe in all scenarios (by throwing user-facing errors or through other mechanisms).

* Active-active replication is supported, with both data centers accepting writes and replicating them to the other data center.

* Replication can be performed to multiple target clusters and the target data center can consume replicated data from multiple source clusters.

* Updates are timeline consistent — the target data center will receive updates for a row in the same order in which they occurred on the source.

* Transactions are applied atomically on the consumer — either all changes in a transaction will be visible or none.

* The target data center will know the data consistency timestamp. Since data in Yugabyte DB is distributed across multiple nodes and replicated from multiple nodes, the target data center is able to determine that all tablets have received data at least until timestamp x, that is, it has received all the writes that happened at source data centers until timestamp x.

### Watch an example video (less than 2 mins)

If you're interested, you can watch the following YouTube video demonstrating a 2DC deployment.

<a href="http://www.youtube.com/watch?feature=player_embedded&v=2quaIAKBATk" target="_blank">
  <img src="http://img.youtube.com/vi/2quaIAKBATk/0.jpg" alt="Yugabyte DB 2DC deployment" width="240" height="180" border="10" />
</a>

## Supported two data center (2DC) deployment scenarios

Yugabyte DB supports the following two scenarios for 2DC deployments.

### Master-slave with asynchronous replication

The replication could be unidirectional from a **source cluster** (aka the *master* cluster) to one or more **sink clusters** (aka *slave* clusters). The sink clusters, typically located in data centers or regions that are different from the source cluster, are *passive* because they do not not take writes from the higher layer services. Such deployments are typically used for serving low latency reads from the slave clusters as well as for disaster recovery purposes.

The source-sink deployment architecture is shown here:

![2DC source-sink deployment](/images/deploy/cdc/2DC-source-sink-deployment.png)

### Multi-master with last-writer-wins (LWW) semantics

The replication of data can be bidirectional between two clusters. In this case, both clusters can perform reads and writes. Writes to any cluster is asynchronously replicated to the other cluster with a timestamp for the update. If the same key is updated in both clusters in a similar time window, this will result in the write with the larger timestamp becoming the latest write. In this case, the clusters are all *active* and the deployment mode is called **multi-master deployments** or **active-active deployments**.

> **NOTE:** The multi-master deployment is built internally using two master-slave unidirectional replication streams as a building block. Special care is taken to ensure that the timestamps are assigned to ensure last writer wins semantics and the data arriving from the replication stream is not re-replicated. Since master-slave is the core building block, we will focus the rest of this design document on that.

The architecture diagram is shown here:

![2DC multi-master deployment](/images/deploy/cdc/2DC-multi-master-deployment.png)

## Life cycle of replication

A 2DC one-way sync replication in Yugabyte DB follows these four life cycle phases:

1. Initialize the producer and the consumer
2. Set up a distributed CDC
3. Pull changes from source cluster
4. Periodically checkpoint progress

Details about these life cycle phases are included below.

### 1. Initialize the producer and the consumer

In order to setup 2DC async replication, the user runs a command similar to the one here:

```bash
yugabyte-db init-replication                         \
            --sink_universe <sink_ip_addresses>      \
            --source_universe <source_ip_addresses>  \
            --source_role <role>                     \
            --source_password <password>
```

This instructs the sink cluster to perform the following two actions:

1. Initialize the **sink replication consumer**, preparing it to consume updates from the source.
2. Make an RPC call to the source universe to initialize the **source replication producer**, preparing it to produce a stream of updates.

This is shown diagrammatically here:

![2DC initialize consumer and producer](/images/deploy/cdc/2DC-step1-initialize.png)

The **sink replication consumer** and the **source replication producer** are distributed, scale-out services that run on each node of the sink and source Yugabyte DB clusters, respectively. In the case of the *source replication producer*, each node is responsible for all of the source tablet leaders that it hosts (note that the tablets are restricted to only those that participate in replication). In the case of the *sink replication consumer*, the metadata about which sink node owns which source tablet is explicitly tracked in the system catalog.

Note that the sink clusters can fall behind the source clusters due to various failure scenarios (for example, node outages or extended network partitions). Upon healing of the failure conditions, continuing replication may not always be safe. In such cases, the user needs to bootstrap the sink cluster to a point from which replication can safely resume. Some of the cases that arise are shown diagrammatically below:

```
                          ╔══════════════════════════════════╗
                          ║      New replication setup?      ║
                          ╚══════════════════════════════════╝
                                    |               |
  ╔══════════════════════╗      YES |               | NO       ╔══════════════════════╗
  ║   Does data already  ║<----------               ---------->║ Can CDC continue     ║
  ║   exist in cluster?  ║                                     ║ from last checkpoint?║
  ╚══════════════════════╝                                     ╚══════════════════════╝
         |          |________________                _______________|        |
      YES|                 NO        |              |      YES               |NO
         V                           V              V                        V
  ╔══════════════════════╗        ╔══════════════════════╗      ╔══════════════════════╗
  ║ First bootstrap data ║        ║   Replication can    ║      ║ Re-bootstrap data    ║
  ║ from source cluster  ║------->║   resume safely      ║<-----║ from source cluster  ║
  ╚══════════════════════╝  DONE  ╚══════════════════════╝ DONE ╚══════════════════════╝
```

### 2. Set up a distributed CDC

The second step is to start the change data capture processes on the source cluster so that it can begin generating a list of changes. Since this feature builds on top of [Change Data Capture (CDC)](../cdc), the change stream has the following guarantees:

* **In order delivery per row**: All changes for a single row (or rows in the same tablet) will be received in the order in which they happened.

> Note that global ordering is not guaranteed across tablets.

* **At least once delivery**: Updates for rows will be pushed at least once. It is possible to receive previously seen updates again.

* **Monotonicity of updates**: Receiving a change for a row implies that all previous changes for that row have been received.

The following substeps happen as a part of this step:

1. The sink cluster fetches the list of source tablets.
2. The responsibility of replication from a subset of source tablets is assigned to each of the nodes in the sink cluster.
3. Each node in the sync cluster pulls the change stream from the source cluster for the tablets that have been assigned to it. These changes are then applied to the sink cluster.

The sync cluster maintains the state of replication in a separate CDC state table (`system.cdc_state`). The following information is tracked for each tablet in the source cluster that needs to be replicated:

* Details of the source tablet, along with which table it belongs to
* The node in the sink cluster that is currently responsible for replicating the data
* The last operation ID that was successfully committed into the sink cluster

### 3. Fetch and handle changes from source cluster

The *sink replication consumer* continuously performs a `GetCDCRecords()` RPC call to fetch updates (which returns a `GetCDCRecordsResponsePB`). The source cluster sends a response that includes a list of update messages that need to be replicated, if any. It may optionally also include a list of IP addresses of new nodes in the source cluster in case tablets have moved. An example of the response message is shown below.

```
GetCDCRecordsResponsePB Format:
╔══════════╦══════════╦══════════════════╦═══════════╦═════════════════════════════════════════════╗
║  Source  ║  Source  ║ CDC type: CHANGE ║  Source   ║ <---------- ReplicateMsg list ----------->  ║
║ universe ║  tablet  ║ (send the final  ║  server   ║ ╔═══════════════════════════════╦═════════╗ ║
║   uuid   ║   uuid   ║  changed values) ║ host:port ║ ║ document_key_1, update msg #1 ║   ...   ║ ║
║          ║          ║                  ║           ║ ╚═══════════════════════════════╩═════════╝ ║
╚══════════╩══════════╩══════════════════╩═══════════╩═════════════════════════════════════════════╝
```

As shown in the figure above, each update message has a corresponding **document key** (denoted by the `document_key_1` field in the figure above), which the *sink replication consumer* uses to locate the owning tablet leader on the sink cluster. The update message is routed to that tablet leader, which handles the update request. For details on how this update is handled, see [transactional guarantees](#transactional-guarantees) below.

{{< note title="Note" >}}

In a Kubernetes deployment, the `GetCDCRecords()` RPC request can be routed to the appropriate node through an external load balancer.

{{< /note >}}

### 4. Periodically checkpoint progress

The sink cluster nodes will periodically write a *checkpoint* consisting of the last successfully applied operation ID along with the source tablet information into its CDC state table (`system.cdc_state`). This allows the replication to continue from this point onwards.

## Transactional guarantees

The CDC change stream generates updates, including provisional writes, at the granularity of document attributes and not at the granularity of user-issued transactions. However, transactional guarantees are important for 2DC deployments. The *sink replication consumer* ensures the following:

* **Atomicity of transactions**: This implies one can never read a partial result of a transaction on the sink cluster.
* **Not globally ordered**: The transactions — especially those that do not involve overlapping rows — may not be applied in the same order as they occur in the source cluster.
* **Last writer wins**: In case of active-active configurations, if there are conflicting writes to the same key, then the update with the larger timestamp is considered the latest update. Thus, the deployment is eventually consistent across the two data centers.

The following sections outline how these transactional guarantees are achieved by looking at how a tablet leader handles a single `ReplicateMsg`, which has changes for a document key that it owns.

### Single-row transactions

```
ReplicateMsg received by the tablet leader of the sink cluster:
╔══════════════╦═══════════╦════════════════════╦════════════════════════╗
║ document_key ║ new value ║   OperationType:   ║  TransactionMetadata:  ║
║              ║           ║       WRITE        ║         (none)         ║
╚══════════════╩═══════════╩════════════════════╩════════════════════════╝
```

In the case of single-row transactions, the `TransactionMetadata` field would not have a `txn uuid` set since there are no provisional writes. Note that in this case, the only valid value for `OperationType` is `write`.

On receiving a new single-row write `ReplicateMsg` for the key `document_key`, the tablet leader that owns the key processes the `ReplicateMsg` according to the following rules:

* **Ignore on later update**: If there is a committed write at a higher timestamp, then the replicated write update is ignored. This is safe because a more recent update already exists, which should be honored as the latest value according to a *last writer wins* policy based on the record timestamp.
* **Abort conflicting transaction**: If there is a conflicting non-replicated transaction in progress, that transaction is aborted and the replicated message is applied.
* **In all other cases, the update is applied.**

Once the `ReplicateMsg` is processed by the tablet leader, the *sink replication consumer* marks the record as `consumed`.

{{< note title="Note" >}}

All replicated records will be applied at the same hybrid timestamp at which they occurred on the producer universe.

{{< /note >}}

### Multi-row transactions

```
ReplicateMsg received by the tablet leader of the sink cluster:
╔══════════════╦═══════════╦════════════════════╦═══════════════════════╗
║              ║           ║   OperationType:   ║  TransactionMetadata: ║
║ document_key ║ new value ║       WRITE        ║    - txn uuid         ║
║              ║           ║         or         ║    - hybrid timestamp ║
║              ║           ║  CREATE/UPDATE TXN ║    - isolation level  ║
╚══════════════╩═══════════╩════════════════════╩═══════════════════════╝
```

In the case of single-row transactions, the `TransactionMetadata` field will be set. Note that in this case, the `OperationType` can be `WRITE` or `CREATE/UPDATE TXN`.

Upon receiving a `ReplicateMsg` that is a part of a multi-row transaction, the *sink replication consumer* handles it as follows:

* `CREATE TXN` messages: Send the message to the transaction status tablet leader (based on the value of `txn uuid` in the `TransactionMetadata` field. The message eventually gets applied to the transaction status table on the sink cluster.
* `WRITE` messages: Sends the record to tablet leader of the key, which ensures there is no conflict before writing a provisional write record.
  * If there is a conflicting provisional write as a part of a non-replicated transaction on the sink cluster, then this transaction is aborted.
  * If there is another non-transactional write recorded at a higher timestamp, then the leader does not write the record into its WAL log but returns success to replication leader (The write is ignored but considered “success”).

* `UPDATE TXN` messages: The following cases are possible:
  * If record is a `Transaction Applying` record for a tablet involved in the transaction, then an RPC is sent to the transaction leader, indicating that the tablet involved has received all records for the transaction. The transaction leader will record this information in its WAL and transactions table (using field tablets_received_applying_record).
  * If record is `Transaction Commit` record, then all tablets involved in the transaction are queried to see if they have received their `Transaction Applying` records.
    * If not, then simply add a `WAITING_TO_COMMIT` message to its WAL and set the transaction status to `WAITING_TO_COMMIT`.
    * If all involved tablets have received `Transaction Applying` message, then set the status to `COMMITTED`, and sends a new `Transaction Applying` wal message to all involved tablets, notifying them to commit the transaction.
    * In applying record above, if the status is `WAITING_TO_COMMIT` and all involved tablets have received their applying message, then it applies a new `COMMITTED` message to WAL, changes transaction status to `COMMITTED` and sends a new applying message for all involved tablets, notifying them to commit the transaction.

This combination of the `WAITING_TO_COMMIT` status and the `tablets_received_applying_record` field will be used to ensure that transaction records are applied atomically on the replicated universe.

## What’s supported

* **Active-active (multi-master) replication**:
  * If there is a write conflict (i.e. same row is updated on both universes), then we’ll follow the “last writer wins” semantics and the write with the latest timestamp will eventually persist on both data centers.

* **Replicating newly added tables**:
  * Support setting up replication on new tables that are added after 2DC is setup. In this situation, users will need to run yb-admin command and list the new table to consume from.
  
* **Setting up replication on empty tables**:
  * Setup replication when the table has no data. We cannot handle setting up replication on a table with existing data.

## Future work

* **Automatically bootstrapping sink clusters**:
  * Currently: it is the responsibility of the end user to ensure that a sink cluster has sufficiently recent updates so that replication can safely resume.
  * Future: bootstrapping the sink cluster can be automated.

* **Replicating DDL changes**:
  * Currently: DDL changes are not automatically replicated. Applying create table and alter table commands to the sync clusters is the responsibility of the user.
  * Future: Allow safe DDL changes to be propagated automatically.
  
* **Safety of DDL and DML in active-active**:
  * Currently: Certain potentially unsafe combinations of DDL/DML are allowed. For example, in having a *unique key constraint* on a column in an active-active *last writer wins* mode is unsafe since a violation could easily be introduced by inserting different values on the two clusters - each of these operations is legal in itself. The ensuing replication can, however, violate the unique key constraint. This will cause the two clusters to permanently diverge and the replication to fail.
  * Future: Detect such unsafe combinations and warn the user. Such combinations should possibly be disallowed by default.

## Impact on application design

Since 2DC replication is done asynchronously and by replicating the WAL (thus bypassing the query layer), application design needs to follow these patterns:

* **Avoid UNIQUE indexes and constraints (only for active-active mode)**:
  * Since replication is done at the WAL level, we don’t have a way to check for unique constraints. It’s possible to have two conflicting writes on separate universes which will violate the unique constraint and will cause the main table to contain both rows but the index to contain just one row, resulting in an inconsistent state.
  
* **Avoid triggers**:
  * Since we bypass the query layer for replicated records, DB triggers will not be fired for those and can result in unexpected behavior.
  
* **Avoid serial columns in primary key (only for active-active mode)**:
  * Since both universes will generate the same sequence numbers, this can result in conflicting rows. It’s better to use UUIDs instead.
