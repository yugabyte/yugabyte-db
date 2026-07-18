---
title: GIN indexes in YugabyteDB YSQL
headerTitle: GIN indexes
linkTitle: GIN indexes
description: Generalized inverted indexes in YSQL
headContent: Explore GIN indexes using YSQL
menu:
  v2.25:
    identifier: indexes-constraints-gin
    parent: explore-indexes-constraints-ysql
    weight: 260
type: docs
---

A GIN index is an inverted index, meaning it stores a mapping from values within a column to the rows that contain those values. This is particularly useful for data types that can contain multiple values, such as arrays, JSONB documents, and full-text search vectors. They allow efficient searches for specific elements within these composite values.

Unlike regular indexes, which store a single entry for each row, GIN indexes store multiple entries for each row, one for each component value. This allows them to efficiently handle queries that search for individual elements within a composite value.

{{<warning>}}
GIN indexes are [work in progress](#limitations). Track issue {{<issue 7850>}} for updates.
{{</warning>}}

You can create the index using `USING ybgin` to specify the index access method:

```sql
CREATE INDEX ON mytable USING ybgin (mycol);
```

The `gin` access method is reserved for temporary relations while `ybgin` is for Yugabyte-backed relations. You can still specify `USING gin`, and, if `mytable` is not a temporary table, it will be automatically substituted for `ybgin`.

{{<note>}}
GIN indexes can't be unique, so `CREATE UNIQUE INDEX` is not allowed.
{{</note>}}

## Deviation from PostgreSQL

YugabyteDB GIN indexes are slightly different from PostgreSQL GIN indexes:

- PostgreSQL uses bitmap index scan, while YugabyteDB uses index scan.
- In YugabyteDB, deletes to the index are written explicitly. This is due to storage-layer architecture differences and is also true for regular indexes.
- YugabyteDB doesn't support fast update, as it isn't practical for a distributed, log-structured database.
- YugabyteDB doesn't yet support fuzzy search limit.

## Setup

{{% explore-setup-single-new %}}

For illustration, set up some tables and insert some data.

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

## Performance

The following examples show the speed improvement of queries using GIN indexes.

GIN indexes currently support IndexScan only, not IndexOnlyScan. The difference is that IndexScan uses the results of a scan to the index for filtering on the indexed table, whereas an IndexOnlyScan need not go to the indexed table because the results from the index are sufficient. Therefore, a GIN index scan can be more costly than a sequential scan straight to the main table if the index scan does not filter out many rows.

Because cost estimates currently aren't very accurate, the more costly index scan may be chosen in some cases.

The assumption in the following examples is that you are using the GIN index in ways that take advantage of it.

1. Enable timing for future queries.

    ```sql
    \timing on
    ```

1. Test GIN index on tsvector as follows:

    ```sql
    SET enable_indexscan = off;
    SELECT * FROM vectors WHERE v @@ to_tsquery('simple', 'the');
    -- Run it once more to reduce cache bias.
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
    SELECT * FROM vectors WHERE v @@ to_tsquery('simple', 'the');
    ```

    ```output
    ...
    Time: 2.838 ms
    ```

    Notice the over 3x timing improvement when using GIN index. This is on a relatively small table: a little over 1000 rows. With more and/or bigger rows, the performance improvement should get better.

1. Use a GIN index on an int array as follows:

    ```sql
    SET enable_indexscan = off;
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
    ```

    ```output
    ...
    Time: 2.989 ms
    ```

1. Use a GIN index on with JSONB as follows:

    ```sql
    SET enable_indexscan = off;
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
    ```

    ```output
    ...
    Time: 2.115 ms
    ```

## jsonb_path_ops

By default, JSONB GIN indexes use the opclass `jsonb_ops`. Another opclass, `jsonb_path_ops`, can be used instead.

The difference is the way they extract elements out of a JSONB. `jsonb_ops` extracts keys and values and encodes them as `<flag_byte><value>`. For example, `'{"abc":[123,true]}'` maps to three GIN keys: `\001abc`, `\004123`, `\003t`. The flag bytes here indicate the types key, numeric, and boolean, respectively.

On the other hand, `jsonb_path_ops` extracts hashed paths. Using the preceding example, there are two paths: `"abc" -> 123` and `"abc" -> true`. Then, there are two GIN keys based on those paths using an internal hashing mechanism: `-1570777299`, `-1227915239`.

`jsonb_path_ops` is better suited for queries involving paths, such as the `jsonb @> jsonb` operator. However, it doesn't support as many operators as `jsonb_ops`. If write performance and storage aren't an issue, it may be worth creating a GIN index of each jsonb opclass so that reads can choose the faster one.

## Presplitting the index

By default, `ybgin` indexes use a single range-partitioned tablet. Like regular tables and indexes, it is possible to presplit a `ybgin` index to multiple tablets at specified split points. These split points are for the index, so they need to be represented in the index key format. This is simple for tsvector and array types, but it gets complicated for JSONB and text (`pg_trgm`). `jsonb_path_ops` especially should use hash partitioning as the index key is itself a hash, but hash partitioning `ybgin` indexes is currently unsupported.

```sql
CREATE INDEX NONCONCURRENTLY vectors_split_idx ON vectors USING ybgin (v) SPLIT AT VALUES (('j'), ('o'));
CREATE INDEX NONCONCURRENTLY arrays_split_idx ON arrays USING ybgin (a) SPLIT AT VALUES ((2), (3), (5));
CREATE INDEX NONCONCURRENTLY jsonbs_split_idx1 ON jsonbs USING ybgin (j) SPLIT AT VALUES ((E'\001some'), (E'\005jsonb'));
CREATE INDEX NONCONCURRENTLY jsonbs_split_idx2 ON jsonbs USING ybgin (j jsonb_path_ops) SPLIT AT VALUES ((-1000000000), (0), (1000000000));
```

## Limitations

GIN indexes are in active development, and currently have the following limitations:

- Multi-column GIN indexes are not currently supported. ([#10652](https://github.com/yugabyte/yugabyte-db/issues/10652))
- You can't yet specify `ASC`, `DESC`, or `HASH` sort methods. The default is `ASC` for prefix match purposes, so this can be relaxed in the future. ([#10653](https://github.com/yugabyte/yugabyte-db/issues/10653))
- UPDATEs may be expensive, as they're currently implemented as DELETE + INSERT.
- SELECT operations have the following limitations:
  - Scans with non-default search mode aren't currently supported.
  - Scans can't ask for more than one index key.

    For example, a request for all rows whose array contains elements 1 or 3 will fail, but one that asks for elements 1 _and_ 3 can succeed by choosing one of the elements for index scan and rechecking the entire condition later.

    However, the choice between 1 and 3 is currently unoptimized, so 3 may be chosen even though 1 corresponds to less rows.
  - Recheck is always done rather than on a case-by-case basis, meaning there can be an unnecessary performance penalty.

If a query is unsupported, you can disable index scan to avoid an error message (`SET enable_indexscan TO off;`) before the query, and re-enable it (`SET enable_indexscan TO on;`) afterwards. In the near future, cost estimates should route such queries to sequential scan.

### More than one required scan entry

Perhaps the most common issue would be "cannot use more than one required scan entry". This means that the GIN index scan internally tries to fetch more than one index key. Because this is currently not supported, it throws an ERROR.

```sql
RESET enable_indexscan;

SELECT * FROM vectors WHERE v @@ to_tsquery('simple', 'quick | lazy');
```

```sql{.nocopy}
ERROR:  unsupported ybgin index scan
DETAIL:  ybgin index method cannot use more than one required scan entry: got 2.
Time: 2.885 ms
```

To get around this you can use a UNION:

```sql
EXPLAIN
SELECT * FROM vectors WHERE v @@ to_tsquery('simple', 'quick') UNION
SELECT * FROM vectors WHERE v @@ to_tsquery('simple', 'lazy');
```

```yaml{.nocopy}
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
```

If performance doesn't matter, the universal fix is to disable index scan so that sequential scan is used. For sequential scan to be chosen, make sure that sequential scan is not also disabled.

```sql
SET enable_indexscan = off;
SELECT * FROM vectors WHERE v @@ to_tsquery('simple', 'quick | lazy');
```

### Non-default search mode

All search modes/strategies besides the default one are currently unsupported.

Many of these are best off using a sequential scan. In fact, the query planner avoids many of these types of index scans by increasing the cost, leading to sequential scan being chosen as the better alternative. Nevertheless, the following examples show some cases that hit the ERROR.

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

## Learn more

- [GIN operators](https://www.postgresql.org/docs/13/gin-builtin-opclasses.html)
