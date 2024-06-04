---
title: CREATE TABLE AS statement [YSQL]
headerTitle: CREATE TABLE AS
linkTitle: CREATE TABLE AS
description: Use the CREATE TABLE AS statement to create a table using the output of a subquery.
menu:
  v2.18:
    identifier: ddl_create_table_as
    parent: statements
type: docs
---

## Synopsis

Use the `CREATE TABLE AS` statement to create a table using the output of a subquery.

## Syntax

{{%ebnf%}}
  create_table_as
{{%/ebnf%}}

## Semantics

YugabyteDB may extend the syntax to allow specifying PRIMARY KEY for `CREATE TABLE AS` command.

##### *table_name*

Specify the name of the table.

##### ( *column_name* [ , ... ] )

Specify the name of a column in the new table. When not specified, column names are taken from the output column names of the query.

#### AS *query* [ WITH [ NO ] DATA ]

##### *query*

##### TEMPORARY or TEMP

Using this qualifier will create a temporary table. Temporary tables are visible only in the current client session or transaction in which they are created and are automatically dropped at the end of the session or transaction. Any indexes created on temporary tables are temporary as well. See the section [Creating and using temporary schema-objects](../../creating-and-using-temporary-schema-objects/).

## Examples

```plpgsql
CREATE TABLE sample(k1 int, k2 int, v1 int, v2 text, PRIMARY KEY (k1, k2));
```

```plpgsql
INSERT INTO sample VALUES (1, 2.0, 3, 'a'), (2, 3.0, 4, 'b'), (3, 4.0, 5, 'c');
```

```plpgsql
CREATE TABLE selective_sample AS SELECT * FROM sample WHERE k1 > 1;
```

```plpgsql
yugabyte=# SELECT * FROM selective_sample ORDER BY k1;
```

```
 k1 | k2 | v1 | v2
----+----+----+----
  2 |  3 |  4 | b
  3 |  4 |  5 | c
(2 rows)
```

## See also

- [`CREATE TABLE`](../ddl_create_table)
