---
title: CREATE VIEW statement [YSQL]
headerTitle: CREATE VIEW
linkTitle: CREATE VIEW
description: Use the CREATE VIEW statement to create a view in a database.
menu:
  stable:
    identifier: ddl_create_view
    parent: statements
type: docs
---

## Synopsis

Use the `CREATE VIEW` statement to create a view in a database. It defines the view name and the (select) statement defining it.

## Syntax

{{%ebnf%}}
  create_view
{{%/ebnf%}}

## Semantics

Create a view.

##### *qualified_name*

Specify the name of the view. An error is raised if view with that name already exists in the specified database (unless the `OR REPLACE` option is used).

##### *column_list*

Specify a comma-separated list of columns. If not specified, the column names are deduced from the query.

###### *select*

Specify a `SELECT` or `VALUES` statement that will provide the columns and rows of the view.

###### TEMPORARY or TEMP

Using this qualifier will create a temporary view. Temporary views are visible only in the current client session in which they are created and are automatically dropped at the end of the session. See the section [Creating and using temporary schema-objects](../../creating-and-using-temporary-schema-objects/).

## Examples

Create a sample table.

```plpgsql
CREATE TABLE sample(k1 int, k2 int, v1 int, v2 text, PRIMARY KEY (k1, k2));
```

Insert some rows.

```plpgsql
INSERT INTO sample(k1, k2, v1, v2) VALUES (1, 2.0, 3, 'a'), (2, 3.0, 4, 'b'), (3, 4.0, 5, 'c');
```

Create a view on the `sample` table.

```plpgsql
CREATE VIEW sample_view AS SELECT * FROM sample WHERE v2 != 'b' ORDER BY k1 DESC;
```

Select from the view.

```plpgsql
yugabyte=# SELECT * FROM sample_view;
```

```
 k1 | k2 | v1 | v2
----+----+----+----
  3 |  4 |  5 | c
  1 |  2 |  3 | a
(2 rows)
```

## See also

- [`SELECT`](../dml_select/)
