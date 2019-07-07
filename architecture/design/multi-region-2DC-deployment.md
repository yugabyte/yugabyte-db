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

* Note that it will be possible to perform replication to multiple target slave clusters. 

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

2DC on-way sync replication in YugaByte DB can be broken down into the following phases:

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
            --source-password <password>
```

> **NOTE:** The commands shown above are for illustration purposes, these are not the final commands. Please refer to the user documentation of this feature after it has landed for the final commands.

This instructs the sink cluster to perform the following two actions:
* Initialize its **sink replication consumer** which prepares it to consume updates from the source.
* Make an RPC call to the source universe to initialize the **source replication producer**, which prepares it to produce a stream of updates.

This is shown diagrammatically below.

![2DC initialize consumer and producer](https://github.com/YugaByte/yugabyte-db/raw/master/architecture/design/images/2DC-step1-initialize.png)

Note that the following cases are possible:
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

The sync cluster maintains the state of replication in a separate `system.replication` table. The following information is tracked for each tablet in the source cluster that needs to be replicated:
* Details of the source tablet, along with which table it belongs to
* The node in the sink cluster that is currently responsible for replicating the data
* The last operation id that was successfully committed into the sink cluster

### Step 3. Pull changes from source cluster

The *sink replication consumer* continuously performs a `GetCDCRecords` RPC call to fetch updates (which are effectively a set of `ReplicateMsg`). The source cluster sends a response that includes a list of update messages that need to be replicated, if any. It may optionally also include a list of IP addresses of new nodes in the source cluster in case tablets have moved. An example of the response message is shown below.

```
ReplicateMsg Format:
╔════════════╦══════════════╦═══════════════════╦═════════════════════════════════════════════╗
║            ║ Message id:  ║ Operation type    ║ <------ List of updates ----------------->  ║
║    Raft    ║ (hybrid ts,  ║ (write, change    ║ ╔═════════════════════════════╦═══════════╗ ║
║    op id   ║  counter id) ║  config, update   ║ ║ document key, update msg #1 ║    ...    ║ ║
║            ║              ║  update txn, etc) ║ ╚═════════════════════════════╩═══════════╝ ║
╚════════════╩══════════════╩═══════════════════╩═════════════════════════════════════════════╝
```
As shown in the figure above, each update has a document key which the *sink replication consumer* server uses to locate the appropriate sink tablet leader that owns the document key. The update message is routed to that tablet leader node to handle the update request. See the section below on transactional guarantees for details on how this update is handled.

> Note that the `GetCDCRecords()` RPC request can be routed to the appropriate node through an external load balancer in the case of a Kubernetes deployment.

### Step 4. Periodically checkpoint progress

The sink cluster nodes will periodically write a *checkpoint* consisting of the last successfully applied operation id along with the source tablet information into its `system.replication` table. This allows the replication to continue from this point onwards.

# Transactional Guarantees

Note that the CDC change stream generates updates (including provisoinal writes) at the granularity of document attributes and not at the granularity of user-issued transactions. However, transactional guarantees are important for 2DC deployments. The *sink replication consumer* ensures the following:

* **Atomicity of transactions**: This implies one can never read a partial result of a transaction on the sink cluster.
* **Not globally ordered**: The transactions (especially those that do not involve overlapping rows) may not be applied in the same order as they occur in the source cluster.
* **Last writer wins**: In case of active-active configurations, if there are conflicting writes to the same key, then the update with the larger timestamp is considered the latest update. Thus, the deployment is eventually consistent across the two data centers.

The following sections outline how these transactional guarantees are achieved.


### Single-row Transactions


 
On receiving the new write request, if the record is not part of any transaction, the tablet leader applies the record to its WAL file. If it finds a conflicting non-replicated transaction that’s in progress, the transaction is aborted. If it finds a committed write at a higher timestamp, then the replicated write is not applied (In this case, we’ll use a last writer wins policy based on record timestamp).
Tablet leader sends RPC to followers to write the record to their WAL files, waits for an acknowledgement from majority and then acks the write request to replication leader.
On receiving acknowledgement from tablet leader, replication leader marks the record as “consumed” by updating consumed_op_id. This consumed_op_id is checkpointed to master using CheckpointCDC RPC.
Note that all replicated records will be applied at the same timestamp at which they occurred on the producer universe (similar to specifying USING timestamp in CQL).


### Multi-row Transactions
