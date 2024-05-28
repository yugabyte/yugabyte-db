---
title: Design goals
headerTitle: Design goals
linkTitle: Design goals
description: Learn the design goals that drive the building of YugabyteDB
headcontent: Goals and ideas considered during designing YugabyteDB
image: fa-sharp fa-thin fa-lightbulb-exclamation-on
aliases:
  - /preview/architecture/design-goals/
menu:
  preview:
    identifier: architecture-design-goals
    parent: architecture
    weight: 100
type: docs
---

## Scalability

YugabyteDB scales out horizontally by adding more nodes to handle increasing data volumes and higher workloads. With YugabyteDB, you can also opt for vertical scaling choosing more powerful infrastructure components. {{<link "../../explore/linear-scalability/">}}

## High Availability

YugabyteDB ensures continuous availability, even in the face of individual node failures or network partitions. YugabyteDB achieves this by replicating data across multiple nodes and implementing failover mechanisms via leader election. {{<link "../../explore/fault-tolerance">}}

## Fault Tolerance

YugabyteDB is resilient to various types of failures, such as node crashes, network partitions, disk failures, and other hardware or software faults and failure of various fault domains. It can automatically recover from these failures without data loss or corruption. {{<link "../../explore/fault-tolerance">}}

## Consistency

YugabyteDB supports distributed transactions while offering strong consistency guarantees (ACID) in the face of potential failures.
For more information, see the following:

- [Achieving consistency with Raft consensus](../docdb-replication/raft)
- [Single-row linearizable transactions in YugabyteDB](../transactions/single-row-transactions/)
- [The architecture of distributed transactions](../transactions/distributed-txns/)

## Single-row linearizability

YugabyteDB supports single-row linearizable writes. Linearizability is one of the strongest single-row consistency models, and implies that every operation appears to take place atomically and in some total linear order that is consistent with the real-time ordering of those operations. In other words, the following is expected to be true for operations on a single row:

- Operations can execute concurrently, but the state of the database at any point in time must appear to be the result of some totally ordered, sequential execution of operations.
- If operation A completes before operation B begins, then B should logically take effect after A.

## Multi-row ACID transactions

YugabyteDB supports multi-row transactions with three isolation levels: Serializable, Snapshot (also known as repeatable read), and Read Committed isolation.

