---
title: HypoPG extension
headerTitle: HypoPG extension
linkTitle: HypoPG
description: Using the HypoPG extension in YugabyteDB
menu:
  preview:
    identifier: extension-hypopg
    parent: pg-extensions
    weight: 20
type: docs
---

The [HypoPG](https://github.com/HypoPG/hypopg) PostgreSQL extension adds support for hypothetical indexes. Use hypothetical indexes to test whether adding an index improves the performance of problematic queries, without expending resources to create them.

To enable the extension:

```sql
CREATE EXTENSION hypopg;
```

Create a table as follows:

```sql
CREATE TABLE up_and_down (up int primary key, down int);
INSERT INTO up_and_down SELECT a AS up, 10001-a AS down FROM generate_series(1,10000) a;
```

The `up_and_down` table has no indexes, but is defined with a primary key. As a result, when using the primary key, records are retrieved directly:

```sql
EXPLAIN SELECT * FROM up_and_down WHERE up = 999;
```

```output
                                     QUERY PLAN
------------------------------------------------------------------------------------
 Index Scan using up_and_down_pkey on up_and_down  (cost=0.00..4.11 rows=1 width=8)
   Index Cond: (up = 999)
```

However, because it doesn't have an index, fetching a value from the `down` column results in a sequential scan:

```sql
EXPLAIN SELECT * FROM up_and_down WHERE down = 999;
```

```output
                           QUERY PLAN
----------------------------------------------------------------
 Seq Scan on up_and_down  (cost=0.00..102.50 rows=1000 width=8)
   Filter: (down = 999)
```

To see what would happen if you were to create an index for the `down` column without actually creating the index, use HypoPG as follows:

```sql
SELECT * FROM hypopg_create_index('create index on up_and_down(down)');
```

```output
 indexrelid |          indexname
------------+-----------------------------
      13283 | <13283>lsm_up_and_down_down
```

Explain now shows that the planner would use the index:

```sql
EXPLAIN SELECT * FROM up_and_down WHERE down = 999;
```

```output
                                            QUERY PLAN
--------------------------------------------------------------------------------------------------
 Index Scan using <13283>lsm_up_and_down_down on up_and_down  (cost=0.00..4.01 rows=1000 width=8)
   Index Cond: (down = 999)
```

As the index is not really created, if you use `EXPLAIN ANALYZE`, the hypothetical index is ignored:

```sql
EXPLAIN ANALYZE SELECT * FROM up_and_down WHERE down = 999;
```

```output
                                                 QUERY PLAN
------------------------------------------------------------------------------------------------------------
 Seq Scan on up_and_down  (cost=0.00..102.50 rows=1000 width=8) (actual time=35.678..35.687 rows=1 loops=1)
   Filter: (down = 999)
   Rows Removed by Filter: 9999
 Planning Time: 0.041 ms
 Execution Time: 35.735 ms
 Peak Memory Usage: 0 kB
```

You can query the hypothetical indexes you created using the `hypopg()` function:

```sql
SELECT * FROM hypopg();
```

```output
          indexname          | indexrelid | indrelid | innatts | indisunique | indkey | indcollation | indclass | indoption | indexprs | indpred | amid
-----------------------------+------------+----------+---------+-------------+--------+--------------+----------+-----------+----------+---------+------
 <13283>lsm_up_and_down_down |      13283 |    16927 |       1 | f           | 2      | 0            | 9942     |           |          |         | 9900
```

If you create multiple hypothetical indexes, you can drop a single hypothetical index using its `indexrelid` as follows:

```sql
SELECT * FROM hypopg_drop_index(13283);
```

```output
 hypopg_drop_index
-------------------
 t
```

To remove all hypothetical indexes, log out or quit your session.

```sql
\q
```

For more information, refer to the [HypoPG documentation](https://hypopg.readthedocs.io/en/rel1_stable/).
