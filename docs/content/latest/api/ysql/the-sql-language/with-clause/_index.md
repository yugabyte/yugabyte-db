---
title: The WITH clause [YSQL]
headerTitle: The Common Table Expression (WITH clause)
linkTitle: WITH clause
description: How to use the WITH clause a.k.a. Common Table Expression
image: /images/section_icons/api/ysql.png
menu:
  latest:
    identifier: with-clause
    parent: the-sql-language
    weight: 200
aliases:
  - /latest/api/ysql/with-clause/
isTocNested: true
showAsideToc: true
---

The `WITH` clause (sometimes known as the _common table expression_) can be used as part of a `SELECT` statement, an `INSERT` statement, an `UPDATE` statement, or a `DELETE` statement. For this reason, the functionality is described in a dedicated section.

For example, a `WITH` clause can be used to provide values for, say, an `INSERT` like this:

```
with a as (select g.v as k, g.v*2 as v from generate_series(11, 20) as g(v))
insert into t
select k, v from a;
```
Moreover, data-changing sub-statements (`INSERT`, `UPDATE` , and `DELETE`) can be used in a `WITH` clause and, when these use a `RETURNING` clause, the returned values can be used in another data-changing statement like this:

```
with moved_rows as (
  delete from t1
  where v > 50
  returning k, v)
insert into t2(k, v)
select k, v from moved_rows;
```

Finally, the _recursive_ with clause enables graph analysis. For example, an _"employees"_ table often has a self-referential foreign key like _"manger_id"_ that points to the table's primary key, _"employee_id"_. `SELECT` statements that use a recursive `WITH` clause allows the reporting structure to be presented in various ways. For example, this result sorts the employees by their level in the hierarchy:

```
 lvl | mgr_name | name  
-----+----------+-------
   0 | -        | mary
   1 | mary     | fred
   1 | mary     | john
   1 | mary     | susan
   2 | fred     | dick
   2 | fred     | doris
   2 | john     | alice
   2 | john     | bill
   3 | bill     | joan
```

And this result shows the reporting paths, sorted in depth-first order:

```
     Reporting path     
------------------------
 mary
 mary->fred
 mary->fred->dick
 mary->fred->doris
 mary->john
 mary->john->alice
 mary->john->bill
 mary->john->bill->joan
 mary->susan
```

{{< note title="Under construction." >}}

A future version of this section will explain everything and provide working code examples.

{{< /note >}}

