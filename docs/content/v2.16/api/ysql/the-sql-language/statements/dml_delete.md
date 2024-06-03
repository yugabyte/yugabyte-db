---
title: DELETE statement [YSQL]
headerTitle: DELETE
linkTitle: DELETE
description: Use the DELETE statement to remove rows that meet certain conditions, and when conditions are not provided in WHERE clause, all rows are deleted.
menu:
  v2.16:
    identifier: dml_delete
    parent: statements
type: docs
---

## Synopsis

Use the `DELETE` statement to remove rows that meet certain conditions, and when conditions are not provided in WHERE clause, all rows are deleted. `DELETE` outputs the number of rows that are being deleted.

## Syntax

<ul class="nav nav-tabs nav-tabs-yb">
  <li >
    <a href="#grammar" class="nav-link active" id="grammar-tab" data-bs-toggle="tab" role="tab" aria-controls="grammar" aria-selected="true">
      <img src="/icons/file-lines.svg" alt="Grammar Icon">
      Grammar
    </a>
  </li>
  <li>
    <a href="#diagram" class="nav-link" id="diagram-tab" data-bs-toggle="tab" role="tab" aria-controls="diagram" aria-selected="false">
      <img src="/icons/diagram.svg" alt="Diagram Icon">
      Diagram
    </a>
  </li>
</ul>

<div class="tab-content">
  <div id="grammar" class="tab-pane fade show active" role="tabpanel" aria-labelledby="grammar-tab">
  {{% includeMarkdown "../../syntax_resources/the-sql-language/statements/delete,returning_clause.grammar.md" %}}
  </div>
  <div id="diagram" class="tab-pane fade" role="tabpanel" aria-labelledby="diagram-tab">
  {{% includeMarkdown "../../syntax_resources/the-sql-language/statements/delete,returning_clause.diagram.md" %}}
  </div>
</div>

{{< note title="Table inheritance is not yet supported" >}}
The [table_expr](../../../syntax_resources/grammar_diagrams/#table-expr) rule specifies syntax that is useful only when at least one other table inherits one of the tables that the `truncate` statement lists explicitly. See [this note](../ddl_alter_table#table-expr-note) for more detail. Until inheritance is supported, use a bare [table_name](../../../syntax_resources/grammar_diagrams/#table-name).
{{< /note >}}

See the section [The WITH clause and common table expressions](../../with-clause/) for more information about the semantics of the `common_table_expression` grammar rule.

## Semantics

- `USING` clause is not yet supported.

- While the `WHERE` clause allows a wide range of operators, the exact conditions used in the `WHERE` clause have significant performance considerations (especially for large datasets). For the best performance, use a `WHERE` clause that provides values for all columns in `PRIMARY KEY` or `INDEX KEY`.

### *delete*

#### WITH [ RECURSIVE ] *with_query* [ , ... ] DELETE FROM [ ONLY ] *table_name* [ * ] [ [ AS ] *alias* ] [ WHERE *condition* | WHERE CURRENT OF *cursor_name* ] [ [*returning_clause*] (#returning-clause) ]

##### *with_query*

Specify the subqueries that are referenced by name in the DELETE statement.

##### *table_name*

Specify the name of the table to be deleted.

##### *alias*

Specify the identifier of the target table within the DELETE statement. When an alias is specified, it must be used in place of the actual table in the statement.

### *returning_clause*

#### RETURNING

Specify the value to be returned. When the _output_expression_ references a column, the existing value of this column (deleted value) is used to returned.

#### *output_name*

## Examples

Create a sample table, insert a few rows, then delete one of the inserted row.

```plpgsql
CREATE TABLE sample(k1 int, k2 int, v1 int, v2 text, PRIMARY KEY (k1, k2));
```

```plpgsql
INSERT INTO sample VALUES (1, 2.0, 3, 'a'), (2, 3.0, 4, 'b'), (3, 4.0, 5, 'c');
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
DELETE FROM sample WHERE k1 = 2 AND k2 = 3;
```

```plpgsql
yugabyte=# SELECT * FROM sample ORDER BY k1;
```

```
DELETE 1
```

```
 k1 | k2 | v1 | v2
----+----+----+----
  1 |  2 |  3 | a
  3 |  4 |  5 | c
(2 rows)
```

## See also

- [`INSERT`](../dml_insert)
- [`SELECT`](../dml_select/)
- [`UPDATE`](../dml_update/)
