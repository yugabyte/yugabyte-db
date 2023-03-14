# Description

## Basic Usage

`pg_hint_plan` reads hinting phrases in a comment of special form given with
the target SQL statement. The special form is beginning by the character
sequence `"/\*+"` and ends with `"\*/"`. Hint phrases are consists of hint name
and following parameters enclosed by parentheses and delimited by spaces. Each
hinting phrases can be delimited by new lines for readability.

In the example below, hash join is selected as the joining method and scanning
`pgbench_accounts` by sequential scan method.

```sql
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
```
