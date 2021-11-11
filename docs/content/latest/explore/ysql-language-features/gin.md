# Generalized inverted indexes

In YugabyteDB, tables and secondary indexes are both key-value stores.
For tables, the key-value store maps primary keys to values.
For secondary indexes, the key-value store maps index keys to primary keys.

Regular indexes index columns.
This makes queries with conditions on the columns more efficient.
For example, if one had a regular index on a single int array column (currently not possible in Yugabyte), queries like `WHERE myintarray = '{1,3,6}'` would be more efficient when using the index.
However, queries like `WHERE myintarray ? 3` (meaning "is 3 an element?") would not benefit from the regular index.

Generalized inverted indexes (GIN indexes) index elements inside container columns.
This makes queries with conditions on elements inside the columns more efficient.
The above example would benefit from a GIN index since we can look up the key `3` in the gin index.

## Compatible types

GIN indexes can only be created over a few types:

- a GIN index on a tsvector column indexes text elements
- a GIN index on a array column indexes array elements
- a GIN index on a jsonb column indexes keys/values

With extensions, more types can be supported.
However, extension support is still in progress:

- [ ] btree_gin
- [ ] hstore
- [x] pg_trgm

## Grammar

### CREATE INDEX

Create the index using `USING ybgin` to specify the index access method.

`CREATE INDEX ON mytable USING ybgin (mycol);`

The `gin` access method is reserved for temporary relations while `ybgin` is for Yugabyte-backed relations.
You may still specify `USING gin`, and, if `mytable` is not a temporary table, it will be automatically substituted for `ybgin`.

Limitations:

- Multicolumn GIN indexes are not supported: this can be done in the future.
- Unique indexes are not supported: this doesn't make sense.
- ASC/DESC/HASH cannot be specified: it defaults to ASC only for prefix match purposes, so this can be relaxed in the future.

### DELETE, INSERT, UPDATE

Writes are fully supported.
However, note that UPDATEs may be expensive since they are currently implemented as DELETE + INSERT.

### SELECT

Only [certain SELECTs][operators] use the GIN index.
Even among them, there are currently some limitations:

- Special scans are not supported.
  - Scans cannot include rows whose column has zero index keys.
  - Scans cannot scan the entire index.
- Scans cannot ask for more than one index key.
  For example, a request for all rows whose array contains elements 1 or 3 will fail, but one that asks for elements 1 _and_ 3 can succeed by choosing one of the elements for index scan and rechecking the entire condition later.
  However, the choice between 1 and 3 is currently unoptimized, so 3 may be chosen even though 1 corresponds to less rows.
- Recheck is always done rather than on a case-by-case basis, meaning there can be an unnecessary performance penalty.

If a query is unsupported, you may avoid the error by disabling index scan: `SET enable_indexscan TO off;` before the query and `SET enable_indexscan TO on;` afterwards.
In the near future, cost estimates should route such queries to sequential scan.

[operators]: https://www.postgresql.org/docs/13/gin-builtin-opclasses.html

## Changes from upstream PostgreSQL

For those familiar with upstream PostgreSQL GIN indexes, Yugabyte GIN indexes are slightly different.

- Upstream PostgreSQL uses bitmap index scan; Yugabyte uses index scan.
- Deletes to the index are written explicitly: this is due to storage-layer architecture differences, and it's also true for regular indexes.
- Fast update is not supported: this isn't practical for a distributed, log-structured database.
- Fuzzy search limit is not supported: it can be in the future.

## Examples

### Setup

To begin, I set up a cluster using `yb-ctl`.

```sh
./bin/yb-ctl --data_dir /tmp/gindemo --rf 3 create --ip_start 201
./bin/ysqlsh --host 127.0.0.201
```

Set up the tables, indexes, and data.

```sql
CREATE TABLE vectors (v tsvector, k serial PRIMARY KEY);
CREATE TABLE arrays (a int[], k serial PRIMARY KEY);
CREATE TABLE jsonbs (j jsonb, k serial PRIMARY KEY);

-- Use NONCONCURRENTLY since there is no risk of online ops.
CREATE INDEX NONCONCURRENTLY ON vectors USING ybgin (v);
CREATE INDEX NONCONCURRENTLY ON arrays USING ybgin (a);
CREATE INDEX NONCONCURRENTLY ON jsonbs USING ybgin (j);

INSERT INTO vectors VALUES
    (to_tsvector('simple', 'the quick brown fox')),
    (to_tsvector('simple', 'jumps over the')),
    (to_tsvector('simple', 'lazy dog'));
-- Add some filler rows to make sequential scan more costly.
INSERT INTO vectors SELECT to_tsvector('simple', 'filler') FROM generate_series(1, 1000);

INSERT INTO arrays VALUES
    ('{1,1,6}'),
    ('{1,6,1}'),
    ('{2,3,6}'),
    ('{2,5,8}'),
    ('{null}'),
    ('{}'),
    (null);
INSERT INTO arrays SELECT '{0}' FROM generate_series(1, 1000);

INSERT INTO jsonbs VALUES
    ('{"a":{"number":5}}'),
    ('{"some":"body"}'),
    ('{"some":"one"}'),
    ('{"some":"thing"}'),
    ('{"some":["where","how"]}'),
    ('{"some":{"nested":"jsonb"}, "and":["another","element","not","a","number"]}');
INSERT INTO jsonbs SELECT '"filler"' FROM generate_series(1, 1000);
```

