---
title: The WITH clause and common table expressions (CTEs) [YSQL]
headerTitle: The WITH clause and common table expressions
linkTitle: WITH clause
description: How to use the WITH clause and common table expressions (CTEs)
image: /images/section_icons/api/ysql.png
menu:
  v2.16:
    identifier: with-clause
    parent: the-sql-language
    weight: 200
type: indexpage
showRightNav: true
---

A `WITH` clause can be used as part of a `SELECT` statement, an `INSERT` statement, an `UPDATE` statement, or a `DELETE` statement. For this reason, the functionality is described in this dedicated section.

## Introduction

The `WITH` clause lets you name one or several so-called _common table expressions_. This latter term is a term of art, and doesn't reflect the spellings of SQL keywords. It is normally abbreviated to CTE, and this acronym will be used throughout the rest of the whole of this "WITH clause" section. See the section [WITH clause—SQL syntax and semantics](./with-clause-syntax-semantics) for the formal treatment.

Briefly, a CTE names a SQL [sub]statement which may be any of these kinds: `SELECT`, `VALUES`, `INSERT`, `UPDATE`, or `DELETE`. And the `WITH` clause is legal at the start of any of these kinds of statement : `SELECT`, `INSERT`, `UPDATE`, or `DELETE`. (`VALUES` is missing from the second list.) There are two kinds of CTE: the _ordinary_ kind; and the _recursive_ kind.

The statement text that the ordinary kind of CTE names has the same syntax as the corresponding SQL statement that you issue at top level. However, a recursive CTE may be used _only_ in a `WITH` clause.

A CTE can, for example, be used to provide values for, say, an `INSERT` like this:

```plpgsql
set client_min_messages = warning;
drop table if exists t1 cascade;
create table t1(k int primary key, v int not null);

with a(k, v) as (
  select g.v, g.v*2 from generate_series(11, 20) as g(v)
  )
insert into t1
select k, v from a;

select k, v from t1 order by k;
```

This component is an example of a CTE:

```sql
a(k, v) as (select g.v, g.v*2 from generate_series(11, 20) as g(v))
```

Notice the (optional) parenthesised parameter list that follows the name, just as in the definition of a schema-level [`VIEW`](../statements/ddl_create_view).

This is the result:

```output
 k  | v
----+----
 11 | 22
 12 | 24
 13 | 26
 14 | 28
 15 | 30
 16 | 32
 17 | 34
 18 | 36
 19 | 38
 20 | 40
```

When a data-changing [sub]statement (`INSERT`, `UPDATE` , or `DELETE`) is named in a CTE, and, when it uses a `RETURNING` clause, the returned values can be used in other CTEs and in the overall statement's final [sub]statement. Here is an example.

```plpgsql
set client_min_messages = warning;
drop table if exists t2 cascade;
create table t2(k int primary key, v int not null);

with moved_rows(k, v) as (
  delete from t1
  where k > 15
  returning k, v)
insert into t2(k, v)
select k, v from moved_rows;

(
  select 't1' as table_name, k, v from t1
  union all
  select 't2' as table_name, k, v from t2
  )
order by table_name, k;
```

This is the result:

```output
 table_name | k  | v
------------+----+----
 t1         | 11 | 22
 t1         | 12 | 24
 t1         | 13 | 26
 t1         | 14 | 28
 t1         | 15 | 30
 t2         | 16 | 32
 t2         | 17 | 34
 t2         | 18 | 36
 t2         | 19 | 38
 t2         | 20 | 40
```

The central notion is that each CTE that you name in a `WITH` clause can then be invoked by its name, either in a subsequent CTE in that `WITH` clause or in the overall statement's final, main, [sub]statement. In this way, a CTE is analogous, in the declarative programming domain of SQL, to a procedure or a function in the domain of an "if-then-else" programming language, bringing the modular programming benefit of hiding names, and the implementations that they stand for, from scopes that have no interest in them.

Finally, the use of a _recursive_ CTE in a `WITH` clause enables advanced functionality, like graph analysis. For example, an _"employees"_ table often has a self-referential foreign key like _"manager_id"_ that points to the table's primary key, _"employee_id"_. `SELECT` statements that use a recursive CTE allow the reporting structure to be presented in various ways. This result shows the reporting paths of employees, in an organization with a strict hierarchical reporting scheme, in depth-first order. See the section [Pretty-printing the top-down depth-first report of paths](./emps-hierarchy/#pretty-printing-the-top-down-depth-first-report-of-paths).

```output
 emps hierarchy
----------------
 mary
   fred
     alfie
     dick
   george
   john
     alice
     bill
       joan
     edgar
   susan
```

The remainder of this section has the following subsections:

- [`WITH` clause—SQL syntax and semantics](./with-clause-syntax-semantics/)

- [The recursive CTE](./recursive-cte/)

- [Case study—Using a recursive CTE to traverse an employee hierarchy](./emps-hierarchy/)

- [Using a recursive CTE to traverse graphs of all kinds](./traversing-general-graphs/)

- [Case study—using a recursive CTE to compute Bacon Numbers for actors listed in the IMDb](./bacon-numbers/)

{{< tip title="Performance considerations" >}}

A SQL statement that uses a `WITH` clause sometimes gets a worse execution plan than the semantically equivalent statement that _doesn’t_ use a `WITH` clause. The explanation is usually that a “push-down” optimization of a restriction or projection hasn’t penetrated into the `WITH` clause’s CTE. You can usually avoid this problem by manually pushing down what you’d hope would be done automatically into your spellings of the `WITH` clause’s CTEs.

Anyway, the ordinary good practice principle holds even more here: always check the execution plans of the SQL statements that your application issues, on representative samples of data, before moving the code to the production environment.

{{< /tip >}}

{{< tip title="Downloadable WITH clause demonstration scripts" >}}

The [`recursive-cte-code-examples.zip`](https://raw.githubusercontent.com/yugabyte/yugabyte-db/master/sample/recursive-cte-code-examples/recursive-cte-code-examples.zip) file contains the `.sql` scripts that illustrate the use of the [recursive CTE](./recursive-cte/):

- [Case study—Using a recursive CTE to traverse an employee hierarchy](./emps-hierarchy/)

- [Using a recursive CTE to traverse graphs of all kinds](./traversing-general-graphs/)

- [Case study—using a recursive CTE to compute Bacon Numbers for actors listed in the IMDb](./bacon-numbers/).

All of these studies make heavy use of regular (non-recursive) CTEs. They therefore show the power of the CTE in a natural, rather than a contrived, way.

After unzipping it in a convenient new directory, you'll see a `README.txt`. It tells you how to start, in turn, a few master-scripts. Simply start each in `ysqlsh`. You can run these time and again. Each one always finishes silently. You can see the reports that they produce on the dedicated spool directories and confirm that the files that are spooled are identical to the corresponding reference copies that are delivered in the zip-file.
{{< /tip >}}
