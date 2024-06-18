---
title: FAQs about YugabyteDB API compatibility
headerTitle: API FAQ
linkTitle: API FAQ
description: Answers to common questions about YugabyteDB APIs and compatibility.
aliases:
  - /faq/cassandra/
  - /preview/faq/cassandra/
menu:
  preview_faq:
    identifier: faq-api-compatibility
    parent: faq
    weight: 60
type: docs
unversioned: true
rightNav:
  hideH3: true
  hideH4: true
---

### Contents

##### General

- [What client APIs are supported by YugabyteDB?](#what-client-apis-are-supported-by-yugabytedb)
- [When should I pick YCQL over YSQL?](#when-should-i-pick-ycql-over-ysql)
- [What is the difference between ysqlsh and psql?](#what-is-the-difference-between-ysqlsh-and-psql)
- [What is the status of the YEDIS API?](#what-is-the-status-of-the-yedis-api)

##### API compatibility

- [What does API compatibility mean exactly?](#what-does-api-compatibility-mean-exactly)
- [Why are YugabyteDB APIs compatible with popular DB languages?](#why-are-yugabytedb-apis-compatible-with-popular-db-languages)

##### YSQL compatibility with PostgreSQL

- [What is the extent of compatibility with PostgreSQL?](#what-is-the-extent-of-compatibility-with-postgresql)
- [Can I insert data using YCQL, but read using YSQL, or vice versa?](#can-i-insert-data-using-ycql-but-read-using-ysql-or-vice-versa)

##### YCQL compatibility with Apache Cassandra QL

- [Features present in YCQL but not present in CQL](#features-present-in-ycql-but-not-present-in-cql)
- [Features present in both YCQL and CQL but YCQL provides stricter guarantees](#features-present-in-ycql-but-not-present-in-cql)
- [CQL features that are either unnecessary or disallowed in YCQL](#cql-features-that-are-either-unnecessary-or-disallowed-in-ycql)
- [Do INSERTs do "upserts" by default? How do I insert data only if it is absent?](#do-inserts-do-upserts-by-default-how-do-i-insert-data-only-if-it-is-absent)
- [Can I have collection data types in the partition key? Will I be able to do partial matches on that collection data type?](#can-i-have-collection-data-types-in-the-partition-key-will-i-be-able-to-do-partial-matches-on-that-collection-data-type)
- [What is the difference between a `COUNTER` data type and `INTEGER` data type?](#what-is-the-difference-between-a-counter-data-type-and-integer-data-type)
- [How is 'USING TIMESTAMP' different in YugabyteDB?](#how-is-using-timestamp-different-in-yugabytedb)

## General

### What client APIs are supported by YugabyteDB?

YugabyteDB supports two flavors of distributed SQL.

#### Yugabyte SQL (YSQL)

[YSQL](../../api/ysql/) is a fully-relational SQL API that is wire compatible with the SQL language in PostgreSQL. It is best fit for RDBMS workloads that need horizontal write scalability and global data distribution while also using relational modeling features such as JOINs, distributed transactions and referential integrity (such as foreign keys). Get started by [exploring YSQL features](../../quick-start/explore/ysql/).

#### Yugabyte Cloud QL (YCQL)

[YCQL](../../api/ycql/) is a semi-relational SQL API that is best fit for internet-scale OLTP and HTAP applications needing massive data ingestion and blazing-fast queries. It supports distributed transactions, strongly consistent secondary indexes and a native JSON column type. YCQL has its roots in the Cassandra Query Language. Get started by [exploring YCQL features](../../quick-start/explore/ycql/).

{{< note title="Note" >}}

The YugabyteDB APIs are isolated and independent from one another today. This means that the data inserted or managed by one API cannot be queried by the other API. Additionally, there is no common way to access the data across the APIs (external frameworks such as [Presto](../../integrations/presto/) can help for basic cases).

**The net impact is that you need to select an API first before undertaking detailed database schema/query design and implementation.**

{{< /note >}}

### When should I pick YCQL over YSQL?

You should pick YCQL over YSQL if your application:

- Does not require fully-relational data modeling constructs, such as foreign keys and JOINs. Note that strongly-consistent secondary indexes and unique constraints are supported by YCQL.
- Needs to serve low-latency (sub-millisecond) queries.
- Needs TTL-driven automatic data expiration.
- Needs to integrate with stream processors, such as Apache Spark and KSQL.

If you have a specific use case in mind, share it in our [Slack community]({{<slack-invite>}}) and the community can help you decide the best approach.

### What is the difference between ysqlsh and psql?

The YSQL shell (`ysqlsh`) is functionally similar to PostgreSQL's `psql` , but uses different default values for some variables (for example, the default user, default database, and the path to TLS certificates). This is done for the user's convenience. In the Yugabyte `bin` directory, the deprecated `psql` alias opens the `ysqlsh` CLI. For more details, see [ysqlsh](../../admin/ysqlsh/).

### What is the status of the YEDIS API?

In the near-term, Yugabyte is not actively working on new feature or driver enhancements to the [YEDIS](../../yedis/) API other than bug fixes and stability improvements. Current focus is on [YSQL](../../api/ysql/) and [YCQL](../../api/ycql/).

For key-value workloads that need persistence, elasticity and fault-tolerance, YCQL (with the notion of keyspaces, tables, role-based access control, and more) is often a great fit, especially if the application is new rather than an existing one already written in Redis. The YCQL drivers are also more clustering aware, and hence YCQL is expected to perform better than YEDIS for equivalent scenarios. In general, our new feature development (support for data types, built-ins, TLS, backups, and more), correctness testing (using Jepsen), and performance optimization is in the YSQL and YCQL areas.

## API compatibility

### What does API compatibility mean exactly?

API compatibility refers to the fact that the database APIs offered by YugabyteDB servers implement the same wire protocol and modeling/query language as that of an existing database. Because [client drivers](../../drivers-orms/), [command line shells](../../admin/), [IDE integrations](../../tools/), and other [ecosystem integrations](../../integrations/) of the existing database rely on this wire protocol and modeling/query language, they are expected to work with YugabyteDB without major modifications.

{{< note title="Note" >}}
The [YSQL](../../api/ysql/) API is compatible with PostgreSQL. This means PostgreSQL client drivers, psql command line shell, IDE integrations such as TablePlus and DBWeaver, and more can be used with YugabyteDB. The same concept applies to [YCQL](../../api/ycql/) in the context of the Apache Cassandra Query Language.
{{< /note >}}

### Why are YugabyteDB APIs compatible with popular DB languages?

- YugabyteDB's API compatibility is aimed at accelerating developer onboarding. By integrating well with the existing ecosystem, YugabyteDB ensures that developers can get started quickly using a language they are already comfortable with.

- YugabyteDB's API compatibility is not aimed at lift-and-shift porting of existing applications written for the original language. This is because existing applications are not written to take advantage of the distributed, strongly-consistent storage architecture that YugabyteDB provides. For such existing applications, you should modify your previously monolithic PostgreSQL and/or non-transactional Cassandra data access logic as you migrate to YugabyteDB.

## YSQL compatibility with PostgreSQL

### What is the extent of compatibility with PostgreSQL?

As highlighted in [Distributed PostgreSQL on a Google Spanner Architecture – Query Layer](https://www.yugabyte.com/blog/distributed-postgresql-on-a-google-spanner-architecture-query-layer/), YSQL reuses the open source PostgreSQL query layer (written in C) as much as possible and as a result is wire-compatible with PostgreSQL dialect and client drivers. Specifically, YSQL is based on PostgreSQL v11.2. Following are some of the currently supported features:

- DDL statements: CREATE, DROP, and TRUNCATE tables
- Data types: All primitive types including numeric types (integers and floats), text data types, byte arrays, date-time types, UUID, SERIAL, as well as JSONB
- DML statements: INSERT, UPDATE, SELECT, and DELETE. Bulk of core SQL including JOINs, WHERE clauses, GROUP BY, ORDER BY, LIMIT, OFFSET, and SEQUENCES
- Transactions: ABORT, ROLLBACK, BEGIN, END, and COMMIT
- Expressions: Rich set of PostgreSQL built-in functions and operators
- Other Features: VIEWs, EXPLAIN, PREPARE-BIND-EXECUTE, and JDBC support

For more information, refer to [SQL features](../../explore/ysql-language-features).

YugabyteDB's goal is to remain as compatible with PostgreSQL as much as possible. If you see a feature currently missing, please file a [GitHub issue](https://github.com/yugabyte/yugabyte-db/issues) for us.

### Can I insert data using YCQL, but read using YSQL, or vice versa?

The YugabyteDB APIs are currently isolated and independent from one another. Data inserted or managed by one API cannot be queried by the other API. Additionally, Yugabyte does not provide a way to access the data across the APIs. An external framework, such as Presto, might be helpful for basic use cases. For an example that joins YCQL and YSQL data, see the blog post about [Presto on YugabyteDB: Interactive OLAP SQL Queries Made Easy](https://www.yugabyte.com/blog/presto-on-yugabyte-db-interactive-olap-sql-queries-made-easy-facebook/).

Allowing YCQL tables to be accessed from the PostgreSQL-compatible YSQL API as foreign tables using foreign data wrappers (FDW) is on the roadmap. You can comment or increase the priority of the associated [GitHub](https://github.com/yugabyte/yugabyte-db/issues/830) issue.

## YCQL compatibility with Apache Cassandra QL

YCQL is compatible with v3.4 of Apache Cassandra QL (CQL). Following questions highlight how YCQL differs from CQL.

### Features present in YCQL but not present in CQL

1. Strongly-consistent reads and writes for a single row as an absolute guarantee. This is because YugabyteDB is a Consistent & Partition-tolerant (CP) database as opposed to Apache Cassandra which is an Available & Partition-tolerant (AP) database. [Official Jepsen tests](https://www.yugabyte.com/blog/yugabyte-db-1-2-passes-jepsen-testing/) prove this correctness aspect under extreme failure conditions.
1. [JSONB](../../explore/ycql-language/jsonb-ycql) column type for modeling document data.
1. [Distributed transactions](../../develop/learn/transactions/acid-transactions-ysql/) for multi-row ACID transactions.

### Features present in both YCQL and CQL but YCQL provides stricter guarantees

1. [Secondary indexes](../../develop/data-modeling/secondary-indexes-ycql) are by default strongly consistent because internally they use distributed transactions.
1. [INTEGER](../../api/ycql/type_int/) and [COUNTER](../../api/ycql/type_int/) data types are equivalent and both can be incremented without any lightweight transactions.
1. Timeline-consistent tunably-stale reads that maintain ordering guarantees from either a follower replica in the primary cluster or a observer replica in the read replica cluster.

### CQL features that are either unnecessary or disallowed in YCQL

1. Lightweight transactions for compare-and-set operations, such as incrementing integers, are unnecessary because YCQL achieves single-row linearizability by default.
1. Tunable write consistency is disallowed in YCQL because writes are committed at quorum using Raft replication protocol.

For additional information, see [The Truth Behind Tunable Consistency, Lightweight Transactions, and Secondary Indexes](https://www.yugabyte.com/blog/apache-cassandra-lightweight-transactions-secondary-indexes-tunable-consistency/).

### Do INSERTs do "upserts" by default? How do I insert data only if it is absent?

By default, inserts overwrite data on primary key collisions. So INSERTs do an upsert. This an intended CQL feature. In order to insert data only if the primary key is not already present,  add a clause "IF NOT EXISTS" to the INSERT statement. This will cause the INSERT fail if the row exists.

Following is an example from CQL:

```sql
INSERT INTO mycompany.users (id, lastname, firstname)
   VALUES (100, 'Smith', 'John')
IF NOT EXISTS;
```

### Can I have collection data types in the partition key? Will I be able to do partial matches on that collection data type?

Yes, you can have collection data types as primary keys as long as they are marked FROZEN. Collection types that are marked `FROZEN` do not support partial matches.

### What is the difference between a `COUNTER` data type and `INTEGER` data type?

Unlike Apache Cassandra, YugabyteDB COUNTER type is almost the same as INTEGER types. There is no need of lightweight transactions requiring 4 round trips to perform increments in YugabyteDB - these are efficiently performed with just one round trip.

### How is 'USING TIMESTAMP' different in YugabyteDB?

In Apache Cassandra, the highest timestamp provided always wins. For example:

INSERT with timestamp far in the future:

```sql
> INSERT INTO table (c1, c2, c3) VALUES (1, 2, 3) USING TIMESTAMP 1607681258727447;
> SELECT * FROM table;
```

```output
 c1 | c2 | c3
----+----+----
  1 |  2 |  3
```

INSERT at the current timestamp does not overwrite previous value which was written at a higher timestamp:

```sql
> INSERT INTO table (c1, c2, c3) VALUES (1, 2, 4);
> SELECT * FROM table;
```

```output
 c1 | c2 | c3
----+----+----
  1 |  2 |  3
```

On the other hand in Yugabyte, for efficiency purposes INSERTs and UPDATEs without the `USING TIMESTAMP` clause always overwrite the older values. On the other hand, if you have the `USING TIMESTAMP` clause, then appropriate timestamp ordering is performed. Example:

```sql
> INSERT INTO table (c1, c2, c3) VALUES (1, 2, 3) USING TIMESTAMP 1000;
> SELECT * FROM table;
```

```output
 c1 | c2 | c3
----+----+----
  1 |  2 |  3
```

INSERT with timestamp far in the future, this would overwrite old value.

```sql
> INSERT INTO table (c1, c2, c3) VALUES (1, 2, 4) USING TIMESTAMP 1607681258727447;
> SELECT * FROM table;
```

```output
 c1 | c2 | c3
----+----+----
  1 |  2 |  4
```

INSERT without 'USING TIMESTAMP' will always overwrite.

```sql
> INSERT INTO table (c1, c2, c3) VALUES (1, 2, 5);
> SELECT * FROM table;
```

```output
 c1 | c2 | c3
----+----+----
  1 |  2 |  5
```
