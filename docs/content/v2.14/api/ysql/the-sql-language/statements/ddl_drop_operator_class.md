---
title: DROP OPERATOR CLASS statement [YSQL]
headerTitle: DROP OPERATOR CLASS
linkTitle: DROP OPERATOR CLASS
description: Use the DROP OPERATOR CLASS statement to remove an operator class.
menu:
  v2.14:
    identifier: ddl_drop_operator_class
    parent: statements
type: docs
---

## Synopsis

Use the `DROP OPERATOR CLASS` statement to remove an operator class.

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
  {{% includeMarkdown "../../syntax_resources/the-sql-language/statements/drop_operator_class.grammar.md" %}}
  </div>
  <div id="diagram" class="tab-pane fade" role="tabpanel" aria-labelledby="diagram-tab">
  {{% includeMarkdown "../../syntax_resources/the-sql-language/statements/drop_operator_class.diagram.md" %}}
  </div>
</div>

## Semantics

See the semantics of each option in the [PostgreSQL docs][postgresql-docs-drop-operator-class].

## Examples

Basic example.

```plpgsql
yugabyte=# CREATE OPERATOR CLASS my_op_class
           FOR TYPE int4
           USING btree AS
           OPERATOR 1 <,
           OPERATOR 2 <=;
yugabyte=# DROP OPERATOR CLASS my_op_class USING btree;
```

## See also

- [CREATE OPERATOR CLASS](../ddl_create_operator_class)

[postgresql-docs-drop-operator-class]: https://www.postgresql.org/docs/11/sql-dropopclass.html
