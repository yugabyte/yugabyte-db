# Details in hinting

## Syntax and placement

`pg_hint_plan` reads hints from only the first block comment and stops parsing
from any characters except alphabetical characters, digits, spaces,
underscores, commas and parentheses.  In the following example,
`HashJoin(a b)` and `SeqScan(a)` are parsed as hints, but `IndexScan(a)` and
`MergeJoin(a b)` are not:

```sql
=# /*+
     HashJoin(a b)
     SeqScan(a)
    */
   /*+ IndexScan(a) */
   EXPLAIN SELECT /*+ MergeJoin(a b) */ *
     FROM pgbench_branches b
     JOIN pgbench_accounts a ON b.bid = a.bid
     ORDER BY a.aid;
                                      QUERY PLAN
---------------------------------------------------------------------------------------
 Sort  (cost=31465.84..31715.84 rows=100000 width=197)
   Sort Key: a.aid
   ->  Hash Join  (cost=1.02..4016.02 rows=100000 width=197)
         Hash Cond: (a.bid = b.bid)
         ->  Seq Scan on pgbench_accounts a  (cost=0.00..2640.00 rows=100000 width=97)
         ->  Hash  (cost=1.01..1.01 rows=1 width=100)
               ->  Seq Scan on pgbench_branches b  (cost=0.00..1.01 rows=1 width=100)
(7 rows)
```

## Using with PL/pgSQL

`pg_hint_plan` works for queries in PL/pgSQL scripts with some restrictions.

-   Hints affect only on the following kind of queries:
    -   Queries that returns one row (`SELECT`, `INSERT`, `UPDATE` and `DELETE`)
    -   Queries that returns multiple rows (`RETURN QUERY`)
    -   Dynamic SQL statements (`EXECUTE`)
    -   Cursor open (`OPEN`)
    -   Loop over result of a query (`FOR`)
-   A hint comment has to be placed after the first word in a query as
    preceding comments are not sent as a part of this query.


```plpgsql
=# CREATE FUNCTION hints_func(integer) RETURNS integer AS $$
   DECLARE
     id  integer;
     cnt integer;
   BEGIN
     SELECT /*+ NoIndexScan(a) */ aid
       INTO id FROM pgbench_accounts a WHERE aid = $1;
     SELECT /*+ SeqScan(a) */ count(*)
       INTO cnt FROM pgbench_accounts a;
     RETURN id + cnt;
   END;
   $$ LANGUAGE plpgsql;
```

## Upper and lower case handling in object names

Unlike the way PostgreSQL handles object names, `pg_hint_plan` compares bare
object names in hints against the database internal object names in a
case-sensitive manner.  Therefore, an object name TBL in a hint matches
only "TBL" in the database and does not match any unquoted names like
TBL, tbl or Tbl.

## Escaping special characters in object names

The objects defined in a hint's parameter can use double quotes if they
includes parentheses, double quotes and white spaces.  The escaping rules are
the same as PostgreSQL.

## Distinction between multiple occurences of a table

`pg_hint_plan` identifies the target object by using aliases if any.  This
behavior is useful to point to a specific occurrence among multiple
occurrences of one table.

```sql
=# /*+ HashJoin(t1 t1) */
   EXPLAIN SELECT * FROM s1.t1
     JOIN public.t1 ON (s1.t1.id=public.t1.id);
INFO:  hint syntax error at or near "HashJoin(t1 t1)"
DETAIL:  Relation name "t1" is ambiguous.
...
=# /*+ HashJoin(pt st) */
   EXPLAIN SELECT * FROM s1.t1 st
     JOIN public.t1 pt ON (st.id=pt.id);
                             QUERY PLAN
---------------------------------------------------------------------
 Hash Join  (cost=64.00..1112.00 rows=28800 width=8)
   Hash Cond: (st.id = pt.id)
   ->  Seq Scan on t1 st  (cost=0.00..34.00 rows=2400 width=4)
   ->  Hash  (cost=34.00..34.00 rows=2400 width=4)
         ->  Seq Scan on t1 pt  (cost=0.00..34.00 rows=2400 width=4)
```

## Underlying tables of views or rules

Hints are not applicable on views, but they can affect the queries within the
view if the object names match the names in the expanded query on the view.
Assigning aliases to the tables in a view enables them to be manipulated
from outside the view.

