---
title: Cassandra Compatibility
linkTitle: Cassandra Compatibility
description: Apache Cassandra Compatibility in YCQL
aliases:
  - /faq/cassandra/
menu:
  1.0:
    identifier: cassandra-api
    parent: faq
    weight: 2500
---

## Do INSERTs do “upserts” by default? How do I insert data only if it is absent?

By default, inserts overwrite data on primary key collisions. So INSERTs do an upsert. This an intended CQL feature. In order to insert data only if the primary key is not already present,  add a clause "IF NOT EXISTS" to the INSERT statement. This will cause the INSERT fail if the row exists.

Here is an example from CQL:

```sql
INSERT INTO mycompany.users (id, lastname, firstname) 
   VALUES (100, ‘Smith’, ‘John’) 
IF NOT EXISTS;
```

## Can I have collection data types in the partition key? Will I be able to do partial matches on that collection data type?

Yes, you can have collection data types as primary keys as long as they are marked FROZEN. Collection types that are marked `FROZEN` do not support partial matches.

## What is the difference between a `COUNTER` type and `INTEGER` types?

Unlike Apache Cassandra, YugaByte COUNTER type is almost the same as INTEGER types. There is no need of lightweight transactions requiring 4 round trips to perform increments in YugaByte - these are efficiently performed with just one round trip.

## How is 'USING TIMESTAMP' different in YugaByte?

In Apache Cassandra, the highest timestamp provided always wins. Example:

```sql
/* INSERT with timestamp far in the future. */
> INSERT INTO table (c1, c2, c3) VALUES (1, 2, 3) USING TIMESTAMP 1607681258727447;
> SELECT * FROM table;

 c1 | c2 | c3
----+----+----
  1 |  2 |  3

/* INSERT at the current timestamp does not overwrite previous value which was written at a higher
timestamp. */
> INSERT INTO table (c1, c2, c3) VALUES (1, 2, 4); 
> SELECT * FROM table;

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

 c1 | c2 | c3
----+----+----
  1 |  2 |  3

/* INSERT with timestamp far in the future, this would overwrite old value. */
> INSERT INTO table (c1, c2, c3) VALUES (1, 2, 4) USING TIMESTAMP 1607681258727447;
> SELECT * FROM table;

 c1 | c2 | c3
----+----+----
  1 |  2 |  4

/* INSERT without 'USING TIMESTAMP' will always overwrite. */
> INSERT INTO table (c1, c2, c3) VALUES (1, 2, 5); 
> SELECT * FROM table;

 c1 | c2 | c3
----+----+----
  1 |  2 |  5
```
