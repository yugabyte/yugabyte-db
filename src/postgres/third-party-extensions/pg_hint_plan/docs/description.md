# Description

## Basic Usage

`pg_hint_plan` reads hinting phrases in a comment of special form given
a SQL statement.  A hint can be specified by prefixing it with the sequence
`"/\*+"` and ending it with `"\*/"`.  Hint phrases consist of hint names
and parameters enclosed by parentheses and delimited by whitespaces.  Hint
phrases can use newlines for readability.

In the example below, a hash join is selected as the join method while
doing a sequential scan on `pgbench_accounts`:

```sql
=# /*+
     <b>HashJoin(a b)</b>
     <b>SeqScan(a)</b>
    */
   EXPLAIN SELECT *
     FROM pgbench_branches b
     JOIN pgbench_accounts a ON b.bid = a.bid
     ORDER BY a.aid;
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
```
