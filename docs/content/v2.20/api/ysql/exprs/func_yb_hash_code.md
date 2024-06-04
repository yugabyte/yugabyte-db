---
title: yb_hash_code() function [YSQL]
headerTitle: yb_hash_code()
linkTitle: yb_hash_code()
description: Returns the partition hash code for a given set of expressions.
menu:
  v2.20:
    identifier: api-ysql-exprs-yb_hash_code
    parent: api-ysql-exprs
type: docs
---

## Synopsis

`yb_hash_code` is a function that returns the hash of a set of given input values using the hash function DocDB uses to shard its data. In effect, it provides direct access to the hash value of any given row of a YSQL table, allowing one to infer a row’s physical location. This enables an application to specify queries based on the physical location of a row or set of rows.

## Instructions

`yb_hash_code` can be used anywhere a normal function can be used in a YSQL expression. There is no limitation to where it can be used. It takes in a variable number of parameters whose types must be allowed in YB primary keys. More formally,

```output
yb_hash_code(a1:t1, a2:t2, a3:t3, a4:t4...) → int4 (32 bit integer)
```

Where `a1, a2, a3...` are expressions of type `t1, t2, t3...`, respectively. `t1, t2, t3...` must be types that are currently allowed in a primary key.

## Function pushdown

This function can be either evaluated at the YSQL layer after resolving each of its argument expressions, or certain invocations of it may be pushed down to be evaluated at the DocDB layer. When pushdown happens, the restrictions implied by the yb_hash_code conditions are used to ensure we only send requests to the tablets that definitely contain values in the requested ranges, as well as only scan the relevant ranges in each tablet. This section discusses the situations where they are pushed down. `yb_hash_code` invocations are pushed down when the YSQL optimizer determines that the return values of the function will directly match the values of the hash value column in a requested base table or index table. For example, if you create a table and insert 10000 rows as follows

```sql
CREATE TABLE sample_table (x INTEGER, y INTEGER, z INTEGER, PRIMARY KEY((x,y) HASH, z ASC));
INSERT INTO sample_table SELECT i,i,i FROM generate_series(1,10000) i;
```

Then the following query will evaluate the `yb_hash_code` calls at the DocDB layer:

```sql
SELECT * FROM sample_table WHERE yb_hash_code(x,y) <= 128 AND yb_hash_code(x,y) >= 0;
```

You can verify this with the `EXPLAIN ANALYZE` result of this statement

```sql
EXPLAIN ANALYZE SELECT * FROM sample_table WHERE yb_hash_code(x,y) <= 128 AND yb_hash_code(x,y) >= 0;
```

```output
                                                            QUERY PLAN
-----------------------------------------------------------------------------------------------------------------------------------
 Index Scan using sample_table_pkey on sample_table  (cost=0.00..5.88 rows=16 width=12) (actual time=0.905..0.919 rows=18 loops=1)
   Index Cond: ((yb_hash_code(x, y) <= 128) AND (yb_hash_code(x, y) >= 0))
 Planning Time: 0.071 ms
 Execution Time: 0.600 ms
 Peak Memory Usage: 8 kB
(5 rows)
```

Here, you can see that the primary key index was used and no row was rechecked at the YSQL layer.

As the `yb_hash_code` calls in this statement request the hash code of `x` and `y`, (i.e. the full hash key of `sample_table`), YSQL pushes down these calls. As an added side effect, the RPC request will only be sent out to tablet servers that definitely contain values in the requested hash range.

This pushdown functionality works for secondary indexes too. This is a feature that is currently unavailable with the YCQL counterpart of this function, partition_hash(). For example, if you create an index on `sample_table` as follows:

```sql
CREATE INDEX sample_idx ON sample_table ((x, z) HASH);
```

You can consider a modified version of the above `SELECT` query as follows:

```sql
SELECT * FROM sample_table WHERE yb_hash_code(x,z) <= 128 AND yb_hash_code(x,z) >= 0;
```

Here, the `yb_hash_code` calls are on `x` and `z`. `x` and `z` do not form the full hash key of the base sample_table table but they do form the full hash key of the index table, `sample_idx`. Hence, the optimizer will consider pushing down the calls to the DocDB layer if an index scan using `sample_idx` is chosen. Note that the optimizer may choose not to go with a secondary scan if it deems the requested hash range to be large enough to warrant doing a full table scan instead. The `EXPLAIN ANALYZE` result for this could be as follows

```sql
EXPLAIN ANALYZE SELECT * FROM sample_table WHERE yb_hash_code(x,z) <= 128 AND yb_hash_code(x,z) >= 0;
```

```output
                                                         QUERY PLAN
----------------------------------------------------------------------------------------------------------------------------
 Index Scan using sample_idx on sample_table  (cost=0.00..6.04 rows=16 width=12) (actual time=3.596..3.622 rows=18 loops=1)
   Index Cond: ((yb_hash_code(x, z) <= 128) AND (yb_hash_code(x, z) >= 0))
 Planning Time: 0.222 ms
 Execution Time: 3.739 ms
 Peak Memory Usage: 8 kB
(5 rows)
```

Note that you can also use [pg_hint_plan](../../../../explore/query-1-performance/pg-hint-plan/) to manipulate the index that is used.
Consider a duplicate index of `sample_idx`:

```sql
CREATE INDEX sample_idx_dup ON sample_table ((x,z) HASH);
```