- The [YSQL API](../../api/ysql/) supports Serializable, Snapshot (default), and Read Committed isolation {{<badge/ea>}} using the PostgreSQL isolation level syntax of `SERIALIZABLE`, `REPEATABLE READ`, and `READ COMMITTED` respectively. For more details, see [Isolation levels](#transaction-isolation-levels).
- The [YCQL API](../../api/ycql/) supports only Snapshot isolation (default) using the [BEGIN TRANSACTION](../../api/ycql/dml_transaction/) syntax.

## Partition Tolerance - CAP

In terms of the [CAP theorem](https://en.wikipedia.org/wiki/CAP_theorem), YugabyteDB is a consistent and partition-tolerant (CP) database which achieves very high availability at the same time. The architectural design of YugabyteDB is similar to Google Cloud Spanner, another CP system. The description of [Spanner](https://cloudplatform.googleblog.com/2017/02/inside-Cloud-Spanner-and-the-CAP-Theorem.html) is also applicable to YugabyteDB. The key takeaway is that no system provides 100% availability, so the pragmatic question is whether or not the system delivers sufficient high availability that most users no longer have to be concerned about outages. For example, given that there are many sources of outages for an application, if YugabyteDB is an insignificant contributor to its downtime, then users are correct not to worry about it.

Split-brain is a computing scenario in which data and availability inconsistencies arise when a distributed system incurs a network partition. For YugabyteDB, when a network partition occurs, the remaining (majority for write acknowledgment purposes) Raft group peers elect a new tablet leader. YugabyteDB implements _leader leases_, which ensures that a single tablet leader exists throughout the entire distributed system including when network partitions occur. Leader leases have a default value of two seconds and can be configured to use a different value. This architecture ensures that YugabyteDB's distributed database is not susceptible to the split-brain condition.

## Data distribution

YugabyteDB splits table data into smaller pieces called tablets and distributes these tablets across different nodes in the cluster for better performance, availability, and resiliency. YugabyteDB automatically re-balances the number of tablets on each node as the cluster scales.

## Load balancing

YugabyteDB monitors and automatically re-balances the number of tablet leaders and followers on each node continuously. This leads to the distribution of reads and writes across multiple nodes avoiding hot-spots and ensuring efficient resource utilization.

## Data locality

YugabyteDB supports colocated tables and databases which enables related data to be kept together on the same node for performance reasons. {{<link "../docdb-sharding/colocated-tables">}}

## Security

YugabyteDB supports robust security measures, such as [authentication](../../secure/authentication/), [authorization](../../secure/authorization/), [encryption at rest](../../secure/encryption-at-rest/), [encryption in transit](../../secure/tls-encryption/), and [auditing](../../secure/audit-logging/) capabilities.

## Operational simplicity

YugabyteDB has been designed with operational simplicity in mind, providing features like [automated deployment](../../deploy/), [configuration management](../../reference/configuration/), [monitoring](../../explore/observability/), and self-healing capabilities to reduce operational overhead.

## Heterogeneous workload support

Depending on the use case, the database may need to support diverse workloads, such as [transactional processing](../../benchmark/tpcc-ysql/), [analytical queries](../../sample-data/retail-analytics/), [real-time data ingestion](../../tutorials/azure/azure-event-hubs/), [time-series](../../develop/common-patterns/timeseries/), and [key-value](../../benchmark/key-value-workload-ycql/) workloads.

## Transaction isolation levels

Transaction isolation is foundational to handling concurrent transactions in databases. YugabyteDB supports three strict transaction isolation levels in [YSQL](../../api/ysql/).

- [Read Committed](../transactions/read-committed/) {{<badge/ea>}}, which maps to the SQL isolation level of the same name
- [Serializable](../../explore/transactions/isolation-levels/#serializable-isolation), which maps to the SQL isolation level of the same name
- [Snapshot](../../explore/transactions/isolation-levels/#snapshot-isolation), which maps to the SQL Repeatable Read isolation level

In [YCQL](../../api/ycql/), it supports only [Snapshot isolation](../../develop/learn/transactions/acid-transactions-ycql/#note-on-linearizability) using the `BEGIN TRANSACTION` syntax.

## SQL compatibility

[YSQL](../../api/ysql/) is a fully relational [SQL API](../../explore/ysql-language-features/) that is wire-compatible with the SQL language in PostgreSQL. It is best fit for RDBMS workloads that need horizontal write scalability and global data distribution, while also using relational modeling features such as joins, distributed transactions, and referential integrity (such as foreign keys). Note that YSQL [reuses the native query layer](https://www.yugabyte.com/blog/why-we-built-yugabytedb-by-reusing-the-postgresql-query-layer/) of the PostgreSQL open source project.

In addition:

- New changes to YSQL do not break existing PostgreSQL functionality.

- YSQL is designed with migrations to newer PostgreSQL versions over time as an explicit goal. This means that new features are implemented in a modular fashion in the YugabyteDB codebase to enable rapid integration with new PostgreSQL features as an ongoing process.

- YSQL supports wide SQL functionality, such as the following:
  - [All data types](../../explore/ysql-language-features/data-types/)
  - [Built-in functions and expressions](../../explore/ysql-language-features/expressions-operators/)
  - [Joins](../../explore/ysql-language-features/join-strategies/) - inner join, outer join, full outer join, cross join, natural join
  - [Constraints](../../explore/ysql-language-features/indexes-constraints/) - primary key, foreign key, unique, not null, check
  - [Secondary indexes](../../explore/ysql-language-features/indexes-constraints/secondary-indexes-ysql/) (including multi-column and covering columns)
  - [Distributed transactions](../../explore/transactions/distributed-transactions-ysql/) (Serializable, Snapshot, and Read Committed Isolation)
  - [Views](../../explore/ysql-language-features/advanced-features/views/)
  - [Stored procedures](../../explore/ysql-language-features/stored-procedures/)
  - [Triggers](../../explore/ysql-language-features/triggers/)

## Cassandra compatibility

[YCQL](../../api/ycql/) is a [semi-relational CQL API](../../explore/ycql-language/) that is best suited for internet-scale OLTP and HTAP applications needing massive write scalability and fast queries. YCQL supports distributed transactions, strongly-consistent secondary indexes, and a native JSON column type. YCQL has its roots in the Cassandra Query Language. {{<link "../query-layer">}}

## Performance

Written in C++ to ensure high performance and the ability to use large memory heaps (RAM) as an internal database cache, YugabyteDB is optimized primarily to run on SSDs and Non-Volatile Memory Express (NVMe) drives. To handle ever-growing event data workload characteristics in mind, YugabyteDB ensures the following:

- high write throughput
- high client concurrency
- high data density (total data set size per node)
- ability to handle ever-growing event data use cases

For more information, see [High performance in YugabyteDB](../docdb/performance/).

## Geographically distributed deployments

YugabyteDB supports [Row Level Geo-Partitioning](../../explore/multi-region-deployments/row-level-geo-partitioning) of tables. This enables a specific row to be placed in a specific geography. It leverages two PostgreSQL features, [Table partitioning](../../explore/ysql-language-features/advanced-features/partitions/) and [Tablespaces](../../explore/going-beyond-sql/tablespaces/) to accomplish this elegantly.

## Cloud-ready

YugabyteDB is a cloud-native database, and can be deployed out of the box in most public cloud services like [AWS, GCP, and Azure](../../deploy/public-clouds/). YugabyteDB also supports a [mult-cloud deployement](../../develop/multi-cloud/multicloud-setup/) which enables a cluster to be spread across different cloud providers.

## Running on commodity hardware

YugabyteDB has been designed with several cloud-native principles in mind.

- Ability to run on any public cloud or on-premises data center. This includes commodity hardware on bare metal machines, virtual machines, and containers.
- Not having hard external dependencies. For example, YugabyteDB does not rely on atomic clocks but can use an atomic clock if available.

## Kubernetes-ready

YugabyteDB works natively in Kubernetes and other containerized environments as a stateful application. {{<link "../../deploy/kubernetes/">}}

## Open source

YugabyteDB is 100% open source under the very permissive Apache 2.0 license. The source code is available on [GitHub](https://github.com/yugabyte/yugabyte-db).

## Learn more

- [Overview of the architectural layers in YugabyteDB](../)
- [DocDB architecture](../docdb/)
- [Transactions in DocDB](../transactions/)
- [Query layer design](../query-layer/)
