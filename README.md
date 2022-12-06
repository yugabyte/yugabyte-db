# pg\_hint\_plan 1.5

1. [Synopsis](#synopsis)
1. [Description](#description)
1. [The hint table](#the-hint-table)
1. [Installation](#installation)
1. [Unistallation](#unistallation)
1. [Details in hinting](#details-in-hinting)
1. [Errors](#errors)
1. [Functional limitations](#functional-limitations)
1. [Requirements](#requirements)
1. [Hints list](#hints-list)


## Synopsis

`pg_hint_plan` makes it possible to tweak PostgreSQL execution plans using so-called "hints" in SQL comments, like `/*+ SeqScan(a) */`.

PostgreSQL uses a cost-based optimizer, which utilizes data statistics, not static rules. The planner (optimizer) esitimates costs of each possible execution plans for a SQL statement then the execution plan with the lowest cost finally be executed. The planner does its best to select the best best execution plan, but is not always perfect, since it doesn't count some properties of the data, for example, correlation between columns.


## Description

### Basic Usage

`pg_hint_plan` reads hinting phrases in a comment of special form given with the target SQL statement. The special form is beginning by the character sequence `"/\*+"` and ends with `"\*/"`. Hint phrases are consists of hint name and following parameters enclosed by parentheses and delimited by spaces. Each hinting phrases can be delimited by new lines for readability.

In the example below, hash join is selected as the joning method and scanning `pgbench_accounts` by sequential scan method.

<pre>
postgres=# /*+
postgres*#    <b>HashJoin(a b)</b>
postgres*#    <b>SeqScan(a)</b>
postgres*#  */
postgres-# EXPLAIN SELECT *
postgres-#    FROM pgbench_branches b
postgres-#    JOIN pgbench_accounts a ON b.bid = a.bid
postgres-#   ORDER BY a.aid;
                                        QUERY PLAN
---------------------------------------------------------------------------------------
    Sort  (cost=31465.84..31715.84 rows=100000 width=197)
    Sort Key: a.aid
    ->  <b>Hash Join</b>  (cost=1.02..4016.02 rows=100000 width=197)
            Hash Cond: (a.bid = b.bid)
            ->  <b>Seq Scan on pgbench_accounts a</b>  (cost=0.00..2640.00 rows=100000 width=97)
            ->  Hash  (cost=1.01..1.01 rows=1 width=100)
                ->  Seq Scan on pgbench_branches b  (cost=0.00..1.01 rows=1 width=100)
(7 rows)

postgres=#
</pre>

## The hint table

Hints are described in a comment in a special form in the above section. This is inconvenient in the case where queries cannot be edited. In the case hints can be placed in a special table named `"hint_plan.hints"`. The table consists of the following columns.

|       column        |                                                                                         description                                                                                         |
|:--------------------|:--------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| `id`                | Unique number to identify a row for a hint. This column is filled automatically by sequence.                                                                                                |
| `norm_query_string` | A pattern matches to the query to be hinted. Constants in the query have to be replace with '?' as in the following example. White space is significant in the pattern.                     |
| `application_name`  | The value of `application_name` of sessions to apply the hint. The hint in the example below applies to sessions connected from psql. An empty string means sessions of any `application_name`. |
| `hints`             | Hint phrase. This must be a series of hints excluding surrounding comment marks.                                                                                                            |

The following example shows how to operate with the hint table.

    postgres=# INSERT INTO hint_plan.hints(norm_query_string, application_name, hints)
    postgres-#     VALUES (
    postgres(#         'EXPLAIN (COSTS false) SELECT * FROM t1 WHERE t1.id = ?;',
    postgres(#         '',
    postgres(#         'SeqScan(t1)'
    postgres(#     );
    INSERT 0 1
    postgres=# UPDATE hint_plan.hints
    postgres-#    SET hints = 'IndexScan(t1)'
    postgres-#  WHERE id = 1;
    UPDATE 1
    postgres=# DELETE FROM hint_plan.hints
    postgres-#  WHERE id = 1;
    DELETE 1
    postgres=#

The hint table is owned by the creator user and having the default privileges at the time of creation. during `CREATE EXTENSION`. Table hints are prioritized over comment hits.

### The types of hints

Hinting phrases are classified into six types based on what kind of object and how they can affect planning. Scanning methods, join methods, joining order, row number correction, parallel query, and GUC setting. You will see the lists of hint phrases of each type in [Hint list](#hints-list).

#### Hints for scan methods

Scan method hints enforce specific scanning method on the target table. `pg_hint_plan` recognizes the target table by alias names if any. They are `SeqScan`, `IndexScan` and so on in this kind of hint.

Scan hints are effective on ordinary tables, inheritance tables, UNLOGGED tables, temporary tables and system catalogs. External (foreign) tables, table functions, VALUES clause, CTEs, views and subquiries are not affected.

    postgres=# /*+
    postgres*#     SeqScan(t1)
    postgres*#     IndexScan(t2 t2_pkey)
    postgres*#  */
    postgres-# SELECT * FROM table1 t1 JOIN table table2 t2 ON (t1.key = t2.key);

#### Hints for join methods

Join method hints enforce the join methods of the joins involving specified tables.

This can affect on joins only on ordinary tables, inheritance tables, UNLOGGED tables, temporary tables, external (foreign) tables, system catalogs, table functions, VALUES command results and CTEs are allowed to be in the parameter list. But joins on views and sub query are not affected.

#### Hint for joining order

This hint "Leading" enforces the order of join on two or more tables. There are two ways of enforcing. One is enforcing specific order of joining but not restricting direction at each join level. Another enfoces join direction additionaly. Details are seen in the [hint list](#hints-list) table.

    postgres=# /*+
    postgres*#     NestLoop(t1 t2)
    postgres*#     MergeJoin(t1 t2 t3)
    postgres*#     Leading(t1 t2 t3)
    postgres*#  */
    postgres-# SELECT * FROM table1 t1
    postgres-#     JOIN table table2 t2 ON (t1.key = t2.key)
    postgres-#     JOIN table table3 t3 ON (t2.key = t3.key);

#### Hint for row number correction

This hint "Rows" corrects row number misestimation of joins that comes from restrictions of the planner.

    postgres=# /*+ Rows(a b #10) */ SELECT... ; Sets rows of join result to 10
    postgres=# /*+ Rows(a b +10) */ SELECT... ; Increments row number by 10
    postgres=# /*+ Rows(a b -10) */ SELECT... ; Subtracts 10 from the row number.
    postgres=# /*+ Rows(a b *10) */ SELECT... ; Makes the number 10 times larger.

#### Hint for parallel plan

This hint `Parallel` enforces parallel execution configuration on scans. The third parameter specifies the strength of enfocement. `soft` means that `pg_hint_plan` only changes `max_parallel_worker_per_gather` and leave all others to planner. `hard` changes other planner parameters so as to forcibly apply the number. This can affect on ordinary tables, inheritnce parents, unlogged tables and system catalogues. External tables, table functions, values clause, CTEs, views and subqueries are not affected. Internal tables of a view can be specified by its real name/alias as the target object. The following example shows that the query is enforced differently on each table.

    postgres=# explain /*+ Parallel(c1 3 hard) Parallel(c2 5 hard) */
           SELECT c2.a FROM c1 JOIN c2 ON (c1.a = c2.a);
                                      QUERY PLAN
    -------------------------------------------------------------------------------
     Hash Join  (cost=2.86..11406.38 rows=101 width=4)
       Hash Cond: (c1.a = c2.a)
       ->  Gather  (cost=0.00..7652.13 rows=1000101 width=4)
             Workers Planned: 3
             ->  Parallel Seq Scan on c1  (cost=0.00..7652.13 rows=322613 width=4)
       ->  Hash  (cost=1.59..1.59 rows=101 width=4)
             ->  Gather  (cost=0.00..1.59 rows=101 width=4)
                   Workers Planned: 5
                   ->  Parallel Seq Scan on c2  (cost=0.00..1.59 rows=59 width=4)

    postgres=# EXPLAIN /*+ Parallel(tl 5 hard) */ SELECT sum(a) FROM tl;
                                        QUERY PLAN
    -----------------------------------------------------------------------------------
     Finalize Aggregate  (cost=693.02..693.03 rows=1 width=8)
       ->  Gather  (cost=693.00..693.01 rows=5 width=8)
             Workers Planned: 5
             ->  Partial Aggregate  (cost=693.00..693.01 rows=1 width=8)
                   ->  Parallel Seq Scan on tl  (cost=0.00..643.00 rows=20000 width=4)

#### GUC parameters temporarily setting

`Set` hint changes GUC parameters just while planning. GUC parameter shown in [Query Planning](http://www.postgresql.org/docs/current/static/runtime-config-query.html) can have the expected effects on planning unless any other hint conflicts with the planner method configuration parameters. The last one among hints on the same GUC parameter makes effect. [GUC parameters for `pg_hint_plan`](#guc-parameters-for-pg_hint_plan) are also settable by this hint but it won't work as your expectation. See [Restrictions](#restrictions) for details.

    postgres=# /*+ Set(random_page_cost 2.0) */
    postgres-# SELECT * FROM table1 t1 WHERE key = 'value';
    ...

### GUC parameters for `pg_hint_plan`

GUC parameters below affect the behavior of `pg_hint_plan`.

|         Parameter name         |                                               Description                                                             | Default   |
|:-------------------------------|:----------------------------------------------------------------------------------------------------------------------|:----------|
| `pg_hint_plan.enable_hint`       | True enbles `pg_hint_plan`.                                                                                         | `on`      |
| `pg_hint_plan.enable_hint_table` | True enbles hinting by table. `true` or `false`.                                                                    | `off`     |
| `pg_hint_plan.parse_messages`    | Specifies the log level of hint parse error. Valid values are `error`, `warning`, `notice`, `info`, `log`, `debug`. | `INFO`    |
| `pg_hint_plan.debug_print`       | Controls debug print and verbosity. Valid vaiues are `off`, `on`, `detailed` and `verbose`.                         | `off`     |
| `pg_hint_plan.message_level`     | Specifies message level of debug print. Valid values are `error`, `warning`, `notice`, `info`, `log`, `debug`.      | `INFO`    |

## Installation

This section describes the installation steps.

### building binary module

Simply run `make` at the top of the source tree, then `make install` as an appropriate user. The `PATH` environment variable should be set properly for the target PostgreSQL for this process.

    $ tar xzvf pg_hint_plan-1.x.x.tar.gz
    $ cd pg_hint_plan-1.x.x
    $ make
    $ su
    # make install

### Loading `pg_hint_plan`

Basically `pg_hint_plan` does not require `CREATE EXTENSION`. Simply loading it by `LOAD` command will activate it and of course you can load it globally by setting `shared_preload_libraries` in `postgresql.conf`. Or you might be interested in `ALTER USER SET`/`ALTER DATABASE SET` for automatic loading for specific sessions.

    postgres=# LOAD 'pg_hint_plan';
    LOAD
    postgres=#

Do `CREATE EXTENSION` and `SET pg_hint_plan.enable_hint_tables TO on` if you are planning to hint tables.

## Unistallation

`make uninstall` in the top directory of source tree will uninstall the installed files if you installed from the source tree and it is left available.

    $ cd pg_hint_plan-1.x.x
    $ su
    # make uninstall

## Details in hinting

#### Syntax and placement

`pg_hint_plan` reads hints from only the first block comment and any characters except alphabets, digits, spaces, underscores, commas and parentheses stops parsing immediately. In the following example `HashJoin(a b)` and `SeqScan(a)` are parsed as hints but `IndexScan(a)` and `MergeJoin(a b)` are not.

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

#### Using with PL/pgSQL

`pg_hint_plan` works for queries in PL/pgSQL scripts with some restrictions.

-   Hints affect only on the following kind of queires.
    -   Queries that returns one row. (`SELECT`, `INSERT`, `UPDATE` and `DELETE`)
    -   Queries that returns multiple rows. (`RETURN QUERY`)
    -   Dynamic SQL statements. (`EXECUTE`)
    -   Cursor open. (`OPEN`)
    -   Loop over result of a query (`FOR`)
-   A hint comment have to be placed after the first word in a query as the following since preceding comments are not sent as a part of the query.

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

#### Letter case in the object names

Unlike the way PostgreSQL handles object names, `pg_hint_plan` compares bare object names in hints against the database internal object names in case sensitive way. Therefore an object name TBL in a hint matches only "TBL" in database and does not match any unquoted names like TBL, tbl or Tbl.

#### Escaping special chacaters in object names

The objects as the hint parameter should be enclosed by double quotes if they includes parentheses, double quotes and white spaces. The escaping rule is the same as PostgreSQL.

### Distinction between multiple occurances of a table

`pg_hint_plan` identifies the target object by using aliases if exists. This behavior is usable to point a specific occurance among multiple occurances of one table.

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

#### Underlying tables of views or rules

Hints are not applicable on views itself, but they can affect the queries within if the object names match the object names in the expanded query on the view. Assigning aliases to the tables in a view enables them to be manipulated from outside the view.

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

#### Inheritance tables

Hints can point only the parent of an inheritance tables and the hint affect all the inheritance. Hints simultaneously point directly to children are not in effect.

#### Hinting on multistatements

One multistatement can have exactly one hint comment and the hints affects all of the individual statement in the multistatement. Notice that the seemingly multistatement on the interactive interface of psql is internally a sequence of single statements so hints affects only on the statement just following.

#### VALUES expressions

`VALUES` expressions in `FROM` clause are named as `*VALUES*` internally so it is hintable if it is the only `VALUES` in a query. Two or more `VALUES` expressions in a query seem distinguishable looking at its explain result. But in reality, it is merely a cosmetic and they are not distinguishable.

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

### Subqueries

Subqueries in the following context occasionally can be hinted using the name `ANY_subquery`.

    IN (SELECT ... {LIMIT | OFFSET ...} ...)
    = ANY (SELECT ... {LIMIT | OFFSET ...} ...)
    = SOME (SELECT ... {LIMIT | OFFSET ...} ...)

For these syntaxes, planner internally assigns the name to the subquery when planning joins on tables including it, so join hints are applicable on such joins using the implicit name as the following.

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

#### Using `IndexOnlyScan` hint

Index scan may unexpectedly performed on another index when the index specifed in IndexOnlyScan hint cannot perform index only scan.

#### Behavior of `NoIndexScan`

`NoIndexScan` hint involes `NoIndexOnlyScan`.

#### Parallel hint and `UNION`

A `UNION` can run in parallel only when all underlying subqueries are parallel-safe. Conversely enforcing parallel on any of the subqueries let a parallel-executable `UNION` run in parallel. Meanwhile, a parallel hint with zero workers hinhibits a scan from executed in parallel.

#### Setting `pg_hint_plan` parameters by Set hints

`pg_hint_plan` parameters change the behavior of itself so some parameters doesn't work as expected.

-   Hints to change `enable_hint`, `enable_hint_tables` are ignored even though they are reported as "used hints" in debug logs.
-   Setting `debug_print` and `message_level` works from midst of the processing of the target query.

## Errors

`pg_hint_plan` stops parsing on any error and uses hints already parsed on the most cases. Following are the typical errors.

#### Syntax errors

Any syntactical errors or wrong hint names are reported as a syntax error. These errors are reported in the server log with the message level specified by `pg_hint_plan.message_level` if `pg_hint_plan.debug_print` is on and above.

#### Object misspecifications

Object misspecifications result in silent ignorance of the hints. This kind of error is reported as "not used hints" in the server log by the same condition as syntax errors.

#### Redundant or conflicting hints

The last hint will be active when redundant hints or hints conflicting with each other. This kind of error is reported as "duplication hints" in the server log by the same condition to syntax errors.

#### Nested comments

Hint comment cannot include another block comment within. If `pg_hint_plan` finds it, differently from other erros, it stops parsing and abandans all hints already parsed. This kind of error is reported in the same manner as other errors.

## Functional limitations

#### Influences of some of planner GUC parameters

The planner does not try to consider joining order for FROM clause entries more than `from_collapse_limit`. `pg_hint_plan` cannot affect joining order as expected for the case.

#### Hints trying to enforce unexecutable plans

Planner chooses any executable plans when the enforced plan cannot be executed.

-   `FULL OUTER JOIN` to use nested loop
-   To use indexes that does not have columns used in quals
-   To do TID scans for queries without ctid conditions

#### Queries in ECPG

ECPG removes comments in queries written as embedded SQLs so hints cannot be passed form those queries. The only exception is that `EXECUTE` command passes given string unmodifed. Please consider hint tables in the case.

#### Work with `pg_stat_statements`

`pg_stat_statements` generates a query id ignoring comments. As the result, the identical queries with different hints are summarized as the same query.

## Requirements

pg_hint_plan 1.5 requires PostgreSQL 15.


PostgreSQL versions tested

- Version 15

OS versions tested

- CentOS 8.5

See also
--------

### PostgreSQL documents

- [EXPLAIN](http://www.postgresql.org/docs/current/static/sql-explain.html)
- [SET](http://www.postgresql.org/docs/current/static/sql-set.html)
- [Server Config](http://www.postgresql.org/docs/current/static/runtime-config.html)
- [Parallel Plans](http://www.postgresql.org/docs/current/static/parallel-plans.html)


## Hints list

The available hints are listed below.

|             Group            |                                                              Format                                                             |                                                                                                                                                                        Description                                                                                                                                                                       |
|:-----------------------------|:--------------------------------------------------------------------------------------------------------------------------------|:---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| Scan method                  | `SeqScan(table)`                                                                                                                  | Forces sequential scan on the table                                                                                                                                                                                                                                                                                                                      |
|                              | `TidScan(table)`                                                                                                                  | Forces TID scan on the table.                                                                                                                                                                                                                                                                                                                            |
|                              | `IndexScan(table[ index...])`                                                                                                     | Forces index scan on the table. Restricts to specified indexes if any.                                                                                                                                                                                                                                                                                   |
|                              | `IndexOnlyScan(table[ index...])`                                                                                                 | Forces index only scan on the table. Rstricts to specfied indexes if any. Index scan may be used if index only scan is not available. Available for PostgreSQL 9.2 and later.                                                                                                                                                                            |
|                              | `BitmapScan(table[ index...])`                                                                                                    | Forces bitmap scan on the table. Restoricts to specfied indexes if any.                                                                                                                                                                                                                                                                                  |
|                              | `IndexScanRegexp(table[ POSIX Regexp...])` `IndexOnlyScanRegexp(table[ POSIX Regexp...])` `BitmapScanRegexp(table[ POSIX Regexp...])` | Forces index scan or index only scan (For PostgreSQL 9.2 and later) or bitmap scan on the table. Restricts to indexes that matches the specified POSIX regular expression pattern                                                                                                                                                                        |
|                              | `NoSeqScan(table)`                                                                                                                | Forces not to do sequential scan on the table.                                                                                                                                                                                                                                                                                                           |
|                              | `NoTidScan(table)`                                                                                                                | Forces not to do TID scan on the table.                                                                                                                                                                                                                                                                                                                  |
|                              | `NoIndexScan(table)`                                                                                                              | Forces not to do index scan and index only scan (For PostgreSQL 9.2 and later) on the table.                                                                                                                                                                                                                                                             |
|                              | `NoIndexOnlyScan(table)`                                                                                                          | Forces not to do index only scan on the table. Available for PostgreSQL 9.2 and later.                                                                                                                                                                                                                                                                   |
|                              | `NoBitmapScan(table)`                                                                                                             | Forces not to do bitmap scan on the table.                                                                                                                                                                                                                                                                                                               |
| Join method                  | `NestLoop(table table[ table...])`                                                                                                | Forces nested loop for the joins consist of the specifiled tables.                                                                                                                                                                                                                                                                                       |
|                              | `HashJoin(table table[ table...])`                                                                                                | Forces hash join for the joins consist of the specifiled tables.                                                                                                                                                                                                                                                                                         |
|                              | `MergeJoin(table table[ table...])`                                                                                               | Forces merge join for the joins consist of the specifiled tables.                                                                                                                                                                                                                                                                                        |
|                              | `NoNestLoop(table table[ table...])`                                                                                              | Forces not to do nested loop for the joins consist of the specifiled tables.                                                                                                                                                                                                                                                                             |
|                              | `NoHashJoin(table table[ table...])`                                                                                              | Forces not to do hash join for the joins consist of the specifiled tables.                                                                                                                                                                                                                                                                               |
|                              | `NoMergeJoin(table table[ table...])`                                                                                             | Forces not to do merge join for the joins consist of the specifiled tables.                                                                                                                                                                                                                                                                              |
| Join order                   | `Leading(table table[ table...])`                                                                                                 | Forces join order as specified.                                                                                                                                                                                                                                                                                                                          |
|                              | `Leading(<join pair>)`                                                                                                            | Forces join order and directions as specified. A join pair is a pair of tables and/or other join pairs enclosed by parentheses, which can make a nested structure.                                                                                                                                                                                       |
| Row number correction        | `Rows(table table[ table...] correction)`                                                                                         | Corrects row number of a result of the joins consist of the specfied tables. The available correction methods are absolute (#<n>), addition (+<n>), subtract (-<n>) and multiplication (*<n>). <n> should be a string that strtod() can read.                                                                                                            |
| Parallel query configuration | `Parallel(table <# of workers> [soft\|hard])`                                                                                     | Enforce or inhibit parallel execution of specfied table. <# of workers> is the desired number of parallel workers, where zero means inhibiting parallel execution. If the third parameter is soft (default), it just changes max_parallel_workers_per_gather and leave everything else to planner. Hard means enforcing the specified number of workers. |
| GUC                          | `Set(GUC-param value)`                                                                                                            | Set the GUC parameter to the value while planner is running.                                                                                                                                                                                                                                                                                             |

* * * * *

Copyright (c) 2012-2022, NIPPON TELEGRAPH AND TELEPHONE CORPORATION
