# Generalized inverted indexes

Regular indexes index columns.
Generalized inverted indexes (GIN indexes) index elements inside container columns.
By default,

- GIN index on tsvector indexes text elements
- GIN index on array indexes array elements
- GIN index on jsonb indexes keys/values

Use GIN indexes when queries condition on elements within these container columns.
If the query is supported by GIN, a GIN index scan may be significantly faster.

## Queries

### CREATE INDEX

Create the index using `USING ybgin` to specify the index access method.

`CREATE INDEX ON mytable USING ybgin (mycol);`

Limitations:

- Multicolumn GIN indexes are not supported: this can be done in the future.
- Unique indexes are not supported: this doesn't make sense.

### DELETE, INSERT, UPDATE

Writes to the index, whether through query or index build, currently have limitations:

- Fast update is not supported: this isn't practical for a distributed, log-structured database.

### SELECT

Since cost estimates currently aren't implemented, you can `SET enable_indexscan TO on;` to ensure that index scans are always used when possible.

Only [certain SELECTs][operators] use the GIN index.
Even among them, there are currently some limitations:

- Special scans are not supported.
  - Scans cannot include rows whose column has zero index keys.
  - Scans cannot scan the entire index.
- Scans involving more than one index key are limited.
  - Scans cannot ask for more than one index key.
    For example, a request for all rows whose array contains elements 1 or 3 will fail, but one that asks for elements 1 _and_ 3 can succeed by choosing one of the elements for index scan and rechecking the entire condition later.
    However, the choice between 1 and 3 is currently unoptimized, so 3 may be chosen even though 1 corresponds to less rows.
- Recheck is always done rather than on a case-by-case basis.
- Fuzzy search limit is not supported: it can be in the future.

[operators]: https://www.postgresql.org/docs/13/gin-builtin-opclasses.html

## Changes from upstream PostgreSQL

For those familiar with upstream PostgreSQL GIN indexes, Yugabyte GIN indexes are slightly different.

- Upstream PostgreSQL uses bitmap index scan; Yugabyte uses index scan.
- Deletes to the index are written explicitly: this is due to storage-layer architecture differences, and it's also true for regular indexes.

## Extensions

Support for extensions providing GIN support are in progress.

- [ ] btree_gin
- [ ] hstore
- [x] pg_trgm

Regarding extensibility in general, some parts are currently not extensible, like partial match, so code changes to Yugabyte, rather than just the extension, would be needed to support them.
