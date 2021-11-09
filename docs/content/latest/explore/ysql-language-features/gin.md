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

By default, there are only a few types that can use GIN indexes:

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

Limitations:

- Multicolumn GIN indexes are not supported: this can be done in the future.
- Unique indexes are not supported: this doesn't make sense.

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
