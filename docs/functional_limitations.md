# Functional limitations

## Influences of some of planner GUC parameters

The planner does not try to consider joining order for FROM clause entries more
than `from_collapse_limit`. `pg_hint_plan` cannot affect joining order as
expected for the case.

## Hints trying to enforce unexecutable plans

Planner chooses any executable plans when the enforced plan cannot be executed.

-   `FULL OUTER JOIN` to use nested loop
-   To use indexes that does not have columns used in quals
-   To do TID scans for queries without ctid conditions

## Queries in ECPG

ECPG removes comments in queries written as embedded SQLs so hints cannot be
passed form those queries. The only exception is that `EXECUTE` command passes
given string unmodifed. Please consider using the hint table in the case.

## Work with `pg_stat_statements`

`pg_stat_statements` generates a query id ignoring comments. As the result, the
identical queries with different hints are summarized as the same query.
