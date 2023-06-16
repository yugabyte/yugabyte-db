---
title: xCluster
headerTitle: xCluster replication
linkTitle: xCluster
description: xCluster replication between multiple YugabyteDB universes.
headContent: Asynchronous replication between independent YugabyteDB universes
aliases:
  - /preview/architecture/docdb/2dc-deployments/
menu:
  preview:
    identifier: architecture-docdb-async-replication
    parent: architecture-docdb-replication
    weight: 1150
type: docs
---

## Synchronous versus asynchronous replication

YugabyteDB's [synchronous replication](../replication/) can be used to
tolerate losing entire data centers or regions.  It replicates data
within a single universe spread across multiple (three or more) data
centers so that the loss of one data center does not impact availability
or strong consistency courtesy of the Raft consensus algorithm.

However, synchronous replication has two important drawbacks when used
this way:

- __High write latency__: each write must achieve consensus across at
  least two data centers, which means at least one round trip between
  data centers.  This can add tens or even hundreds of milliseconds of
  extra latency, which is orders of magnitude larger than the latency in
  non-geo-distributed universes.

- __Need for at least three data centers__: because consensus requires
  an odd number of fault domains so ties can be broken, at least three
  data centers must be used, which adds operational cost.

As an alternative, YugabyteDB provides asynchronous replication that
replicates data between two or more separate universes in the
background.  It does not suffer from the drawbacks of synchronous
replication: because it is done in the background, it does not impact
write latency, and because it does not use consensus it does not require
a third data center.

Asynchronous replication has its own drawbacks, however, including:

- __Data loss on failure__: when a universe fails, the data in it that
  has not yet been replicated will be lost.  The amount depends on the
  replication lag, which is usually subsecond.

- __Limitations on transactionality__: Because transactions in the
  universes cannot coordinate with each other, either the kinds of
  transactions must be restricted or some consistency and isolation must
  be lost.


## xCluster replication

