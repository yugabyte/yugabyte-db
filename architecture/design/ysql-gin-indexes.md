# YSQL GIN indexes

As of v2.4/v2.6, Yugabyte supports only one type of index access method for
DocDB-backed relations: `lsm`.  This simply maps columns to the primary key of
the base table.

Generalized inverted indexes map elements inside container columns to the pk of
the base table.  To support GIN, we add a new access method `ybgin` and
implement the access method api.  A lot of the work can be borrowed from
upstream postgres's `gin` access method.

## amapi

### build

`ambuild` and `yb_ambackfill` are used for `CREATE INDEX` with and without
online schema changes.  They both read from the base table and insert to the
index table.  Therefore, this is a superset of `yb_aminsert`.  There's not much
else to it.

### insert

`yb_aminsert` is used instead of `aminsert` for Yugabyte.  To support index
writes, extract the scan entries for each item and write

    [scan entry, basectid]

### select

`amgettuple` is used for `ybgin`, unlike `amgetbitmap` for `gin`.  Since `gin`
uses bitmap scan but `ybgin` doesn't, the implementation here is most
different.

Given a query, extract the scan keys and entries.  In the simplest case, only
one scan key and one scan entry is given, so the query can be like

    basectid := get_from_idx(scan entry)
    return get_from_tab(basectid)

A more complicated one is one key and two entries where both are required.  In
this case, we can scan using one entry and recheck the condition afterward.

For now, rechecking the condition is always done, even if it may not be
necessary.

### delete

`yb_amdelete` is used.  Upstream postgres doesn't have a delete because it
relies on vacuum, but we don't have that, so we need to explicitly write
tombstone records.  This is similar to insert.

    [scan entry, basectid] -> tombstone

### update

For now, this does a delete than insert.  This may be inefficient for `ybgin`
since updating one row can mean multiple deletes and inserts when only a few
would have sufficed.

## null categories

For regular indexes and tables, nulls are binary: an item is either null or
not.  For GIN, there's more to distinguish, so they are categorized as follows:

- null key: a null element inside the container (e.g. `ARRAY[null]`)
- empty item: no elements in the container (e.g. `ARRAY[]`)
- null item: the container itself is null (i.e. `null`)

DocDB does not support null categories, so either it will have to be extended
or a workaround needs to be found to support these null categories.  We can't
just smash all the null categories into one because of search modes...

## search modes

For regular indexes, there are scan flags like `SK_SEARCHISNULL` and
`SK_SEARCHARRAY`.  For GIN, there are search modes:

- include empty: also match `GIN_CAT_EMPTY_ITEM`
- all: match everything but `GIN_CAT_NULL_ITEM`
- everything: match everything

## multicolumn

Multicolumn will be disabled in the near-term.  TODO
