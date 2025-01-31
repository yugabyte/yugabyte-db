---
title: DROP SEQUENCE statement [YSQL]
headerTitle: DROP SEQUENCE
linkTitle: DROP SEQUENCE
description: Use the DROP SEQUENCE statement to delete a sequence in the current schema.
menu:
  stable_api:
    identifier: ddl_drop_sequence
    parent: statements
type: docs
---

## Synopsis

Use the `DROP SEQUENCE` statement to delete a sequence in the current schema.

## Syntax

{{%ebnf%}}
  drop_sequence
{{%/ebnf%}}

## Semantics

### *sequence_name*

Specify the name of the sequence.

- An error is raised if a sequence with that name does not exist in the current schema unless `IF EXISTS` is specified.
- An error is raised if any object depends on this sequence unless the `CASCADE` option is specified.

### CASCADE

Remove also all objects that depend on this sequence (for example a `DEFAULT` value in a table's column).

#### RESTRICT

Do not remove this sequence if any object depends on it. This is the default behavior even if it's not specified.

## Examples

Dropping a sequence that has an object depending on it, fails.

```plpgsql
yugabyte=# CREATE TABLE t(k SERIAL, v INT);
```

```
CREATE TABLE
```

```plpgsql
\d t
```

```
                           Table "public.t"
 Column |  Type   | Collation | Nullable |           Default
--------+---------+-----------+----------+------------------------------
 k      | integer |           | not null | nextval('t_k_seq'::regclass)
 v      | integer |           |          |
```

```plpgsql
yugabyte=#  DROP SEQUENCE t_k_seq;
```

```
ERROR:  cannot drop sequence t_k_seq because other objects depend on it
DETAIL:  default for table t column k depends on sequence t_k_seq
HINT:  Use DROP ... CASCADE to drop the dependent objects too.
```

Dropping the sequence with the `CASCADE` option solves the problem and also deletes the default value in table `t`.

```plpgsql
yugabyte=# DROP SEQUENCE t_k_seq CASCADE;
```

```
NOTICE:  drop cascades to default for table t column k
DROP SEQUENCE
```

```plpgsql
\d t
```

```
                 Table "public.t"
 Column |  Type   | Collation | Nullable | Default
--------+---------+-----------+----------+---------
 k      | integer |           | not null |
 v      | integer |           |          |

```

## See also

- [`ALTER SEQUENCE`](../ddl_alter_sequence)
- [`CREATE SEQUENCE`](../ddl_create_sequence)
- [`currval()`](../../../exprs/func_currval)
- [`lastval()`](../../../exprs/func_lastval)
- [`nextval()`](../../../exprs/func_nextval)
- [`setval()`](../../../exprs/func_setval)
