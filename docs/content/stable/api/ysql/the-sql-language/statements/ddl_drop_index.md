---
title: DROP INDEX statement [YSQL]
headerTitle: DROP INDEX
linkTitle: DROP INDEX
description: Use the DROP INDEX statement to remove one or more indexes from the database.
menu:
  stable:
    identifier: ddl_drop_index
    parent: statements
type: docs
---

## Synopsis

Use the `DROP INDEX` statement to remove an index from the database.

## Syntax

{{%ebnf%}}
  drop_index
{{%/ebnf%}}

## Semantics

#### *if_exists*

Under normal operation, an error is raised if the index does not exist.  Adding `IF EXISTS` will quietly ignore any non-existent indexes specified.

#### *index_name*

Specify the name of the index to be dropped. Objects associated with the index will be invalidated after the `DROP INDEX` statement is completed.

#### RESTRICT / CASCADE

`RESTRICT` (the default) will not drop the index if any objects depend on it.

`CASCADE` will drop any objects that transitively depend on the index.

## Example

Create a table with an index:

```plpgsql
CREATE TABLE t1(id BIGSERIAL PRIMARY KEY, v TEXT);
CREATE INDEX i1 ON t1(v);
```

Verify the index was created:

```sql
\d t1
```

```output
                            Table "public.t1"
 Column |  Type  | Collation | Nullable |            Default
--------+--------+-----------+----------+--------------------------------
 id     | bigint |           | not null | nextval('t1_id_seq'::regclass)
 v      | text   |           |          |
Indexes:
    "t1_pkey" PRIMARY KEY, lsm (id HASH)
    "i1" lsm (v HASH)
```

Drop the index:

```sql
DROP INDEX i1;
```

Use the `\d t1` meta-command to verify that the index no longer exists.

```sql
\d t1
```

```output
                            Table "public.t1"
 Column |  Type  | Collation | Nullable |            Default
--------+--------+-----------+----------+--------------------------------
 id     | bigint |           | not null | nextval('t1_id_seq'::regclass)
 v      | text   |           |          |
Indexes:
    "t1_pkey" PRIMARY KEY, lsm (id HASH)
```

## See also

- [`CREATE INDEX`](../ddl_create_index)
- [`INSERT`](../dml_insert)
- [`SELECT`](../dml_select/)
