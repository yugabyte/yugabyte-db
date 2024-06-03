---
title: postgresql-hll extension
headerTitle: postgresql-hll extension
linkTitle: postgresql-hll
description: Using the postgresql-hll extension in YugabyteDB
menu:
  stable:
    identifier: extension-postgresql-hll
    parent: pg-extensions
    weight: 20
type: docs
---

The [postgresql-hll](https://github.com/yugabyte/yugabyte-db/tree/master/src/postgres/third-party-extensions/postgresql-hll) extension adds a new data type hll, which is a HyperLogLog data structure. HyperLogLog is a fixed-size, set-like structure used for distinct value counting with tunable precision. For example, in 1280 bytes hll can estimate the count of tens of billions of distinct values with only a few percent error.

First, enable the extension:

```sql
CREATE EXTENSION "hll";
```

To run the helloworld example from the [postgresql-hll](https://github.com/yugabyte/yugabyte-db/tree/master/src/postgres/third-party-extensions/postgresql-hll#usage) repository, connect using `ysqlsh` and run the following:

```sql
CREATE TABLE helloworld (id integer, set hll);
```

```output
CREATE TABLE
```

Insert an empty HLL as follows:

```sql
INSERT INTO helloworld(id, set) VALUES (1, hll_empty());
```

```output
INSERT 0 1
```

Add a hashed integer to the HLL as follows:

```sql
UPDATE helloworld SET set = hll_add(set, hll_hash_integer(12345)) WHERE id = 1;
```

```output
UPDATE 1
```

Add a hashed string to the HLL as follows:

```sql
UPDATE helloworld SET set = hll_add(set, hll_hash_text('hello world')) WHERE id = 1;
```

```output
UPDATE 1
```

Get the cardinality of the HLL as follows:

```sql
SELECT hll_cardinality(set) FROM helloworld WHERE id = 1;
```

```output
 hll_cardinality
-----------------
               2
(1 row)
```

For a more advanced example, see the [Data Warehouse Use Case](https://github.com/yugabyte/yugabyte-db/tree/master/src/postgres/third-party-extensions/postgresql-hll#data-warehouse-use-case).
