Tracking GitHub issue [7850](https://github.com/yugabyte/yugabyte-db/issues/7850)
# YSQL GIN indexes

As of v2.6 and v2.7, Yugabyte supports only one type of index access method for DocDB-backed relations: `lsm`. This simply maps columns to the primary key of the base table.

Generalized inverted indexes map elements inside container columns to the primary key of the base table. To support GIN, we add a new access method `ybgin` and implement the access method API. A lot of the work can be borrowed from the upstream PostgreSQL `gin` access method.

## opclasses

Upstream PostgreSQL comes with the following [`gin` opclasses][opclasses]:

- `tsvector_ops`: map `tsvector` to `text`
- `array_ops`: map `anyarray` to `anyelement`
- `jsonb_ops`: map `jsonb` to `text` (key or value in jsonb)
- `jsonb_path_ops`: map `jsonb` to `int4` (hash of root-to-leaf path in jsonb)

We can just duplicate these for the `ybgin` access method and reference the same opfamily.

[opclasses]: https://www.postgresql.org/docs/current/gin-builtin-opclasses.html

## amapi

### Build

`ambuild` and `yb_ambackfill` are used for `CREATE INDEX` with and without online schema changes. They both read from the base table and insert to the index table. Therefore, this is a superset of `yb_aminsert`.

### Insert

`yb_aminsert` is used instead of `aminsert` for Yugabyte. To support index writes, extract the scan entries (reuse code from upstream) for each item and write

    [scan entry, basectid]

For example, given `ybgin` index on `a int[]` and insert `pk='foo', a={1,3,5}`, write

    [1, 'foo']
    [3, 'foo']
    [5, 'foo']

to the index and

    ['foo'] -> '{1,3,5}'

to the base table.

### Select

`amgettuple` is used for `ybgin`, unlike `amgetbitmap` for `gin`. Since `gin` uses bitmap scan but `ybgin` doesn't, the implementation here is most different.

Given a query, extract the scan keys and entries (reuse code from upstream). In the simplest case, only one scan key and one scan entry is given, so the query can be like

    basectid := get_from_idx(scan entry)
    return get_from_tab(basectid)

A more complicated case is one key and two entries where both are required. We can scan using one entry and recheck the condition afterward.

For the first iteration, rechecking the condition is always done, even if it may not be necessary.

### Delete

`yb_amdelete` is used. Upstream postgres doesn't have a delete because it relies on vacuum, but we don't have that, so we need to explicitly write tombstone records. This is similar to insert.

    [scan entry, basectid] -> tombstone

### Update

An update operation does a delete then insert. This may be inefficient for `ybgin` since updating one row can mean many deletes and inserts when only a few would have sufficed.

## Null categories

For regular indexes and tables, nulls are binary: an item is either null or not. For GIN, there's more to distinguish, so they are categorized as follows:

- null key: a null element inside the container (for example, `ARRAY[null]`)
- empty item: no elements in the container (for example, `ARRAY[]`)
- null item: the container itself is `null`

DocDB does not support null categories, so we can add a new value type specifically for GIN nulls. For the first iteration, nulls won't be supported.

## Search modes

For regular indexes, there are scan flags such as `SK_SEARCHISNULL` and `SK_SEARCHARRAY`. For GIN, there are search modes:

- include empty: also match `GIN_CAT_EMPTY_ITEM`
- all: match everything but `GIN_CAT_NULL_ITEM`
- everything: match everything

## Multicolumn

Multicolumn will be disabled in the first iteration, and the design is forthcoming.

## fastupdate

The upstream PostgreSQL "fastupdate" option writes rows to a buffer (called pending list) before flushing to disk for performance purposes. We're not implementing this in the first iteration. Moreover, since in a multi-node setup the list needs to be cached on all nodes, we may never support it.

## Extensions

Some extensions extend `gin`:

- [`btree_gin`][ext-btree-gin]: add opclasses to support ordinary types (no element extraction) so that they can be pseudo-included in `gin` indexes, which don't allow included columns
- [`hstore`][ext-hstore]: add opclass to support `hstore` type
- [`intarray`][ext-intarray]: add opclass to support faster and more operators on `_int4` (`int4` array) type without nulls
- [`pg_trgm`][ext-pg-trgm]: add opclass to support trigram text search on `text` type

A `ybgin` equivalent can be created for each. Creating the system objects is simple, but the underlying implementation may need to be rewritten.

[ext-btree-gin]: https://www.postgresql.org/docs/current/btree-gin.html
[ext-hstore]: https://www.postgresql.org/docs/current/hstore.html
[ext-intarray]: https://www.postgresql.org/docs/current/intarray.html
[ext-pg-trgm]: https://www.postgresql.org/docs/current/pgtrgm.html
