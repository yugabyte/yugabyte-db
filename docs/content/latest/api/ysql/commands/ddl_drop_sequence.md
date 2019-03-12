---
title: DROP SEQUENCE
summary: Drop a sequence in the current schema
description: DROP SEQUENCE
menu:
  latest:
    identifier: api-ysql-commands-drop-sequence
    parent: api-ysql-commands
aliases:
  - /latest/api/ysql/commands/drop_sequence
isTocNested: true
showAsideToc: true
---

## Synopsis
The `DROP SEQUENCE` command deletes a sequence in the current schema.

## Syntax

### Diagrams
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="573" height="70" viewbox="0 0 573 70"><path class="connector" d="M0 22h5m53 0h10m87 0h30m32 0h10m64 0h20m-141 0q5 0 5 5v8q0 5 5 5h116q5 0 5-5v-8q0-5 5-5m5 0h10m118 0h30m77 0h22m-109 25q0 5 5 5h5m79 0h5q5 0 5-5m-104-25q5 0 5 5v33q0 5 5 5h89q5 0 5-5v-33q0-5 5-5m5 0h5"/><rect class="literal" x="5" y="5" width="53" height="25" rx="7"/><text class="text" x="15" y="22">DROP</text><rect class="literal" x="68" y="5" width="87" height="25" rx="7"/><text class="text" x="78" y="22">SEQUENCE</text><rect class="literal" x="185" y="5" width="32" height="25" rx="7"/><text class="text" x="195" y="22">IF</text><rect class="literal" x="227" y="5" width="64" height="25" rx="7"/><text class="text" x="237" y="22">EXISTS</text><a xlink:href="../grammar_diagrams#sequence-name"><rect class="rule" x="321" y="5" width="118" height="25"/><text class="text" x="331" y="22">sequence_name</text></a><rect class="literal" x="469" y="5" width="77" height="25" rx="7"/><text class="text" x="479" y="22">CASCADE</text><rect class="literal" x="469" y="35" width="79" height="25" rx="7"/><text class="text" x="479" y="52">RESTRICT</text></svg>

### sequence_name
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="117" height="35" viewbox="0 0 117 35"><path class="connector" d="M0 22h5m107 0h5"/><rect class="literal" x="5" y="5" width="107" height="25" rx="7"/><text class="text" x="15" y="22">&lt;Text Literal&gt;</text></svg>

### Grammar
```
drop_sequence ::= DROP SEQUENCE [ IF EXISTS ] sequence_name [ CASCADE | RESTRICT ];
```

Where

- `sequence_name` is the name of the sequence.
- `CASCADE`. Remove also all objects that depend on this sequence (for example a `DEFAULT` value in a table's column).
- `RESTRICT`. Do not remove this sequence if any object depends on it. This is the default behavior even if it's not specified.

## Semantics
- An error is raised if a sequence with that name does not exist in the current schema unless `IF EXISTS` is specified.
- An error is raised if any object depends on this sequence unless the `CASCADE` option is specified.


## Examples

Dropping a sequence that has an object depending on it, fails.

```sql
postgres=# CREATE TABLE t(k SERIAL, v INT);
```
```
CREATE TABLE
```

```sql
\d t
```
```
                           Table "public.t"
 Column |  Type   | Collation | Nullable |           Default
--------+---------+-----------+----------+------------------------------
 k      | integer |           | not null | nextval('t_k_seq'::regclass)
 v      | integer |           |          |
```

```sql
postgres=#  DROP SEQUENCE t_k_seq;
```
```
ERROR:  cannot drop sequence t_k_seq because other objects depend on it
DETAIL:  default for table t column k depends on sequence t_k_seq
HINT:  Use DROP ... CASCADE to drop the dependent objects too.
```

Dropping the sequence with the `CASCADE` option solves the problem and also deletes the default value in table `t`.


```sql
postgres=# DROP SEQUENCE t_k_seq CASCADE;
```
```
NOTICE:  drop cascades to default for table t column k
DROP SEQUENCE
```

```sql
\d t
```
```
                 Table "public.t"
 Column |  Type   | Collation | Nullable | Default
--------+---------+-----------+----------+---------
 k      | integer |           | not null |
 v      | integer |           |          |

```

## See Also
[`CREATE SEQUENCE`](../ddl_create_sequence)
[`currval()`](../currval_sequence)
[`lastval()`](../lastval_sequence)
[`nextval()`](../nextval_sequence)
[Other YSQL Statements](..)