### Timing

Here are some examples to show the speed improvement of queries using GIN index.
GIN indexes currently support IndexScan only, not IndexOnlyScan.
The difference is that IndexScan uses the results of a scan to the index for filtering on the indexed table whereas an IndexOnlyScan need not go to the indexed table since the results from the index are sufficient.
Therefore, a GIN index scan can be more costly than a sequential scan straight to the main table if the index scan does not filter out many rows.
Since cost estimates currently aren't very accurate, the more costly index scan may be chosen in some cases.

The assumption in the following examples is that the user is using the GIN index in ways that take advantage of it.

First, enable timing for future queries.

```sql
\timing on
```

First, test GIN index on tsvector:

```
SET enable_indexscan = off;
SELECT * FROM vectors WHERE v @@ to_tsquery('simple', 'the');
-- Run it several times to reduce cache bias.
SELECT * FROM vectors WHERE v @@ to_tsquery('simple', 'the');
SELECT * FROM vectors WHERE v @@ to_tsquery('simple', 'the');
```

```output
                  v                  | k
-------------------------------------+---
 'brown':3 'fox':4 'quick':2 'the':1 | 1
 'jumps':1 'over':2 'the':3          | 2
(2 rows)

Time: 11.141 ms
```

```sql
SET enable_indexscan = on;
SELECT * FROM vectors WHERE v @@ to_tsquery('simple', 'the');
-- Run it several times to reduce cache bias.
SELECT * FROM vectors WHERE v @@ to_tsquery('simple', 'the');
SELECT * FROM vectors WHERE v @@ to_tsquery('simple', 'the');
```

```output
...
Time: 2.838 ms
```

Notice the over 3x timing improvement when using GIN index.
This is on a relatively small table: a little over 1000 rows.
With more and/or bigger rows, the timing improvement should get better.

Next, int array:

```sql
SET enable_indexscan = off;
SELECT * FROM arrays WHERE a @> '{6}';
SELECT * FROM arrays WHERE a @> '{6}';
SELECT * FROM arrays WHERE a @> '{6}';
```

```output
    a    | k
---------+---
 {1,1,6} | 1
 {1,6,1} | 2
 {2,3,6} | 3
(3 rows)

Time: 9.501 ms
```

```sql
SET enable_indexscan = on;
SELECT * FROM arrays WHERE a @> '{6}';
SELECT * FROM arrays WHERE a @> '{6}';
SELECT * FROM arrays WHERE a @> '{6}';
```

```output
...
Time: 2.989 ms
```

Next, jsonb:

```sql
SET enable_indexscan = off;
SELECT * FROM jsonbs WHERE j ? 'some';
SELECT * FROM jsonbs WHERE j ? 'some';
SELECT * FROM jsonbs WHERE j ? 'some';
```

```output
                                         j                                          | k
------------------------------------------------------------------------------------+---
 {"some": ["where", "how"]}                                                         | 5
 {"and": ["another", "element", "not", "a", "number"], "some": {"nested": "jsonb"}} | 6
 {"some": "thing"}                                                                  | 4
 {"some": "body"}                                                                   | 2
 {"some": "one"}                                                                    | 3
(5 rows)

Time: 13.451 ms
```

```sql
SET enable_indexscan = on;
SELECT * FROM jsonbs WHERE j ? 'some';
SELECT * FROM jsonbs WHERE j ? 'some';
SELECT * FROM jsonbs WHERE j ? 'some';
```

```output
...
Time: 2.115 ms
```

### Unsupported queries

Sometimes, an unsupported query may be encountered by getting an ERROR.
Here, I show workarounds to some of these cases.

#### more than one required scan entry

Perhaps the most common issue would be "cannot use more than one required scan entry".
It means that the GIN index scan internally tries to fetch more than one index key.
Since this is currently not supported, it throws an ERROR.

```sql
RESET enable_indexscan;
\timing off

SELECT * FROM vectors WHERE v @@ to_tsquery('simple', 'quick | lazy');
```

```output
ERROR:  unsupported ybgin index scan
DETAIL:  ybgin index method cannot use more than one required scan entry: got 2.
Time: 2.885 ms
```

One way to get around this is to use `OR` outside the tsquery:

