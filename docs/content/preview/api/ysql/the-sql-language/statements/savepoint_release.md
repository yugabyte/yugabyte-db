---
title: RELEASE SAVEPOINT statement [YSQL]
headerTitle: RELEASE SAVEPOINT
linkTitle: RELEASE SAVEPOINT
description: Use the `RELEASE SAVEPOINT` statement to release a savepoint.
menu:
  preview:
    identifier: savepoint_release
    parent: statements
aliases:
  - /preview/api/ysql/commands/savepoint_release/
isTocNested: true
showAsideToc: true
---

## Synopsis

Use the `RELEASE SAVEPOINT` statement to release the server-side state associated with tracking a savepoint and make the named savepoint no longer accessible to [`ROLLBACK TO`](../savepoint_rollback).

## Syntax

<ul class="nav nav-tabs nav-tabs-yb">
  <li >
    <a href="#grammar" class="nav-link active" id="grammar-tab" data-toggle="tab" role="tab" aria-controls="grammar" aria-selected="true">
      <i class="fas fa-file-alt" aria-hidden="true"></i>
      Grammar
    </a>
  </li>
  <li>
    <a href="#diagram" class="nav-link" id="diagram-tab" data-toggle="tab" role="tab" aria-controls="diagram" aria-selected="false">
      <i class="fas fa-project-diagram" aria-hidden="true"></i>
      Diagram
    </a>
  </li>
</ul>

<div class="tab-content">
  <div id="grammar" class="tab-pane fade show active" role="tabpanel" aria-labelledby="grammar-tab">
  {{% includeMarkdown "../../syntax_resources/the-sql-language/statements/savepoint_release.grammar.md" %}}
  </div>
  <div id="diagram" class="tab-pane fade" role="tabpanel" aria-labelledby="diagram-tab">
  {{% includeMarkdown "../../syntax_resources/the-sql-language/statements/savepoint_release.diagram.md" %}}
  </div>
</div>

## Semantics

### *release*

```plpgsql
RELEASE [ SAVEPOINT ] name
```

#### NAME

The name of the savepoint you wish to release.

## Examples

Begin a transaction and create a savepoint.

```plpgsql
BEGIN TRANSACTION;
SAVEPOINT test;
```

Once you are done with it, release the savepoint:

```plpgsql
RELEASE test;
```

If at this point, you attempt to rollback to `test`, it will be an error:

```plpgsql
ROLLBACK TO test;
```

```output
ERROR:  savepoint "test" does not exist
```

## See also

- [`SAVEPOINT`](../savepoint_create)
- [`ROLLBACK TO`](../savepoint_rollback)
