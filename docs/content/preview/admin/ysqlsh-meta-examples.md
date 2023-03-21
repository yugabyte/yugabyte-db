---
title: ysqlsh meta-command examples
headerTitle: ysqlsh meta-command examples
linkTitle: Examples
description: YSQL shell meta-command examples.
headcontent: Examples of ysqlsh meta-commands
menu:
  preview:
    identifier: ysqlsh-meta-examples
    parent: ysqlsh-meta-commands
    weight: 20
type: docs
---

The first example shows how to spread a SQL statement over several lines of input. Notice the changing prompt:

```sql
testdb=> CREATE TABLE my_table (
testdb(>  first integer not null default 0,
testdb(>  second text)
testdb-> ;
CREATE TABLE
```

Now look at the table definition again:

```sql
testdb=> \d my_table
```

```output
              Table "public.my_table"
 Column |  Type   | Collation | Nullable | Default
--------+---------+-----------+----------+---------
 first  | integer |           | not null | 0
 second | text    |           |          |
```

To change the prompt to something more interesting:

```sql
testdb=> \set PROMPT1 '%n@%m %~%R%# '
```

```output
peter@localhost testdb=>
```

Assume you've filled the table with data and want to take a look at it:

```sql
peter@localhost testdb=> SELECT * FROM my_table;
```

```output
 first | second
-------+--------
     1 | one
     2 | two
     3 | three
     4 | four
(4 rows)
```

You can display tables in different ways by using the [`\pset`](#pset-option-value) command:

```sql
peter@localhost testdb=> \pset border 2
```

```output
Border style is 2.
```

```sql
peter@localhost testdb=> SELECT * FROM my_table;
```

```output
+-------+--------+
| first | second |
+-------+--------+
|     1 | one    |
|     2 | two    |
|     3 | three  |
|     4 | four   |
+-------+--------+
(4 rows)
```

```sql
peter@localhost testdb=> \pset border 0
```

```output
Border style is 0.
```

```sql
peter@localhost testdb=> SELECT * FROM my_table;
```

```output
first second
----- ------
    1 one
    2 two
    3 three
    4 four
(4 rows)
```

```sql
peter@localhost testdb=> \pset border 1
```

```output
Border style is 1.
```

```sql
peter@localhost testdb=> \pset format unaligned
```

```output
Output format is unaligned.
```

```sql
peter@localhost testdb=> \pset fieldsep ","
```

```output
Field separator is ",".
```

```sql
peter@localhost testdb=> \pset tuples_only
```

```output
Showing only tuples.
```

```sql
peter@localhost testdb=> SELECT second, first FROM my_table;
```

```output
one,1
two,2
three,3
four,4
```

Alternatively, use the short commands:

```sql
peter@localhost testdb=> \a \t \x
```

```output
Output format is aligned.
Tuples only is off.
Expanded display is on.
```

```sql
peter@localhost testdb=> SELECT * FROM my_table;
```

```output
-[ RECORD 1 ]-
first  | 1
second | one
-[ RECORD 2 ]-
first  | 2
second | two
-[ RECORD 3 ]-
first  | 3
second | three
-[ RECORD 4 ]-
first  | 4
second | four
```

When suitable, query results can be shown in a crosstab representation with the `\crosstabview` command:

```sql
testdb=> SELECT first, second, first > 2 AS gt2 FROM my_table;
```

```output
 first | second | gt2
-------+--------+-----
     1 | one    | f
     2 | two    | f
     3 | three  | t
     4 | four   | t
(4 rows)
```

```sql
testdb=> \crosstabview first second
```

```output
 first | one | two | three | four
-------+-----+-----+-------+------
     1 | f   |     |       |
     2 |     | f   |       |
     3 |     |     | t     |
     4 |     |     |       | t
(4 rows)
```

This second example shows a multiplication table with rows sorted in reverse numerical order and columns with an independent, ascending numerical order.

```sql
testdb=> SELECT t1.first as "A", t2.first+100 AS "B", t1.first*(t2.first+100) as "AxB",
testdb(> row_number() over(order by t2.first) AS ord
testdb(> FROM my_table t1 CROSS JOIN my_table t2 ORDER BY 1 DESC
testdb(> \crosstabview "A" "B" "AxB" ord
```

```output
 A | 101 | 102 | 103 | 104
---+-----+-----+-----+-----
 4 | 404 | 408 | 412 | 416
 3 | 303 | 306 | 309 | 312
 2 | 202 | 204 | 206 | 208
 1 | 101 | 102 | 103 | 104
(4 rows)
```