```sql
SELECT * FROM vectors WHERE v @@ to_tsquery('simple', 'quick')
                      OR v @@ to_tsquery('simple', 'lazy');
```

```output
                  v                  | k
-------------------------------------+---
 'brown':3 'fox':4 'quick':2 'the':1 | 1
 'dog':2 'lazy':1                    | 3
(2 rows)

Time: 10.169 ms
```

However, this doesn't use the index:

```sql
EXPLAIN
SELECT * FROM vectors WHERE v @@ to_tsquery('simple', 'quick')
                      OR v @@ to_tsquery('simple', 'lazy');
```

```output
                              QUERY PLAN
-----------------------------------------------------------------------
 Seq Scan on vectors  (cost=0.00..105.00 rows=1000 width=36)
   Filter: ((v @@ '''quick'''::tsquery) OR (v @@ '''lazy'''::tsquery))
(2 rows)

Time: 1.050 ms
```

Another way that does use the index is `UNION`:

```sql
EXPLAIN
SELECT * FROM vectors WHERE v @@ to_tsquery('simple', 'quick') UNION
SELECT * FROM vectors WHERE v @@ to_tsquery('simple', 'lazy');
```

```output
                                                  QUERY PLAN
--------------------------------------------------------------------------------------------------------------
 Unique  (cost=163.76..178.76 rows=2000 width=36)
   ->  Sort  (cost=163.76..168.76 rows=2000 width=36)
         Sort Key: vectors.v, vectors.k
         ->  Append  (cost=4.00..54.10 rows=2000 width=36)
               ->  Index Scan using vectors_v_idx on vectors  (cost=4.00..12.05 rows=1000 width=36)
                     Index Cond: (v @@ '''quick'''::tsquery)
               ->  Index Scan using vectors_v_idx on vectors vectors_1  (cost=4.00..12.05 rows=1000 width=36)
                     Index Cond: (v @@ '''lazy'''::tsquery)
(8 rows)

Time: 1.143 ms
```

```sql
SELECT * FROM vectors WHERE v @@ to_tsquery('simple', 'quick') UNION
SELECT * FROM vectors WHERE v @@ to_tsquery('simple', 'lazy');
```

```output
                  v                  | k
-------------------------------------+---
 'dog':2 'lazy':1                    | 3
 'brown':3 'fox':4 'quick':2 'the':1 | 1
(2 rows)

Time: 5.559 ms
```

If performance doesn't matter, the universal fix is to disable index scan so that sequential scan is used.
For sequential scan to be chosen, make sure that sequential scan is not also disabled.

```sql
SET enable_indexscan = off;
SELECT * FROM vectors WHERE v @@ to_tsquery('simple', 'quick | lazy');
```

```output
                  v                  | k
-------------------------------------+---
 'brown':3 'fox':4 'quick':2 'the':1 | 1
 'dog':2 'lazy':1                    | 3
(2 rows)

Time: 11.188 ms
```

Notice that the modified query using the index is still 2x faster than the original query to the main table.

#### non-default search mode

All search modes/strategies besides the default one are currently unsupported.
Many of these are best off using a sequential scan.
In fact, the query planner avoids many of these types of index scans by increasing the cost, leading to sequential scan being chosen as the better alternative.
Nevertheless, here are some cases that hit the ERROR.

```sql
RESET enable_indexscan;
\timing off

SELECT * FROM arrays WHERE a = '{}';
```

```output
ERROR:  unsupported ybgin index scan
DETAIL:  ybgin index method does not support non-default search mode: include-empty.
```

```sql
SELECT * FROM arrays WHERE a <@ '{6,1,1,null}';
```

```output
ERROR:  unsupported ybgin index scan
DETAIL:  ybgin index method does not support non-default search mode: include-empty.
```

There's currently no choice but to use a sequential scan on these.

### `jsonb_path_ops`

By default, jsonb GIN indexes use the opclass `jsonb_ops`.
There is another opclass `jsonb_path_ops` that can be used instead.

The difference is the way they extract elements out of a jsonb.
`jsonb_ops` extracts keys and values and encodes them as `<flag_byte><value>`.
For example, `'{"abc":[123,true]}'` maps to three GIN keys: `\001abc`, `\004123`, `\003t`.
The flag bytes here indicate the types key, numeric, and boolean, respectively.

On the other hand, `jsonb_path_ops` extracts hashed paths.
Using the above example, there are two paths: `"abc" -> 123` and `"abc" -> true`.
Then, there are two GIN keys based on those paths using an internal hashing mechanism: `-1570777299`, `-1227915239`.

`jsonb_path_ops` is better suited for queries involving paths, such as the `jsonb @> jsonb` operator.
However, it doesn't support as many operators as `jsonb_ops`.
If write performance and storage aren't an issue, it may be worth creating a GIN index of each jsonb opclass so that reads can choose the faster one.
