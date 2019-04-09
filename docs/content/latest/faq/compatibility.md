---
title: API Compatibility
linkTitle: API Compatibility
description: API Compatibility
aliases:
  - /faq/cassandra/
  - /latest/faq/cassandra/
menu:
  latest:
    parent: faq
    weight: 2740
isTocNested: true
showAsideToc: true
---

## Why are YugaByte DB APIs compatible with popular DB languages?

- YugaByte DB's API compatibility is aimed at accelerating developer onboarding. By integrating well with the existing ecosystem, YugaByte DB ensures that developers can get started easily using a language they are already comfortable with. 

- YugaByte DB's API compatibility is not aimed at lift-and-shift porting of existing applications written for the original language. This is because existing applications are most likely not written to take advantage of the distributed SQL APIs provided by YugaByte DB. For such existing applications, developers should expect to modify their previously monolithic PostgreSQL and/or non-transactional Cassandra data access logic as they look to migrate to YugaByte DB.

## YSQL Compatibility with PostgreSQL

### What is the extent of compatibility with PostgreSQL?

As highlighted in [Distributed PostgreSQL on a Google Spanner Architecture – Query Layer](https://blog.yugabyte.com/distributed-postgresql-on-a-google-spanner-architecture-query-layer/), YSQL reuses open source PostgreSQL’s query layer (written in C) as much as possible and as a result is wire-compatible with PostgreSQL dialect and client drivers. Specifically, YSQL v1.2 is based on PostgreSQL v11.2. Following are some of the currently supported features:

- DDL statements: CREATE, DROP and TRUNCATE tables
- Data types: All primitive types including numeric types (integers and floats), text data types, byte arrays, date-time types, UUID, SERIAL, as well as JSONB
- DML statements: INSERT, UPDATE, SELECT and DELETE. Bulk of core SQL including JOINs, WHERE clauses, GROUP BY, ORDER BY, LIMIT, OFFSET and SEQUENCES
- Transactions: ABORT, ROLLBACK, BEGIN, END, and COMMIT
- Expressions: Rich set of PostgreSQL built-in functions and operators
- Other Features: VIEWs, EXPLAIN, PREPARE-BIND-EXECUTE, and JDBC support

YugaByte DB's goal is to remain as compatible with PostgreSQL as possible. If you see a feature currently missing, please file a [GitHub issue](https://github.com/YugaByte/yugabyte-db/issues) for us.

## YCQL Compatibility with Apache Cassandra

### Do INSERTs do “upserts” by default? How do I insert data only if it is absent?

By default, inserts overwrite data on primary key collisions. So INSERTs do an upsert. This an intended CQL feature. In order to insert data only if the primary key is not already present,  add a clause "IF NOT EXISTS" to the INSERT statement. This will cause the INSERT fail if the row exists.

Here is an example from CQL:

```sql
INSERT INTO mycompany.users (id, lastname, firstname) 
   VALUES (100, ‘Smith’, ‘John’) 
IF NOT EXISTS;
```

### Can I have collection data types in the partition key? Will I be able to do partial matches on that collection data type?

Yes, you can have collection data types as primary keys as long as they are marked FROZEN. Collection types that are marked `FROZEN` do not support partial matches.

### What is the difference between a `COUNTER` type and `INTEGER` types?

Unlike Apache Cassandra, YugaByte COUNTER type is almost the same as INTEGER types. There is no need of lightweight transactions requiring 4 round trips to perform increments in YugaByte - these are efficiently performed with just one round trip.

### How is 'USING TIMESTAMP' different in YugaByte?

In Apache Cassandra, the highest timestamp provided always wins. Example:

INSERT with timestamp far in the future.
```sql
> INSERT INTO table (c1, c2, c3) VALUES (1, 2, 3) USING TIMESTAMP 1607681258727447;
> SELECT * FROM table;
```
```
 c1 | c2 | c3
----+----+----
  1 |  2 |  3
```
INSERT at the current timestamp does not overwrite previous value which was written at a higher
timestamp.
```sql
> INSERT INTO table (c1, c2, c3) VALUES (1, 2, 4); 
> SELECT * FROM table;
```
```
 c1 | c2 | c3
----+----+----
  1 |  2 |  3
```

On the other hand in YugaByte, for efficiency purposes INSERTs and UPDATEs without the `USING
TIMESTAMP` clause always overwrite the older values. On the other hand if we have the `USING
TIMESTAMP` clause, then appropriate timestamp ordering is performed. Example:

```sql
> INSERT INTO table (c1, c2, c3) VALUES (1, 2, 3) USING TIMESTAMP 1000;
> SELECT * FROM table;
```
```
 c1 | c2 | c3
----+----+----
  1 |  2 |  3
```
INSERT with timestamp far in the future, this would overwrite old value.
```sql
> INSERT INTO table (c1, c2, c3) VALUES (1, 2, 4) USING TIMESTAMP 1607681258727447;
> SELECT * FROM table;
```
```
 c1 | c2 | c3
----+----+----
  1 |  2 |  4
```
INSERT without 'USING TIMESTAMP' will always overwrite.
```sql
> INSERT INTO table (c1, c2, c3) VALUES (1, 2, 5); 
> SELECT * FROM table;
```
```
 c1 | c2 | c3
----+----+----
  1 |  2 |  5
```
