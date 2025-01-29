---
title: CREATE RULE statement [YSQL]
headerTitle: CREATE RULE
linkTitle: CREATE RULE
description: Use the CREATE RULE statement to create a rule.
menu:
  v2.14:
    identifier: ddl_create_rule
    parent: statements
type: docs
---

## Synopsis

Use the `CREATE RULE` statement to create a rule.

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
  {{% includeMarkdown "../../syntax_resources/the-sql-language/statements/create_rule,rule_event,command.grammar.md" %}}
  </div>
  <div id="diagram" class="tab-pane fade" role="tabpanel" aria-labelledby="diagram-tab">
  {{% includeMarkdown "../../syntax_resources/the-sql-language/statements/create_rule,rule_event,command.diagram.md" %}}
  </div>
</div>

## Semantics

See the semantics of each option in the [PostgreSQL docs][postgresql-docs-create-rule].

## Examples

Basic example.

```plpgsql
yugabyte=# CREATE TABLE t1(a int4, b int4);
yugabyte=# CREATE TABLE t2(a int4, b int4);
yugabyte=# CREATE RULE t1_to_t2 AS ON INSERT TO t1 DO INSTEAD
             INSERT INTO t2 VALUES (new.a, new.b);
yugabyte=# INSERT INTO t1 VALUES (3, 4);
yugabyte=# SELECT * FROM t1;
```

```
 a | b
---+---
(0 rows)
```

```plpgsql
yugabyte=# SELECT * FROM t2;
```

```
 a | b
---+---
 3 | 4
(1 row)
```

## See also

- [DROP RULE](../ddl_drop_rule)

[postgresql-docs-create-rule]: https://www.postgresql.org/docs/11/sql-createrule.html
