---
title: DEALLOCATE statement [YSQL]
headerTitle: DEALLOCATE
linkTitle: DEALLOCATE
description: Use the `DEALLOCATE` statement to deallocate a previously prepared SQL statement.
menu:
  v2.14:
    identifier: perf_deallocate
    parent: statements
type: docs
---

## Synopsis

Use the `DEALLOCATE` statement to deallocate a previously prepared SQL statement.

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
  {{% includeMarkdown "../../syntax_resources/the-sql-language/statements/deallocate.grammar.md" %}}
  </div>
  <div id="diagram" class="tab-pane fade" role="tabpanel" aria-labelledby="diagram-tab">
  {{% includeMarkdown "../../syntax_resources/the-sql-language/statements/deallocate.diagram.md" %}}
  </div>
</div>

## Semantics

### *name*

Specify the name of the prepared statement to deallocate.

### ALL

Deallocate all prepared statements.

## Examples

Prepare and deallocate an insert statement.

```plpgsql
yugabyte=# CREATE TABLE sample(k1 int, k2 int, v1 int, v2 text, PRIMARY KEY (k1, k2));
```

```plpgsql
yugabyte=# PREPARE ins (bigint, double precision, int, text) AS
               INSERT INTO sample(k1, k2, v1, v2) VALUES ($1, $2, $3, $4);
```

```plpgsql
yugabyte=# DEALLOCATE ins;
```

## See also

- [`EXECUTE`](../perf_execute)
- [`PREPARE`](../perf_prepare)
