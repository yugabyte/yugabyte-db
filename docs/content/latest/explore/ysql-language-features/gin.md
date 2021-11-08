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