And then execute the same query with a pg_hint_plan directive:

```sql
SET pg_hint_plan.enable_hint=ON;
/*+IndexScan(sample_table sample_idx_dup) */
EXPLAIN ANALYZE SELECT * FROM sample_table WHERE yb_hash_code(x,z) <= 128 AND yb_hash_code(x,z) >= 0;
```

```output
                                                          QUERY PLAN
--------------------------------------------------------------------------------------------------------------------------------
 Index Scan using sample_idx_dup on sample_table  (cost=0.00..6.04 rows=16 width=12) (actual time=3.224..3.249 rows=18 loops=1)
   Index Cond: ((yb_hash_code(x, z) <= 128) AND (yb_hash_code(x, z) >= 0))
 Planning Time: 6.038 ms
 Execution Time: 3.351 ms
 Peak Memory Usage: 8 kB
(4 rows)
```

You can also mix calls that can be pushed down and calls that cannot in a single statement as such

```sql
EXPLAIN ANALYZE SELECT * FROM sample_table WHERE yb_hash_code(x,z) <= 128 and yb_hash_code(x,y) >= 5 AND yb_hash_code(x,y,z) <= 256;
```

```output

                                                          QUERY PLAN
-------------------------------------------------------------------------------------------------------------------------------
 Index Scan using sample_idx_dup on sample_table  (cost=0.00..6.27 rows=16 width=12) (actual time=5.600..5.616 rows=1 loops=1)
   Index Cond: (yb_hash_code(x, z) <= 128)
   Filter: ((yb_hash_code(x, y) >= 5) AND (yb_hash_code(x, y, z) <= 256))
   Rows Removed by Filter: 17
 Planning Time: 0.337 ms
 Execution Time: 5.735 ms
 Peak Memory Usage: 8 kB
(7 rows)
```

In this example, only the first clause is pushed down to an index, `sample_idx`. The rest are filters executed at the YSQL level. The optimizer prefers to push down this particular filter because it selects the fewest rows as determined by the low number of hash values it filters for compared to the `yb_hash_code(x,y) >= 5` filter.

## Use case examples

Here are some expected use case examples of the `yb_hash_code` function in YSQL. We use `sample_table` and `sample_idx` from the previous section for our examples here too:

### Finding the hash code of a particular set of values

Given a set of expressions, you can compute the hash code of them directly as such

```sql
SELECT yb_hash_code(1::int, 2::int, 'sample string'::text);
```

```output
 yb_hash_code
--------------
        23017
```

### Querying rows from a specific tablet

You can request a batch of rows from a tablet of your choice without unnecessarily touching other tablets.

```sql
SELECT * FROM sample_table WHERE yb_hash_code(x,y) >= 0 and yb_hash_code(x,y) <= 128;
```

Assuming each tablet holds at least 128 hash values, this statement will request rows from the first tablet of `sample_table`. Similarly, you can request a batch of physically colocated rows based on index table location too.

```sql
SELECT * FROM sample_table WHERE yb_hash_code(x,z) >= 0 and yb_hash_code(x,z) <= 128;
```

### Querying a table fragment

We can extend the above use case to sample a batch of rows by selecting over a random fixed size interval over the hash partition space. For example, you can select over a random space of 512 hash values. Note that this is 1/128 of the total hash partition space as there are 2^16 = 65536 hash values in total. In order to do this, first select a random lower bound for this interval.

```sql
SELECT floor(random()*65536);
```

```output
 floor
-------
 4600
```

Now, you can search over all rows whose hash values range in [4600, 5112). In this example, we take a count of all such rows.

```sql
SELECT COUNT(*) FROM sample_table WHERE yb_hash_code(x,y) >= 4600 and yb_hash_code(x,y) < 5112;
```

```output
 count
-------
    74
(1 row)
```

Because we use what can be assumed to be a uniformly distributed hash function to partition our rows, we can assume that this is a count of approximately 1/128 of all the rows. Therefore, multiplying this count, 74, by 128 gives us a good estimate of the total number of rows (9472 in this case) in the table without querying and iterating over all tablets.

### Distributed parallel queries

Seeing how you can constrain our queries to any physically collocated group of rows now you can also shard individual aggregate queries across multiple partition groups. These sharded queries can execute in parallel. For example, you can execute a batch of `COUNT(*)` queries as follows on separate threads:

```sql
SELECT COUNT(*) FROM sample_table WHERE yb_hash_code(x,y) >= 0 and yb_hash_code(x,y) < 8192;

SELECT COUNT(*) FROM sample_table WHERE yb_hash_code(x,y) >= 8192 and yb_hash_code(x,y) < 16384;

SELECT COUNT(*) FROM sample_table WHERE yb_hash_code(x,y) >= 16384 and yb_hash_code(x,y) < 24576;
-- .
-- .
-- .
-- .

SELECT COUNT(*) FROM sample_table WHERE yb_hash_code(x,y) >= 57344 and yb_hash_code(x,y) < 65536;
```

Summing up all the results of these queries gives us the equivalent of a `COUNT(*)` over all of `sample_table`.

An example of a script that performs the above on a YSQL instance can be found in the [yb-tools](https://github.com/yugabyte/yb-tools/blob/main/ysql_table_row_count.py) repository.

## See also

- [partition_hash](../../../../api/ycql/expr_fcall/#partition-hash-function)
