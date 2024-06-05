---
title: tablefunc extension
headerTitle: tablefunc extension
linkTitle: tablefunc
description: Using the tablefunc extension in YugabyteDB
menu:
  v2.20:
    identifier: extension-tablefunc
    parent: pg-extensions
    weight: 20
type: docs
---

The [tablefunc](https://www.postgresql.org/docs/11/tablefunc.html) module includes various functions that return tables (that is, multiple rows).

```sql
CREATE EXTENSION tablefunc;

CREATE TABLE t(k int primary key, v double precision);

PREPARE insert_k_v_pairs(int) AS
INSERT INTO t(k, v)
SELECT
  generate_series(1, $1),
  normal_rand($1, 1000.0, 10.0);
```

Test it as follows:

```sql
DELETE FROM t;

EXECUTE insert_k_v_pairs(10);

SELECT k, to_char(v, '9999.99') AS v
FROM t
ORDER BY k;
```

You'll see results similar to the following:

```output
 k  |    v
----+----------
  1 |   988.53
  2 |  1005.18
  3 |  1014.30
  4 |  1000.92
  5 |   999.51
  6 |  1000.94
  7 |  1007.45
  8 |   991.22
  9 |   987.95
 10 |   996.57
(10 rows)
```

Every time you repeat the test, you'll see different generated values for `v`.

For another example that uses `normal_rand()`, refer to [Analyzing a normal distribution with percent_rank(), cume_dist() and ntile()](../../../../api/ysql/exprs/window_functions/analyzing-a-normal-distribution/). It populates a table with a large number (say 100,000) of rows and displays the outcome as a histogram that clearly shows the familiar bell-curve shape.

`tablefunc` also provides the `connectby()`, `crosstab()`, and `crosstabN()` functions.

The `connectby()` function displays a hierarchy of the kind that you see in an _"employees"_ table with a reflexive foreign key constraint where _"manager_id"_ refers to _"employee_id"_. Each next deeper level in the tree is indented from its parent following the well-known pattern.

The `crosstab()`and  `crosstabN()` functions produce "pivot" displays. The _"N"_ in crosstabN() indicates the fact that a few, `crosstab1()`, `crosstab2()`, `crosstab3()`, are provided natively by the extension and that you can follow documented steps to create more.
