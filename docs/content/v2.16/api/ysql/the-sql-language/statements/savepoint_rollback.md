---
title: ROLLBACK TO statement [YSQL]
headerTitle: ROLLBACK TO
linkTitle: ROLLBACK TO SAVEPOINT
description: Use the `ROLLBACK TO` statement to start a subtransaction within the current transaction.
menu:
  v2.16:
    identifier: savepoint_rollback
    parent: statements
type: docs
---

## Synopsis

Use the `ROLLBACK TO SAVEPOINT` statement to revert the state of the transaction to a previously established savepoint. This can be particularly useful to handle and unwind errors like key/index constraint violations.

## Syntax

<ul class="nav nav-tabs nav-tabs-yb">
  <li >
    <a href="#grammar" class="nav-link active" id="grammar-tab" data-toggle="tab" role="tab" aria-controls="grammar" aria-selected="true">
      <img src="/icons/file-lines.svg" alt="Grammar Icon">
      Grammar
    </a>
  </li>
  <li>
    <a href="#diagram" class="nav-link" id="diagram-tab" data-toggle="tab" role="tab" aria-controls="diagram" aria-selected="false">
      <img src="/icons/diagram.svg" alt="Diagram Icon">
      Diagram
    </a>
  </li>
</ul>

<div class="tab-content">
  <div id="grammar" class="tab-pane fade show active" role="tabpanel" aria-labelledby="grammar-tab">
  {{% includeMarkdown "../../syntax_resources/the-sql-language/statements/savepoint_rollback.grammar.md" %}}
  </div>
  <div id="diagram" class="tab-pane fade" role="tabpanel" aria-labelledby="diagram-tab">
  {{% includeMarkdown "../../syntax_resources/the-sql-language/statements/savepoint_rollback.diagram.md" %}}
  </div>
</div>

## Semantics

### *begin*

```plpgsql
ROLLBACK [ WORK | TRANSACTION ] TO [ SAVEPOINT ] name
```

#### NAME

The name of the savepoint to which you wish to roll back.

## Examples

Create a sample table and add one row to start.

```plpgsql
CREATE TABLE sample(k int PRIMARY KEY, v int);
INSERT INTO sample(k, v) VALUES (1, 2);
```

Begin a transaction and insert some rows.

```plpgsql
BEGIN TRANSACTION;
INSERT INTO sample(k, v) VALUES (3, 4);
```

Now, create a savepoint before inserting a duplicate row for `k=1`:

```plpgsql
SAVEPOINT test;
INSERT INTO sample(k, v) VALUES (1, 3);
```

You should get the following error:

```output
ERROR:  duplicate key value violates unique constraint "k_pkey"
```

Any other operations should error, since the transaction is now in a bad state:

```plpgsql
SELECT * FROM sample;
```

```output
ERROR:  current transaction is aborted, commands ignored until end of transaction block
```

However, you can roll back to our earlier savepoint and continue with the transaction without losing our earlier insert:

```plpgsql
ROLLBACK TO test;
INSERT INTO sample(k, v) VALUES (5, 6);
COMMIT;
```

If you check the rows in the table, you will see the row you inserted before the primary key violation, as well as the one you inserted after roll back:

```plpgsql
SELECT * FROM sample;
```

```output
 k  | v
----+----
  1 |  2
  3 |  4
  5 |  6
(3 rows)
```

## See also

- [`SAVEPOINT`](../savepoint_create)
- [`RELEASE`](../savepoint_release)
