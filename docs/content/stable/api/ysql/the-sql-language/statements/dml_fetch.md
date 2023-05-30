---
title: FETCH statement [YSQL]
headerTitle: FETCH
linkTitle: FETCH
description: Use the FETCH statement to fetch one or several rows from a cursor.
menu:
  stable:
    identifier: dml_fetch
    parent: statements
type: docs
---

{{< warning title="YSQL currently supports only fetching rows from a cursor consecutively in the forward direction." >}}
See the subsection [Beware Issue #6514](../../../cursors/#beware-issue-6514) in the generic section [Cursors](../../../cursors/).
{{< /warning >}}

## Synopsis

Use the `FETCH` statement to fetch one or several rows from a _[cursor](../../../cursors/)_. The `FETCH` statement is used jointly with the [`DECLARE`](../dml_declare), [`MOVE`](../dml_move), and [`CLOSE`](../dml_close) statements.

## Syntax

<ul class="nav nav-tabs nav-tabs-yb">
  <li >
    <a href="#grammar" class="nav-link" id="grammar-tab" data-toggle="tab" role="tab" aria-controls="grammar" aria-selected="true">
      <img src="/icons/file-lines.svg" alt="Grammar Icon">
      Grammar
    </a>
  </li>
  <li>
    <a href="#diagram" class="nav-link active" id="diagram-tab" data-toggle="tab" role="tab" aria-controls="diagram" aria-selected="false">
      <img src="/icons/diagram.svg" alt="Diagram Icon">
      Diagram
    </a>
  </li>
</ul>

<div class="tab-content">
  <div id="grammar" class="tab-pane fade" role="tabpanel" aria-labelledby="grammar-tab">
  {{% includeMarkdown "../../syntax_resources/the-sql-language/statements/fetch,fetch_one_row,fetch_many_rows.grammar.md" %}}
  </div>
  <div id="diagram" class="tab-pane fade show active" role="tabpanel" aria-labelledby="diagram-tab">
  {{% includeMarkdown "../../syntax_resources/the-sql-language/statements/fetch,fetch_one_row,fetch_many_rows.diagram.md" %}}
  </div>
</div>

## Semantics

`FETCH` fetches one or several rows from a _cursor_.

A _cursor_ represents the current position in its result set. After declaring a cursor but before the first `FETCH` or `MOVE` execution, the current position is immediately _before_ the first row.

- The `FETCH FORWARD 0` variant fetches the row at the current position and leaves the current position unchanged.
- The `FETCH NEXT` variant, the bare `FETCH` variant, the bare `FETCH FORWARD` variant, and the `FETCH FORWARD 1` variant all fetch the row immediately after the current position and update the current position to the just-fetched row. However, if before executing one of these `FETCH` variants, the current position is the last row in the result set, then the `FETCH` runs off the end of the available rows, an empty result is returned, and the cursor position is left _after_ the last row. There are no flavors of _after the last row_. It's a uniquely defined state so that following any number of invocations of `FETCH NEXT` in this state, `FETCH PRIOR` will then fetch the last row in the result set (and update the current position to that last row.)

- The `FETCH PRIOR` variant, the bare `FETCH BACKWARD` variant, and the `FETCH BACKWARD 1` variant all fetch the row immediately before the current position and update the current position to the just-fetched row. However, if before executing one of these `FETCH` variants, the current position is the first row in the result set, then the `FETCH` runs off the start of the available rows, an empty result is returned, and the cursor position is left _before_ the first row. There are no flavors of _before the first row_. It's a uniquely defined state so that after following any number of invocations of `FETCH PRIOR` in this state, `FETCH NEXT` will then fetch the first row in the result set (and update the current position to that first row.)

- `FETCH ALL` and `FETCH FORWARD ALL` fetch all the rows from the row immediately after the current position through the last row, and the cursor position is left _after_ the last row. Of course, if when `FETCH ALL` (or `FETCH FORWARD ALL`) is invoked, the current position is the last row, or _after_ the last row, then an empty result is returned and the current position is left _after_ the last row.

- `FETCH BACKWARD ALL` fetches all the rows from the row immediately before the current position through the first row, and the cursor position is left _before_ the first row. Of course, if when `FETCH BACKWARD ALL` is invoked, the current position is the first row, or _before_ the first row, then an empty result is returned and the current position is left _before_ the first row.

- The `FETCH :n` and `FETCH FORWARD :n` variants fetch exactly _:n_ rows forwards from and including the row after the current position when this many rows are available and otherwise just as many as there are to fetch analogously to how `FETCH FORWARD ALL` behaves.
- The `FETCH BACKWARD :n` variant fetches exactly _:n_ rows backwards from and including the row before the current position when this many rows are available and otherwise just as many as there are to fetch analogously to how `FETCH BACKWARD ALL` behaves.

- The `FETCH ABSOLUTE :n` variant fetches the single row at exactly the indicated absolute position. The `FETCH RELATIVE :n` variant fetches the single row at exactly the indicated relative position (_:n_ can be negative) to the current row. For both `FETCH ABSOLUTE :n` and `FETCH RELATIVE :n`, the requested row might lie before the first row or after the last row. The outcome here is the same as it is when executing other `FETCH` variants cause the current position to fall outside the range from the first through the last row in the cursor's result set. Notice that _:n_ can be negative for both the `ABSOLUTE` and the `RELATIVE` variants.

- Each of the `FETCH FIRST` and `FETCH LAST` variants fetches, respectively, the first row or the last row. The meanings are therefore insensitive to the current cursor position, and each can be repeated time and again and will always produce the same result. 

Notice that the three variants ,`FETCH FORWARD 0`, `FETCH BACKWARD 0`, and `FETCH RELATIVE 0`, all mean the same as each other.

### *name*

A _cursor_ is identified only by an unqualified name and is visible only in the session that declares it. This determines the uniqueness scope for its name. (The name of a  _cursor_ is like that of a _prepared statement_ in this respect.)

## Simple example


```plpgsql
drop table if exists t cascade;
create table t(k, v) as
select g.val, g.val*100
from generate_series(1, 22) as g(val);

start transaction;
  declare cur scroll cursor without hold for
  select k, v
  from t
  where (k <> all (array[1, 3, 5, 7, 11, 13, 17, 19]))
  order by k;

  fetch forward     from cur;
  fetch forward     from cur;
  fetch forward     from cur;
  fetch forward   0 from cur;
  fetch forward   0 from cur;
  fetch forward all from cur;
rollback;
```

This is the result. (Blanks lines were added manually to improve the readability.)

```output
  k |  v  
----+------
  2 |  200
  4 |  400
  6 |  600

  6 |  600
  6 |  600

  8 |  800
  9 |  900
 10 | 1000
 12 | 1200
 14 | 1400
 15 | 1500
 16 | 1600
 18 | 1800
 20 | 2000
 21 | 2100
 22 | 2200
```

## See also

- [`DECLARE`](../dml_declare)
- [`MOVE`](../dml_move)
- [`CLOSE`](../dml_close)