```sql
=# CREATE VIEW v1 AS SELECT * FROM t2;
=# EXPLAIN /*+ HashJoin(t1 v1) */
          SELECT * FROM t1 JOIN v1 ON (c1.a = v1.a);
                            QUERY PLAN
------------------------------------------------------------------
 Hash Join  (cost=3.27..18181.67 rows=101 width=8)
   Hash Cond: (t1.a = t2.a)
   ->  Seq Scan on t1  (cost=0.00..14427.01 rows=1000101 width=4)
   ->  Hash  (cost=2.01..2.01 rows=101 width=4)
         ->  Seq Scan on t2  (cost=0.00..2.01 rows=101 width=4)
```

## Inheritance

Hints can only point to the parent of an inheritance tree and the hint saffect
all the tables in an inheritance tree.  Hints pointing directly to inherited
children have no effect.

## Hints in multistatements

One multistatement can have exactly one hint comment and the hint affects all
of the individual statements in the multistatement.

## VALUES expressions

`VALUES` expressions in `FROM` clause are named as `*VALUES*` internally these
can be hinted if it is the only `VALUES` of a query.  Two or more `VALUES`
expressions in a query cannot be distinguised by looking at an `EXPLAIN` result,
resulting in ambiguous results:

```sql
=# /*+ MergeJoin(*VALUES*_1 *VALUES*) */
   EXPLAIN SELECT * FROM (VALUES (1, 1), (2, 2)) v (a, b)
     JOIN (VALUES (1, 5), (2, 8), (3, 4)) w (a, c) ON v.a = w.a;
INFO:  pg_hint_plan: hint syntax error at or near "MergeJoin(*VALUES*_1 *VALUES*) "
DETAIL:  Relation name "*VALUES*" is ambiguous.
                               QUERY PLAN
-------------------------------------------------------------------------
 Hash Join  (cost=0.05..0.12 rows=2 width=16)
   Hash Cond: ("*VALUES*_1".column1 = "*VALUES*".column1)
   ->  Values Scan on "*VALUES*_1"  (cost=0.00..0.04 rows=3 width=8)
   ->  Hash  (cost=0.03..0.03 rows=2 width=8)
         ->  Values Scan on "*VALUES*"  (cost=0.00..0.03 rows=2 width=8)
```

## Subqueries

Subqueries context can be occasionally hinted using the name `ANY_subquery`:

    IN (SELECT ... {LIMIT | OFFSET ...} ...)
    = ANY (SELECT ... {LIMIT | OFFSET ...} ...)
    = SOME (SELECT ... {LIMIT | OFFSET ...} ...)

For these syntaxes, the planner internally assigns the name to the subquery
when planning joins on tables including it, so join hints are applicable on
such joins using the implicit name.  For example:

```sql
=# /*+HashJoin(a1 ANY_subquery)*/
   EXPLAIN SELECT *
     FROM pgbench_accounts a1
   WHERE aid IN (SELECT bid FROM pgbench_accounts a2 LIMIT 10);
                                         QUERY PLAN

---------------------------------------------------------------------------------------------
 Hash Semi Join  (cost=0.49..2903.00 rows=1 width=97)
   Hash Cond: (a1.aid = a2.bid)
   ->  Seq Scan on pgbench_accounts a1  (cost=0.00..2640.00 rows=100000 width=97)
   ->  Hash  (cost=0.36..0.36 rows=10 width=4)
         ->  Limit  (cost=0.00..0.26 rows=10 width=4)
               ->  Seq Scan on pgbench_accounts a2  (cost=0.00..2640.00 rows=100000 width=4)
```

## Using `IndexOnlyScan` hint

Index scan may be unexpectedly performed on another index when the index
specified in IndexOnlyScan hint cannot perform an index only scan.

## About `NoIndexScan`

A `NoIndexScan` hint implies `NoIndexOnlyScan`.

## Parallel hints and `UNION`

A `UNION` can run in parallel only when all underlying subqueries are
parallel-safe.  Hence, enforcing parallel on any of the subqueries will let a
parallel-executable `UNION` run in parallel.  Meanwhile, a parallel hint with
zero workers prevents a scan from being executed in parallel.

## Setting `pg_hint_plan` parameters by Set hints

`pg_hint_plan` parameters influence its own behavior so some parameters
will not work as one could expect:

-   Hints to change `enable_hint`, `enable_hint_tables` are ignored even though
    they are reported as "used hints" in debug logs.
-   Setting `debug_print` and `message_level` in the middle of query processing.
