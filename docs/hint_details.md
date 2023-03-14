# Details in hinting

## Syntax and placement

`pg_hint_plan` reads hints from only the first block comment and any characters
except alphabets, digits, spaces, underscores, commas and parentheses stops
parsing immediately. In the following example `HashJoin(a b)` and `SeqScan(a)`
are parsed as hints but `IndexScan(a)` and `MergeJoin(a b)` are not.

```sql
postgres=# /*+
postgres*#    HashJoin(a b)
postgres*#    SeqScan(a)
postgres*#  */
postgres-# /*+ IndexScan(a) */
postgres-# EXPLAIN SELECT /*+ MergeJoin(a b) */ *
postgres-#    FROM pgbench_branches b
postgres-#    JOIN pgbench_accounts a ON b.bid = a.bid
postgres-#   ORDER BY a.aid;
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

postgres=#
```

## Using with PL/pgSQL

`pg_hint_plan` works for queries in PL/pgSQL scripts with some restrictions.

-   Hints affect only on the following kind of queries.
    -   Queries that returns one row. (`SELECT`, `INSERT`, `UPDATE` and `DELETE`)
    -   Queries that returns multiple rows. (`RETURN QUERY`)
    -   Dynamic SQL statements. (`EXECUTE`)
    -   Cursor open. (`OPEN`)
    -   Loop over result of a query (`FOR`)
-   A hint comment have to be placed after the first word in a query as the
    following since preceding comments are not sent as a part of the query.

```sql
postgres=# CREATE FUNCTION hints_func(integer) RETURNS integer AS $$
postgres$# DECLARE
postgres$#     id  integer;
postgres$#     cnt integer;
postgres$# BEGIN
postgres$#     SELECT /*+ NoIndexScan(a) */ aid
postgres$#         INTO id FROM pgbench_accounts a WHERE aid = $1;
postgres$#     SELECT /*+ SeqScan(a) */ count(*)
postgres$#         INTO cnt FROM pgbench_accounts a;
postgres$#     RETURN id + cnt;
postgres$# END;
postgres$# $$ LANGUAGE plpgsql;
```

## Letter case in the object names

Unlike the way PostgreSQL handles object names, `pg_hint_plan` compares bare
object names in hints against the database internal object names in case
sensitive way. Therefore an object name TBL in a hint matches only "TBL" in
database and does not match any unquoted names like TBL, tbl or Tbl.

## Escaping special chacaters in object names

The objects as the hint parameter should be enclosed by double quotes if they
includes parentheses, double quotes and white spaces. The escaping rule is the
same as PostgreSQL.

## Distinction between multiple occurances of a table

`pg_hint_plan` identifies the target object by using aliases if exists. This
behavior is usable to point a specific occurance among multiple occurances of
one table.

```sql
postgres=# /*+ HashJoin(t1 t1) */
postgres-# EXPLAIN SELECT * FROM s1.t1
postgres-# JOIN public.t1 ON (s1.t1.id=public.t1.id);
INFO:  hint syntax error at or near "HashJoin(t1 t1)"
DETAIL:  Relation name "t1" is ambiguous.
...
postgres=# /*+ HashJoin(pt st) */
postgres-# EXPLAIN SELECT * FROM s1.t1 st
postgres-# JOIN public.t1 pt ON (st.id=pt.id);
                             QUERY PLAN
---------------------------------------------------------------------
 Hash Join  (cost=64.00..1112.00 rows=28800 width=8)
   Hash Cond: (st.id = pt.id)
   ->  Seq Scan on t1 st  (cost=0.00..34.00 rows=2400 width=4)
   ->  Hash  (cost=34.00..34.00 rows=2400 width=4)
         ->  Seq Scan on t1 pt  (cost=0.00..34.00 rows=2400 width=4)
```

## Underlying tables of views or rules

Hints are not applicable on views itself, but they can affect the queries
within if the object names match the object names in the expanded query on the
view. Assigning aliases to the tables in a view enables them to be manipulated
from outside the view.

```sql
postgres=# CREATE VIEW v1 AS SELECT * FROM t2;
postgres=# EXPLAIN /*+ HashJoin(t1 v1) */
          SELECT * FROM t1 JOIN v1 ON (c1.a = v1.a);
                            QUERY PLAN
------------------------------------------------------------------
 Hash Join  (cost=3.27..18181.67 rows=101 width=8)
   Hash Cond: (t1.a = t2.a)
   ->  Seq Scan on t1  (cost=0.00..14427.01 rows=1000101 width=4)
   ->  Hash  (cost=2.01..2.01 rows=101 width=4)
         ->  Seq Scan on t2  (cost=0.00..2.01 rows=101 width=4)
```

## Inheritance tables

Hints can point only the parent of an inheritance tables and the hint affect
all the inheritance. Hints simultaneously point directly to children are not in
effect.

## Hinting on multistatements

One multistatement can have exactly one hint comment and the hints affects all
of the individual statement in the multistatement. Notice that the seemingly
multistatement on the interactive interface of psql is internally a sequence of
single statements so hints affects only on the statement just following.

## VALUES expressions

`VALUES` expressions in `FROM` clause are named as `*VALUES*` internally so it
is hintable if it is the only `VALUES` in a query. Two or more `VALUES`
expressions in a query seem distinguishable looking at its explain result. But
in reality, it is merely a cosmetic and they are not distinguishable.

```sql
postgres=# /*+ MergeJoin(*VALUES*_1 *VALUES*) */
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

Subqueries in the following context occasionally can be hinted using the name
`ANY_subquery`.

    IN (SELECT ... {LIMIT | OFFSET ...} ...)
    = ANY (SELECT ... {LIMIT | OFFSET ...} ...)
    = SOME (SELECT ... {LIMIT | OFFSET ...} ...)

For these syntaxes, planner internally assigns the name to the subquery when
planning joins on tables including it, so join hints are applicable on such
joins using the implicit name as the following.

```sql
postgres=# /*+HashJoin(a1 ANY_subquery)*/
postgres=# EXPLAIN SELECT *
postgres=#    FROM pgbench_accounts a1
postgres=#   WHERE aid IN (SELECT bid FROM pgbench_accounts a2 LIMIT 10);
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

Index scan may unexpectedly performed on another index when the index specifed
in IndexOnlyScan hint cannot perform index only scan.

## Behavior of `NoIndexScan`

`NoIndexScan` hint involes `NoIndexOnlyScan`.

## Parallel hint and `UNION`

A `UNION` can run in parallel only when all underlying subqueries are
parallel-safe. Conversely enforcing parallel on any of the subqueries let a
parallel-executable `UNION` run in parallel. Meanwhile, a parallel hint with
zero workers hinhibits a scan from executed in parallel.

## Setting `pg_hint_plan` parameters by Set hints

`pg_hint_plan` parameters change the behavior of itself so some parameters
doesn't work as expected.

-   Hints to change `enable_hint`, `enable_hint_tables` are ignored even though
    they are reported as "used hints" in debug logs.
-   Setting `debug_print` and `message_level` works from midst of the
    processing of the target query.
