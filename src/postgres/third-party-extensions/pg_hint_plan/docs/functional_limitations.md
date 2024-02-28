(functional-limitations)=

# Functional limitations

## Influence of planner GUC parameters

The planner does not try to consider joining order for FROM clause entries
more than `from_collapse_limit`. `pg_hint_plan` cannot affect the joining
order in this case.

## Hints trying to enforce non-executable plans

Planner chooses any executable plans when the enforced plan cannot be
executed:

-   `FULL OUTER JOIN` to use nested loop.
-   Use of indexes that do not have columns used in quals.
-   TID scans for queries without ctid conditions.

## Queries in ECPG

ECPG removes comments in queries written as embedded SQLs so hints cannot
be passed to it.  The only exception `EXECUTE`, that passes the query string
to the server as-is.  The hint table can be used in the case.

## `pg_stat_statements`

`pg_stat_statements` generates a query ID, ignoring comments.  Hence,
queries with different hints, still written the same way, may compute the
same query ID.
