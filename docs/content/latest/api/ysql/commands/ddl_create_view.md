---
title: CREATE VIEW
summary: Create a new view in a database
description: CREATE VIEW
menu:
  latest:
    identifier: api-ysql-commands-create-view
    parent: api-ysql-commands
aliases:
  - /latest/api/ysql/ddl_create_view/
isTocNested: true
showAsideToc: true
---

## Synopsis
The `CREATE VIEW` command creates a new view in a database. It defines the view name and the (select) statement defining it.  

## Syntax

### Diagrams

#### create_view

<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="755" height="49" viewbox="0 0 755 49"><path class="connector" d="M0 21h5m67 0h30m37 0h10m74 0h20m-156 0q5 0 5 5v8q0 5 5 5h131q5 0 5-5v-8q0-5 5-5m5 0h10m50 0h10m114 0h30m25 0h10m89 0h10m25 0h20m-194 0q5 0 5 5v8q0 5 5 5h169q5 0 5-5v-8q0-5 5-5m5 0h10m36 0h10m58 0h5"/><rect class="literal" x="5" y="5" width="67" height="24" rx="7"/><text class="text" x="15" y="21">CREATE</text><rect class="literal" x="102" y="5" width="37" height="24" rx="7"/><text class="text" x="112" y="21">OR</text><rect class="literal" x="149" y="5" width="74" height="24" rx="7"/><text class="text" x="159" y="21">REPLACE</text><rect class="literal" x="253" y="5" width="50" height="24" rx="7"/><text class="text" x="263" y="21">VIEW</text><a xlink:href="../grammar_diagrams#qualified-name"><rect class="rule" x="313" y="5" width="114" height="24"/><text class="text" x="323" y="21">qualified_name</text></a><rect class="literal" x="457" y="5" width="25" height="24" rx="7"/><text class="text" x="467" y="21">(</text><a xlink:href="../grammar_diagrams#column-list"><rect class="rule" x="492" y="5" width="89" height="24"/><text class="text" x="502" y="21">column_list</text></a><rect class="literal" x="591" y="5" width="25" height="24" rx="7"/><text class="text" x="601" y="21">)</text><rect class="literal" x="646" y="5" width="36" height="24" rx="7"/><text class="text" x="656" y="21">AS</text><a xlink:href="../grammar_diagrams#query"><rect class="rule" x="692" y="5" width="58" height="24"/><text class="text" x="702" y="21">query</text></a></svg>

### Grammar
```
create_view ::= CREATE [ OR REPLACE ] VIEW qualified_name [ ( column_list ) ] AS query ;
```

Where

- `qualified_name`  is the name of the view
- `column_list` is a comma-separated list of columns

## Semantics
- An error is raised if view with that name already exists in the specified database (unless the `OR REPLACE` option is used).

## Examples

Create a sample table.


```sql
postgres=# CREATE TABLE sample(k1 int, k2 int, v1 int, v2 text, PRIMARY KEY (k1, k2));
```

Insert some rows.


```sql
postgres=# INSERT INTO sample(k1, k2, v1, v2) VALUES (1, 2.0, 3, 'a'), (2, 3.0, 4, 'b'), (3, 4.0, 5, 'c');
```

Create a view on the `sample` table.


```sql
postgres=# CREATE VIEW sample_view AS SELECT * FROM sample WHERE v2 != 'b' ORDER BY k1 DESC;
```

Select from the view.


```sql
postgres=# SELECT * FROM sample_view;
```

```
 k1 | k2 | v1 | v2
----+----+----+----
  3 |  4 |  5 | c
  1 |  2 |  3 | a
(2 rows)
```

## See Also
[`SELECT`](../dml_select)
[Other YSQL Statements](..)
