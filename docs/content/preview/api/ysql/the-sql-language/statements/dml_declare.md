---
title: DECLARE statement [YSQL]
headerTitle: DECLARE
linkTitle: DECLARE
description: Use the DECLARE statement to create a cursor. (There's no notion of "opening" a cursor in top-level SQL.)
menu:
  preview:
    identifier: dml_declare
    parent: statements
type: docs
---

{{< warning title="YSQL currently supports only fetching rows from a cursor consecutively in the forward direction." >}}
See the subsection [Beware Issue #6514](../../../cursors/#beware-issue-6514) in the generic section [Cursors](../../../cursors/).
{{< /warning >}}

## Synopsis

Use the `DECLARE` statement to create a _cursor_. See the generic section [Cursors](../../../cursors/). The `DECLARE` statement is used jointly with the [`MOVE`](../dml_move), [`FETCH`](../dml_fetch), and [`CLOSE`](../dml_close) statements.

The term _cursor_ is a SQL keyword that's used in the `DECLARE` statement (but in no other SQL statement).

There's also a PL/pgSQL API for explicit cursor management.

## Syntax

{{%ebnf%}}
  declare
{{%/ebnf%}}

## Semantics

`DECLARE` creates a _cursor_.  (There's no notion of so-called "opening" a _cursor_ in top-level SQL.) A _cursor's_ duration is limited to the duration of the session that declares it. Notice the critical maximum lifetime difference between a _holdable_, and a _non-holdable_, _cursor_. See the section [Transactional behavior — holdable and non-holdable cursors](../../../cursors/#transactional-behavior-holdable-and-non-holdable-cursors). (The [`CLOSE`](../dml_close) statement drops a _cursor_ so that you can shorten its lifetime if you want to—typically in order to save resources.)

The _pg_cursors_ catalog view lists all the currently existing _cursors_ in the current session.

### *name*

A _cursor_ is identified only by an unqualified name and is visible only in the session that declares it. This determines the uniqueness scope for its name. (The name of a  _cursor_ is like that of a _prepared statement_ in this respect.)

### *BINARY*

Specifies that the _cursor_ returns data in binary rather than in text format.

Usually, a _cursor_ is specified to return data in text format, the same as a `SELECT` statement would produce. This binary format reduces conversion effort for both the server and client at the cost of more programmer effort to deal with platform-dependent binary data formats. For example, if a query returns a value of _one_ from an integer column, you would get the string value _1_ with the default choice. But with a binary _cursor_ you would get a 4-byte field containing the internal representation of the value (in big-endian byte order).

Use binary cursors carefully. Many applications, including _ysqlsh_ (and _psql_) are not prepared to handle binary cursors and expect data to come back in the text format.

{{< note title="BINARY cursors and the 'extended' query protocol" >}}
When the client application uses the _“extended"_ query protocol to issue a `FETCH` statement, the bind protocol message specifies whether data is to be retrieved in text or binary format. This choice overrides the way that the _cursor_ is defined. It therefore isn't useful to declare a _cursor_ explicitly as `BINARY` because _any_ _cursor_ can be treated as either text or binary when you use the extended query protocol.
{{< /note >}}

### *INSENSITIVE*

This indicates that data retrieved from the _cursor_ should be unaffected by updates to the table(s) underlying the _cursor_ that occur after the _cursor_ has been declared. In PostgreSQL, and therefore in YSQL, this is the _only_ behavior. This key word therefore has no effect and is accepted only for compatibility with the SQL standard.

### *SCROLL and NO SCROLL*

`SCROLL` specifies that you can use the flavors of `FETCH` and `MOVE` to access the current row and rows in the _cursor's_ result set that lie _before_ the current row (i.e. including and before the row that `FETCH RELATIVE 0` accesses). In simple cases, the execution plan is intrinsically reversible: it allows backwards fetching just as easily as it allows forwards fetching. But not all execution plans are reversible; and when a plan is not reversible, specifying `SCROLL` implies creating a cache of the rows that the _cursor's_ _subquery_ defines at the moment that the first `MOVE` or `FETCH` statement is executed (or on demand as new rows are accessed). This implies both a performance cost and a resource consumption cost.

`NO SCROLL` specifies that the _cursor_ cannot be used to retrieve the current row or rows that lie before it.

When you specify neither `SCROLL` nor `NO SCROLL`, then allow scrolling is allowed in only _some_ cases—and this is therefore different from specifying `SCROLL` explicitly. 

{{< tip title="Always specify either SCROLL or NO SCROLL explicitly" >}}
See the [tip](../../../cursors/#specify-no-scroll-or-scroll-explicitly) in the subsection [Scrollable cursors](../../../cursors/#scrollable-cursors) on the dedicated [Cursors](../../../cursors/) page.

Choose the mode that you want explicitly to honor the requirements that you must meet. Notice that while [Issue #6514](https://github.com/yugabyte/yugabyte-db/issues/6514) remains open, your only viable choice is `NO SCROLL`.
{{< /tip >}}

### *WITHOUT HOLD and WITH HOLD*

`WITHOUT HOLD` specifies that the _cursor_ cannot be used after the transaction that created it ends (even if it ends with a successful _commit_).

`WITH HOLD` specifies that the _cursor_ can continue to be used after the transaction that created it successfully commits. (Of course, it vanishes if the transaction that created it rolls back.)

Specifying neither `WITHOUT HOLD` nor `WITH HOLD` is the same as specifying `WITHOUT HOLD`.

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
  
  select
    statement,
    is_holdable::text,
    is_scrollable::text
  from pg_cursors where name = 'cur'
  and not is_binary
  and creation_time < (transaction_timestamp() + make_interval(secs=>0.05));

  fetch all from cur;
  
  close cur;
rollback;
```

This is the result from _"select... from pg_cursors..."_:

```output
                       statement                        | is_holdable | is_scrollable 
--------------------------------------------------------+-------------+---------------
 declare cur scroll cursor without hold for            +| false       | true
   select k, v                                         +|             | 
   from t                                              +|             | 
   where (k <> all (array[1, 3, 5, 7, 11, 13, 17, 19]))+|             | 
   order by k;                                          |             |
```

And this is the result from `FETCH ALL`:

```output
 k  |  v   
----+------
  2 |  200
  4 |  400
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

- [`MOVE`](../dml_move)
- [`FETCH`](../dml_fetch)
- [`CLOSE`](../dml_close)
