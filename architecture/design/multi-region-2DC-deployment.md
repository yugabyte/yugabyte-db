# Two Data Center Deployment Support with YugaByte DB

This document outlines the 2-datacenter deployments that YugaByte DB is being enhanced to support, as well as the architecture that enables these. This feature is built on top of [Change Data Capture (CDC)](https://github.com/YugaByte/yugabyte-db/blob/master/architecture/design/docdb-change-data-capture.md) support in DocDB, and that design may be of interest.

### Watch an example video (less than 2 mins)
<a href="http://www.youtube.com/watch?feature=player_embedded&v=2quaIAKBATk" target="_blank">
  <img src="http://img.youtube.com/vi/2quaIAKBATk/0.jpg" alt="YugaByte DB 2DC deployment" width="240" height="180" border="10" />
</a>

## Features

> **Note:** In this design document, the terms "cluster" and "universe" will be used interchangeably. While not a requirement in the final design, we assume here that each YugaByte DB universe is deployed in a single data-center for simplicity purposes.

This feature will support the following:

* Replication across datacenters is done at the DocDB level. This means that replication will work across **all** the APIs - YSQL, YCQL and YEDIS.

* The design is general enough to support replication of single-key updates as well as multi-key/distributed transactions.

* In the initial version, we assume that schema changes are run on both the universes independently. This will eventually be automated to the extent possible. Note that some combinations of schema changes and update operations are inherently unsafe. Identifying all of these cases and making the database safe in all scenarios (by throwing a user facing error out or through some other such mechanism) will be a follow on task to harden this feature.

* Design will support active-active replication, with both data centers accepting writes and replicating them to the other data center.

* Note that it will be possible to perform replication to multiple target clusters. Similarly, it will be possible to consume replicated data from multiple source clusters.

* Updates will be timeline consistent. That is, target data center will receive updates for a row in the same order in which they occurred on the source.

* Transactions will be applied atomically on the consumer. That is, either all changes in a transaction should be visible or none.

* Target data center will know the data consistency timestamp. Since YB data is distributed across multiple nodes, (and data is replicated from multiple nodes), target data center should be able to tell that all tablets have received data at least until timestamp x, that is, it has received all the writes that happened at source data center(s) until timestamp x.


# Supported Deployment Scenarios

The following 2-DC scenarios will be supported:

### Master-Slave with *asynchronous replication*

The replication could be unidirectional from a **source cluster** (aka the *master* cluster) to one or more **sink clusters** (aka *slave* clusters). The sink clusters, typically located in data centers or regions that are different from the source cluster, are *passive* because they do not not take writes from the higher layer services. Such deployments are typically used for serving low latency reads from the slave clusters as well as for disaster recovery purposes.

The source-sink deployment architecture is shown in the diagram below:

![2DC source-sink deployment](https://github.com/YugaByte/yugabyte-db/raw/master/architecture/design/images/2DC-source-sink-deployment.png)

### Multi-Master with *last writer wins (LWW)* semantics

The replication of data can be bi-directional between two clusters. In this case, both clusters can perform reads and writes. Writes to any cluster is asynchronously replicated to the other cluster with a timestamp for the update. If the same key is updated in both clusters at a similar time window, this will result in the write with the larger timestamp becoming the latest write. Thus, in this case, the clusters are all *active* and this deployment mode is called **multi-master deployments** or **active-active deployments**).

> **NOTE:** The multi-master deployment is built internally using two master-slave unidirectional replication streams as a building block. Special care is taken to ensure that the timestamps are assigned to ensure last writer wins semantics and the data arriving from the replication stream is not re-replicated. Since master-slave is the core building block, we will focus the rest of this design document on that.


The architecture diagram is shown below:

![2DC multi-master deployment](https://github.com/YugaByte/yugabyte-db/raw/master/architecture/design/images/2DC-multi-master-deployment.png)



# Design - Lifecycle of Replication

2DC one-way sync replication in YugaByte DB can be broken down into the following phases:

* Initialize the producer and the consumer
* Set up a distributed CDC
* Pull changes from source cluster
* Periodically checkpoint progress

Each of these are described below.

### Step 1. Initialize the producer and the consumer

In order to setup 2DC async replication, the user runs a command similar to the one shown below:

```
yugabyte-db init-replication                         \
            --sink_universe <sink_ip_addresses>      \
            --source_universe <source_ip_addresses>  \
            --source_role <role>                     \
            --source_password <password>
```

> **NOTE:** The commands shown above are for illustration purposes, these are not the final commands. Please refer to the user documentation of this feature after it has landed for the final commands.

This instructs the sink cluster to perform the following two actions:
* Initialize its **sink replication consumer** which prepares it to consume updates from the source.
* Make an RPC call to the source universe to initialize the **source replication producer**, which prepares it to produce a stream of updates.

This is shown diagrammatically below.

![2DC initialize consumer and producer](https://github.com/YugaByte/yugabyte-db/raw/master/architecture/design/images/2DC-step1-initialize.png)

The **sink replication consumer** and the **source replication producer** are distributed, scale-out services that run on each node of the sink and source clusters respectively. In the case of the *source replication producer*, each node is responsible for all the source tablet leaders it hosts (note that the tablets are restricted to only those that participate in replication). In the case of the *sink replication consumer*, the metadata about which sink node owns which source tablet is explicitly tracked in the system catalog.

Note that the sink clusters can fall behind the source clusters due to various failure scenarios (such as node outages or extended network partitions). Upon the failure conditions healing, it may not always be safe to continue replication. In such cases, the user would have to bootstrap the sink cluster to a point from which replication can safely resume. Some of the cases that arise are shown diagrammatically below:
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

### Step 2. Set up a distributed CDC

The next step is to start the change data capture processes on the source cluster so that it can begin generating a list of changes. Since this feature builds on top of *Change Data Capture (CDC)*, the change stream has the following guarantees:

* **In order delivery per row**: All changes for a single row (or rows in the same tablet) will be received in the order in which they happened.
  > Note that global ordering is not guaranteed across tablets.

* **At least once delivery**: Updates for rows will be pushed at least once. It is possible to receive previously seen updates again.

* **Monotonicity of updates**: Receiving a change for a row implies that all previous changes for that row have been received.

The following substeps happen as a part of this step:

* The sink cluster fetches the list of source tablets.
* The responsibility of replication from a subset of source tablets is assigned to each of the nodes in the sink cluster.
* Each node in the sync cluster pulls the change stream from the source cluster for the tablets that have been assigned to it. These changes are then applied to the sink cluster.

The sync cluster maintains the state of replication in a separate `system.cdc_state` table. The following information is tracked for each tablet in the source cluster that needs to be replicated:
* Details of the source tablet, along with which table it belongs to
* The node in the sink cluster that is currently responsible for replicating the data
* The last operation id that was successfully committed into the sink cluster

### Step 3. Fetch and handle changes from source cluster

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

As shown in the figure above, each update message has a corresponding **document key** (denoted by the `document_key_1` field in the figure above) which the *sink replication consumer* uses to locate the owning tablet leader on the sink cluster. The update message is routed to that tablet leader, which handles the update request. See the section below on transactional guarantees for details on how this update is handled.

> Note that the `GetCDCRecords()` RPC request can be routed to the appropriate node through an external load balancer in the case of a Kubernetes deployment.

### Step 4. Periodically checkpoint progress

The sink cluster nodes will periodically write a *checkpoint* consisting of the last successfully applied operation id along with the source tablet information into its `system.cdc_state` table. This allows the replication to continue from this point onwards.

# Transactional Guarantees

Note that the CDC change stream generates updates (including provisoinal writes) at the granularity of document attributes and not at the granularity of user-issued transactions. However, transactional guarantees are important for 2DC deployments. The *sink replication consumer* ensures the following:

* **Atomicity of transactions**: This implies one can never read a partial result of a transaction on the sink cluster.
* **Not globally ordered**: The transactions (especially those that do not involve overlapping rows) may not be applied in the same order as they occur in the source cluster.
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

On receiving a new single-row write `ReplicateMsg` for the key `document_key`, the tablet leader owning that key processes the `ReplicateMsg` according to the following rules:
* **Ignore on later update**: If there is a committed write at a higher timestamp, then the replicated write update is ignored. This is safe because a more recent update already exists, which should be honored as the latest value according to a *last writer wins* policy based on the record timestamp.
* **Abort conflicting transaction**: If there is a conflicting non-replicated transaction in progress, that transaction is aborted and the replicated message is applied.
* **In all other cases, the update is applied.**

Once the `ReplicateMsg` is processed by the tablet leader, the *sink replication consumer* marks the record as `consumed`

> Note that all replicated records will be applied at the same hybrid timestamp at which they occurred on the producer universe.


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

In the case of single-row transactions, the `TransactionMetadata` field be set. Note that in this case, the `OperationType` can be `WRITE` or `CREATE/UPDATE TXN`.

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

This combination of `WAITING_TO_COMMIT` status and `tablets_received_applying_record` field will be used to ensure that transaction records are applied atomically on the replicated universe.

# What’s supported in the initial version?
* **Active-active (multi-master) replication**:
  * If there is a write conflict (i.e. same row is updated on both universes), then we’ll follow the “last writer wins” semantics and the write with the latest timestamp will eventually persist on both data centers.

* **Replicating newly added tables**:
  * Support setting up replication on new tables that are added after 2DC is setup. In this situation, users will need to run yb-admin command and list the new table to consume from.
  
* **Setting up replication on empty tables**:
  * Setup replication when the table has no data. We cannot handle setting up replication on a table with existing data.


# Future Work

* **Automatically bootstrapping sink clusters**:
  * Currently: it is the responsibility of the end user to ensure that a sink cluster has sufficiently recent updates so that replication can safely resume.
  * Future: bootstrapping the sink cluster can be automated.

* **Replicating DDL changes**:
  * Currently: DDL changes are not automatically replicated. Applying create table and alter table commands to the sync clusters is the responsibiity of the user.
  * Future: Allow safe DDL changes to be propagated automatically.
  
* **Safety of DDL and DML in active-active**:
  * Currently: Certain potentially unsafe combinations of DDL/DML are allowed. For example, in having a *unique key constraint* on a column in an active-active *last writer wins* mode is unsafe since a violation could easily be introduced by inserting different values on the two clusters - each of these operations is legal in itself. The ensuing replication can, however, violate the unique key constraint. This will cause the two clusters to permanently diverge and the replication to fail.
  * Future: Detect such unsafe combinations and warn the user. Such combinations should possibly be disallowed by default.

# Impact on Application Design
Since 2DC replication is done asynchronously and by replicating the WAL (and thereby bypassing the query layer), application design needs to follow these patterns:

* **Avoid UNIQUE indexes / constraints (only for active-active mode)**:
  * Since replication is done at the WAL level, we don’t have a way to check for unique constraints. It’s possible to have two conflicting writes on separate universes which will violate the unique constraint and will cause the main table to contain both rows but the index to contain just 1 row, resulting in an inconsistent state.
  
* **Avoid triggers**: 
  * Since we bypass the query layer for replicated records, DB triggers will not be fired for those and can result in unexpected behavior.
  
* **Avoid serial columns in primary key (only for active-active mode)**:
  * Since both universes will generate the same sequence numbers, this can result in conflicting rows. It’s better to use UUIDs  instead.


[![Analytics](https://yugabyte.appspot.com/UA-104956980-4/architecture/design/multi-region-2DC-deployment.md?pixel&useReferer)](https://github.com/YugaByte/ga-beacon)
