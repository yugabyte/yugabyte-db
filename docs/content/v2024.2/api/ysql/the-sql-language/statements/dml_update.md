---
title: UPDATE statement [YSQL]
headerTitle: UPDATE
linkTitle: UPDATE
description: Use UPDATE to modify values of specified columns in all rows that meet certain conditions. When conditions are not provided in WHERE clause, all rows update.
menu:
  v2024.2_api:
    identifier: dml_update
    parent: statements
type: docs
---

## Synopsis

Use the `UPDATE` statement to modify the values of specified columns in all rows that meet certain conditions, and when conditions are not provided in WHERE clause, all rows are updated. `UPDATE` outputs the number of rows that are being updated.

## Syntax

{{%ebnf%}}
  update,
  returning_clause,
  update_item,
  column_values,
  column_names
{{%/ebnf%}}

{{< note title="Table inheritance is not yet supported" >}}
The [table_expr](../../../syntax_resources/grammar_diagrams/#table-expr) rule specifies syntax that is useful only when at least one other table inherits one of the tables that the `truncate` statement lists explicitly. See [this note](../ddl_alter_table#table-expr-note) for more detail. Until inheritance is supported, use a bare [table_name](../../../syntax_resources/grammar_diagrams/#table-name).
{{< /note >}}

See the section [The WITH clause and common table expressions](../../with-clause/) for more information about the semantics of the `common_table_expression` grammar rule.

## Semantics

Updating columns that are part of an index key including PRIMARY KEY is not yet supported.

- While the `WHERE` clause allows a wide range of operators, the exact conditions used in the where clause have significant performance considerations (especially for large datasets). For the best performance, use a `WHERE` clause that provides values for all columns in `PRIMARY KEY` or `INDEX KEY`.

### *with_query*

Specify the subqueries that are referenced by name in the `UPDATE` statement.

### *table_name*

Specify the name of the table to be updated.

### *alias*

Specify the identifier of the target table within the `UPDATE` statement. When an alias is specified, it must be used in place of the actual table in the statement.

### *column_name*

Specify the column in the table to be updated.

### *expression*

Specify the value to be assigned to a column. When the expression is referencing a column, the old value of this column is used to evaluate.

### *output_expression*

Specify the value to be returned. When the `output_expression` is referencing a column, the new value of this column (updated value) is used to evaluate.

### *subquery*

Specify the SELECT subquery statement. Its selected values will be assigned to the specified columns.

## Examples

Create a sample table, insert a few rows, then update the inserted rows.

```plpgsql
yugabyte=# CREATE TABLE sample(k1 int, k2 int, v1 int, v2 text, PRIMARY KEY (k1, k2));
```

```plpgsql
yugabyte=# INSERT INTO sample VALUES (1, 2.0, 3, 'a'), (2, 3.0, 4, 'b'), (3, 4.0, 5, 'c');
```

```plpgsql
yugabyte=# SELECT * FROM sample ORDER BY k1;
```

```
 k1 | k2 | v1 | v2
----+----+----+----
  1 |  2 |  3 | a
  2 |  3 |  4 | b
  3 |  4 |  5 | c
(3 rows)
```

```plpgsql
yugabyte=# UPDATE sample SET v1 = v1 + 3, v2 = '7' WHERE k1 = 2 AND k2 = 3;
```

```
UPDATE 1
```

```plpgsql
yugabyte=# SELECT * FROM sample ORDER BY k1;
```

```
 k1 | k2 | v1 | v2
----+----+----+----
  1 |  2 |  3 | a
  2 |  3 |  7 | 7
  3 |  4 |  5 | c
(2 rows)
```

## See also

- [`DELETE`](../dml_delete/)
- [`INSERT`](../dml_insert)
- [`SELECT`](../dml_select/)