xCluster replication is YugabyteDB's implementation of asynchronous
replication.  It allows you to set up one or more unidirectional
replication _flows_ between universes.  Note that xCluster can only be
used to replicate between primary clusters in two different universes;
it cannot be used to replicate between clusters in the same universe.
(See [universe versus
cluster](../../concepts/universe#universe-vs-cluster) for more on the
distinction between universes and clusters.)

For each flow, data is replicated from a _source_ (also called a
producer) universe to a _sink_ (also called a consumer) universe.
Replication is done at the DocDB level, with newly committed writes in
the source universe asynchronously replicated to the sink universe.

Multiple flows can be used; for example, two unidirectional flows
between two universes, one in each direction, produce bidirectional
replication where anything written in one universe will be replicated to
the other &mdash; data is only asynchronously replicated once to avoid
infinite loops.  See [supported deployment
scenarios](#supported-deployment-scenarios) for which flow combinations
are currently supported.

Although for simplicity, we will describe flows between entire
universes, flows are actually composed of streams between pairs of
tables, one in each universe, allowing replication of only certain
namespaces or tables.

xCluster is more flexible than a hypothetical scheme wherein read
replicas are promoted to full replicas when primary replicas are lost
because it does not require the two universes to be tightly coupled.
With xCluster, for example, the two universes can be running different
versions of YugabyteDB and the same table can be split into tablets in
different ways in the two universes.  xCluster also allows for
bidirectional replication, which is not possible using read replicas
because read replicas cannot take writes.


## Asynchronous replication modes

Because there is a useful trade-off between how much consistency is lost
and what transactions are allowed, YugabyteDB provides two different
modes of asynchronous replication:

- __non-transactional replication__: all transactions are allowed but some
  consistency is lost
- __transactional replication__: consistency is preserved but sink
  transactions must be read-only

### Non-transactional replication

Here, after each transaction commits in the source universe, its writes
are independently replicated to the sink universe where they are applied
with the same timestamp they had on the source universe.  No locks are
taken or honored on the sink side.

Note that the writes are being written in the past as far as the sink
universe is concerned.  This violates the preconditions for YugabyteDB
serving consistent reads (see the discussion on [safe
timestamps](../../transactions/single-row-transactions/#safe-timestamp-assignment-for-a-read-request)).
Accordingly, reads on the sink universe are no longer strongly
consistent but rather eventually consistent.

If both sink and source universes write to the same key then the last
writer wins.  The deciding factor is the underlying hybrid time of the
updates from each universe.

Because of replication lag, a read done immediately in the sink universe
after a write done on the source universe may not see that write.
Another way of putting this is that reads in the sink universe do not
wait for up-to-date data from the source universe to become visible.

#### Inconsistencies affecting transactions

Because the writes are being independently replicated, a transaction
from the source universe becomes visible over time.  This means
transactions on the sink universe can see non-repeated reads and phantom
reads no matter what their declared isolation level is.  Effectively
then all transactions on the sink universe are at SQL-92 isolation level
READ COMMITTED, which only guarantees that transactions never read
uncommitted data.  Unlike the normal YugabyteDB READ COMMITTED level, this
does not guarantee a statement will see a consistent snapshot or all
the data that has been committed before the statement is issued.

If the source universe dies, then the sink universe may be left in an
inconsistent state where some source universe transactions have only
some of their writes applied in the sink universe (these are called
_torn transactions_).  This inconsistency will not automatically heal
over time and may need to be manually resolved.

Note that these inconsistencies are limited to the tables/rows being
written to and replicated from the source universe: any sink transaction
that does not interact with such rows is unaffected.

### Transactional replication

This mode is an extension of the previous one.  In order to restore
consistency, we additionally disallow writes on the sink universe and
cause reads to read as of a time far enough in the past that all the
relevant data from the source universe has already been replicated.

In particular, we pick the time to read as of, _T_, so that all the
writes from all the transactions that will commit at or before time _T_
have been replicated to the sink universe.  Put another way, we refuse
to read as of a time that there could be incoming source writes at or
before.  This restores consistent reads and ensures source universe
transaction results become visible atomically.

In order to know when to read as of, we maintain an analog of safe time
called _xCluster safe time_, which is the latest time it is currently
safe to read as of with xCluster transactional replication in order to
guarantee consistency and atomicity.  xCluster safe time advances as
replication proceeds but lags behind real-time by the current
replication lag.  This means, for example, if we write at 2 PM in the
source universe and read at 2:01 PM in the sink universe and replication
lag is say five minutes then the read will read as of 1:56 PM and will
not see the write.  We won't be able to see the write until 2:06 PM in
the sink.

If the source universe dies, then we can discard all the incomplete
information in the sink universe by rewinding it to the latest xCluster
safe time (1:56 PM in the example) using YugabyteDB's [Point-in-Time
Recovery (PITR)](../../../manage/backup-restore/point-in-time-recovery/)
feature.  The result will be the fully consistent database that results
from applying a prefix of the source universe's transactions, namely
exactly those that committed at or before the xCluster safe time.
Unlike with non-transactional replication, there is thus no need to
handle torn transactions.

It is unclear how to best support writes in the sink universe using this
strategy of maintaining consistency by reading only at a safe time in
the past: A sink update transaction would be reading from the past but
writing in the present; it would thus have to wait for at least the
replication lag to make sure no interfering writes from the source
universe occurred during that interval.  Such transactions would thus be
slow and prone to aborting.

Accordingly, sink writes are not currently permitted when using xCluster
transactional replication.  This means that the transactional
replication mode cannot support bidirectional replication.

Sink read-only transactions are still permitted; they run at
serializable isolation level on a single consistent snapshot taken in
the past.


## High-level implementation details

At a high level, xCluster replication is implemented by having _pollers_
in the sink universe that poll the source universe tablet servers for
recent changes.  Each poller works independently and polls one or more
source tablet servers, distributing the received changes among a set
of sink tablet servers.

The polled tablets examine only their Raft logs to determine what
changes have occurred recently rather than looking at their RocksDB
instances.  The incoming poll request specifies the Raft log entry ID to
start gathering changes from and the response includes a batch of
changes and the Raft log entry ID it left off on.

Pollers occasionally checkpoint the Raft ID of the last batch of changes
they have processed; this ensures each change is processed at least
once.  Much of the code for responding to polls is shared with the
[Change data capture (CDC)](../change-data-capture) feature.

### The mapping between source and sink tablets

In simple cases, we can associate a poller with each sink tablet that
polls the corresponding source tablet.

However, in the general case the number of tablets for a table in the
source universe and in the sink universe may be different.  Even if the
number of tablets is the same, they may have different sharding
boundaries due to tablet splits occurring at different places in the
past.

This means that each sink tablet may need the changes from multiple
source tablets and multiple sink tablets may need changes from the same
source tablets.  To avoid multiple redundant cross-universe reads to the
same source tablet, the sink master leader assigns only one poller for
each source tablet; in cases where a source tablet's changes are needed
by multiple sink tablets, the poller assigned to that source tablet
distributes the changes to the relevant sink tablets.

The following illustrative diagram shows what this looks like:

_diagram showing mapping of tablets and placement of pollers_

Tablet splitting involves a Raft entry, which is replicated to the sink
side so that the mapping of pollers to source tablets can be updated as
needed when a source tablet splits.

### Single-shard transactions

These are straightforward: when one of these transaction commits, a
single Raft log entry is produced containing all of that transaction's
writes and its commit time.  This entry in turn is used to generate part
of a changes batch when the poller requests changes.

Upon receiving the changes, the poller examines each write to see what
key it writes to in order to determine which tablet covers that part of
the table then forwards the relevant writes to each of the associated
tablets.  The commit times of the writes are preserved and the writes
are marked as _external_, which prevents them from being further
replicated by xCluster.

### Distributed transactions

These are more complicated because they involve multiple Raft records.
Simplifying somewhat, each time one of these transactions makes a
provisional write, a Raft entry is made on the appropriate tablet and
after the transaction commits, a Raft entry is made on all the involved
tablets to _apply the transaction_.  Applying a transaction here means
converting its writes from provisional records to regular writes.

Provisional writes are handled similarly to the normal writes in the
single-shard transaction case but are written as provisional records
instead of normal writes.  A special inert format is used that differs
from the usual provisional records format.  This both saves space as the
original locking information, which is not needed on the sink side, is
omitted and prevents the provisional records from interacting with the
sink read or locking pathways.  This ensures the transaction will not
affect transactions on the sink side yet.

The apply Raft entries also generate changes received by the pollers.
When a poller receives an apply entry, it sends instructions to all the
sink tablets it handles to apply the given transaction.  Transaction
application on the sink tablets is similar to that on the source
universe but differs among other things due to the different provisional
record format.  It converts the provisional writes into regular writes,
again at the same commit time as on the source universe and with them
being marked as external.  At this point the writes of the transaction
to this tablet become visible to reads.

Because pollers operate independently and the writes/applies to multiple
tablets are not done as a set atomically, writes from a single
transaction &mdash; even a single-shard one &mdash; to multiple tablets
can become visible at different times.

When a transaction commits, it is applied to the relevant tablets
lazily.  This means that even though transaction _X_ commits before
transaction _Y_, _X_'s application Raft entry may occur after _Y_'s
application Raft entry on some tablets.  If this happens, the writes
from _X_ can become visible in the sink universe after _Y_'s.  This is
why non-transactional mode reads are only eventually consistent and not
timeline consistent.

### Transactional mode

xCluster safe time is computed for each database by the sink master
leader as the minimum _xCluster application time_ any tablet in that
database has reached.  Pollers determine this time using information from
the source tablet servers of the form "once you have fully applied all
the changes before this one, your xCluster application time will be _T_".

A source tablet server sends such information when it determines that no
active transaction involving that tablet can commit before _T_ and that
all transactions involving that tablet that committed before _T_ have
application Raft entries that have been previously sent as changes.  A
periodic background task checks for committed transactions that are
missing apply Raft entries and generates such entries for them; this
helps xCluster safe time advance faster.

Note: a previous implementation (pre-2.18) of this mode replicated Raft
entries for the transaction coordinators instead of replicating the
application Raft entries.  This was found to have much greater
complexity than the current implementation.


## Schema differences

_To be written..._


## Replication bootstrapping

_To be written..._



## After this point not yet edited

YugabyteDB provides [synchronous replication](../replication/) of data in universes dispersed across multiple (three or more) data centers by using the Raft consensus algorithm to achieve enhanced high availability and performance. However, many use cases do not require synchronous replication or justify the additional complexity and operation costs associated with managing three or more data centers. For these needs, YugabyteDB supports two-data-center (2DC) deployments that use cross-cluster (xCluster) replication built on top of [change data capture (CDC)](../change-data-capture) in DocDB.

For details about configuring an xCluster deployment, see [xCluster deployment](../../../deploy/multi-dc/async-replication).

xCluster replication of data works across YSQL and YCQL APIs because the replication is done at the DocDB level.

## Supported deployment scenarios

A number of deployment scenarios is supported.

### Active-passive

The replication could be unidirectional from a source universe (also known as producer universe) to one target universe (also known as consumer universe or sink universe). The target universes are typically located in data centers or regions that are different from the source universe. They are passive because they do not take writes from the higher layer services. Usually, such deployments are used for serving low-latency reads from the target universes, as well as for disaster recovery purposes.

The following diagram shows the source-target deployment architecture:

<img src="/images/architecture/replication/2DC-source-sink-deployment.png" style="max-width:750px;"/>

### Active-active

The replication of data can be bidirectional between two universes, in which case both universes can perform reads and writes. Writes to any universe are asynchronously replicated to the other universe with a timestamp for the update. If the same key is updated in both universes at a similar time window, this results in the write with the larger timestamp becoming the latest write. In this case, the universes are all active, and this deployment mode is called a multi-master or active-active deployment.

The multi-master deployment is built internally using two source-target unidirectional replication streams as a building block. Special care is taken to ensure that the timestamps are assigned to guarantee last writer wins semantics and the data arriving from the replication stream is not rereplicated.

The following is the architecture diagram:

<img src="/images/architecture/replication/2DC-multi-master-deployment.png" style="max-width:750px;"/>

## Not supported deployment scenarios

A number of deployment scenarios are not yet supported in YugabyteDB.

### Broadcast

This topology involves one source universe sending data to many target universes. See [#11535](https://github.com/yugabyte/yugabyte-db/issues/11535) for details.

### Consolidation

This topology involves many source universes sending data to one central target universe. See [#11535](https://github.com/yugabyte/yugabyte-db/issues/11535) for details.

### More complex topologies

Outside of the traditional 1:1 topology and the previously described 1:N and N:1 topologies, there are many other desired configurations that are not currently supported, such as the following:

- Daisy chaining, which involves connecting a series of universes as both source and target, for example: `A<>B<>C`
- Ring, which involves connecting a series of universes in a loop, for example: `A<>B<>C<>A`

Some of these topologies might become naturally available as soon as [Broadcast](#broadcast) and [Consolidation](#consolidation) use cases are resolved, thus allowing a universe to simultaneously be both a source and a target to several other universes. For details, see [#11535](https://github.com/yugabyte/yugabyte-db/issues/11535).

## Features and limitations

A number of features and limitations are worth noting.

Because replication is done at the DocDB level
bypassing the query layer, database triggers are not fired in the sink
universe for these changes.

transactional replication currently only supports SQL

### Features

- The target universe has at-least-once semantics. This means every update on the source is eventually replicated to the target.
- Updates are timeline-consistent. That is, the target data center receives updates for a row in the same order in which they occurred on the source.
- Multi-shard transactions are supported, but with relaxed atomicity and global ordering semantics, as per [Limitations](#limitations).
- For active-active deployments, there could be updates to the same rows, on both universes. Underneath, a last-writer-wins conflict resolution semantic could be used. The deciding factor is the underlying hybrid time of the updates, from each universe.

### Impact on application design

Because 2DC replication is done asynchronously and by replicating the WAL (and thereby bypassing the query layer), application design needs to follow these patterns:

- Avoid `UNIQUE` indexes and constraints (only for active-active mode): Because replication is done at the WAL-level, there is no way to check for unique constraints. It is possible to have two conflicting writes on separate universes which would violate the unique constraint and cause the main table to contain both rows, yet the index to contain only one row, resulting in an inconsistent state.

- Avoid triggers: Because the query layer is bypassed for replicated records, the database triggers are not fired for those records and can result in unexpected behavior.

- Avoid serial columns in primary key (only for active-active mode): Because both universes generate the same sequence numbers, this can result in conflicting rows. It is recommended to use UUIDs instead.

### Limitations

There is a number of limitations in the current xCluster implementation.

#### Transactional semantics

- Transactions from the source are not applied atomically on the target. That is, some changes in a transaction may be visible before others.
- Transactions from the source might not respect global ordering on the target. While transactions affecting the same shards are guaranteed to be timeline consistent even on the target, transactions affecting different shards might end up being visible on the target in a different order than they were committed on the source.

This is tracked in [#10976](https://github.com/yugabyte/yugabyte-db/issues/10976).

#### Bootstrapping target universes

- Currently, it is your responsibility to ensure that a target universe has sufficiently recent updates, so that replication can safely resume. In the future, bootstrapping the target universe will be automated, which is tracked in [#11538](https://github.com/yugabyte/yugabyte-db/issues/11538).
- Bootstrap currently relies on the underlying backup and restore (BAR) mechanism of YugabyteDB. This means it also inherits all of the limitations of BAR. For YSQL, currently the scope of BAR is at a database level, while the scope of replication is at table level. This implies that when bootstrapping a target universe, you automatically bring any tables from source database to the target database, even the ones on which you might not plan to actually configure replication. This is tracked in [#11536](https://github.com/yugabyte/yugabyte-db/issues/11536).

#### DDL changes

- Currently, DDL changes are not automatically replicated. Applying commands such as `CREATE TABLE`, `ALTER TABLE`, and `CREATE INDEX` to the target universes is your responsibility.
- `DROP TABLE` is not supported. You must first disable replication for this table.
- `TRUNCATE TABLE` is not supported. This is an underlying limitation, due to the level at which the two features operate. That is, replication is implemented on top of the Raft WAL files, while truncate is implemented on top of the RocksDB SST files.
- In the future, it will be possible to propagate DDL changes safely to other universes. This is tracked in [#11537](https://github.com/yugabyte/yugabyte-db/issues/11537).

#### Safety of DDL and DML in active-active

- Currently, certain potentially unsafe combinations of DDL and DML are allowed. For example, in having a unique key constraint on a column in an active-active last writer wins mode is unsafe because a violation could be introduced by inserting different values on the two universes, since each of these operations is legal in itself. The ensuing replication can, however, violate the unique key constraint and cause the two universes to permanently diverge and the replication to fail.
- In the future, it will be possible to detect such unsafe combinations and issue a warning, potentially by default. This is tracked in [#11539](https://github.com/yugabyte/yugabyte-db/issues/11539).

#### Kubernetes

- Technically, replication can be set up with Kubernetes-deployed universes. However, the source and target must be able to communicate by directly referencing the pods in the other universe. In practice, this either means that the two universes must be part of the same Kubernetes cluster or that two Kubernetes clusters must have DNS and routing properly setup amongst themselves.
- Being able to have two YugabyteDB universes, each in their own standalone Kubernetes cluster, communicating with each other via a load balancer, is not currently supported, as per [#2422](https://github.com/yugabyte/yugabyte-db/issues/2422).

### Cross-feature interactions

A number of interactions across features is supported.

#### Supported

- TLS is supported for both client and internal RPC traffic. Universes can also be configured with different certificates.
- RPC compression is supported. Note that both universes must be on a version that supports compression, before a compression algorithm is enabled.
- Encryption at rest is supported. Note that the universes can technically use different Key Management Service (KMS) configurations. However, for bootstrapping a target universe, the reliance is on the backup and restore flow. As such, a limitation from that is inherited, which requires that the universe being restored has at least access to the same KMS as the one in which the backup was taken. This means both the source and the target must have access to the same KMS configurations.
- YSQL colocation is supported.
- YSQL geo-partitioning is supported. Note that you must configure replication on all new partitions manually, as DDL changes are not replicated automatically.
- Source and target universes can have different number of tablets.
- Tablet splitting is supported on both source and target universes.

#### Not currently supported

- [Savepoints](../../../explore/ysql-language-features/advanced-features/savepoints/) are not supported, as per [#14308](https://github.com/yugabyte/yugabyte-db/issues/14308).

## Transactional guarantees

<!--

### Atomicity of transactions

This implies one can never read a partial result of a transaction on the sink universe.

-->

### Not globally ordered

Transactions on non-overlapping rows may be applied in a different order on the target universe, than they were on the source universe.

### Last writer wins

In case of active-active configurations, if there are conflicting writes to the same key, then the update with the larger timestamp is considered the latest update. Thus, the deployment is eventually consistent across the two data centers.
