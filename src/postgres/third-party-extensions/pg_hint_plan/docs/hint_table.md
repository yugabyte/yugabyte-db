# The hint table

Hints can be specified in a comment, still this can be inconvenient in the case
where queries cannot be edited.  In the case, hints can be placed in a special
table named `"hint_plan.hints"`.  The table consists of the following columns:

| column | description |
|:-------|:------------|
| `id` | Unique number to identify a row for a hint.  <br>This column is filled automatically by sequence. |
| `norm_query_string` | A pattern matching with the query to be hinted. <br>Constants in the query are replaced by '?' as in the following example. |
| `application_name` | The value of `application_name` where sessions can apply a hint. <br>The hint in the example below applies to sessions connected from psql. <br>An empty string implies that all sessions will apply the hint. |
| `hints` | Hint phrase.  <br>This must be a series of hints excluding surrounding comment marks. |

The following example shows how to operate with the hint table.

```sql
=# INSERT INTO hint_plan.hints(norm_query_string, application_name, hints)
     VALUES (
         'EXPLAIN (COSTS false) SELECT * FROM t1 WHERE t1.id = ?;',
         '',
         'SeqScan(t1)');
INSERT 0 1
=# UPDATE hint_plan.hints
     SET hints = 'IndexScan(t1)'
     WHERE id = 1;
UPDATE 1
=# DELETE FROM hint_plan.hints WHERE id = 1;
DELETE 1
```

The hint table is owned by the extension owner and has the same default
privileges as of the time of its creation, during `CREATE EXTENSION`.
Hints in the hint table are prioritized over hints in comments.

## Types of hints

Hinting phrases are classified in multiple types based on what kind of object
and how they can affect the planner.  See [Hint list](#hint-list) for more
details.

### Hints for Scan methods

Scan method hints enforce specific scanning methods on the target table.
`pg_hint_plan` recognizes the target table by alias names if any.  These are
for example `SeqScan` or `IndexScan`.

Scan hints work on ordinary tables, inheritance tables, UNLOGGED tables,
temporary tables and system catalogs. External (foreign) tables, table
functions, VALUES clause, CTEs, views and subqueries are not affected.

```sql
=# /*+
     SeqScan(t1)
     IndexScan(t2 t2_pkey)
    */
   SELECT * FROM table1 t1 JOIN table table2 t2 ON (t1.key = t2.key);
```

### Hints for Join methods

Join method hints enforce the join methods of the joins involving the
specified tables.

This can affect joins only on ordinary tables.  Inheritance tables, UNLOGGED
tables, temporary tables, external (foreign) tables, system catalogs, table
functions, VALUES command results and CTEs are allowed to be in the parameter
list.  Joins on views and subqueries are not affected.

### Hints for Joining order

This hint, named "Leading", enforces the order of join on two or more tables.
There are two methods of enforcing it.  The first method enforces a specific
order of joining but does not restrict the direction at each join level.
The second method enforces the join direction additionaly.  See
[hint list](#hint-list) for more details.  For example:

```sql
=# /*+
     NestLoop(t1 t2)
     MergeJoin(t1 t2 t3)
     Leading(t1 t2 t3)
    */
   SELECT * FROM table1 t1
     JOIN table table2 t2 ON (t1.key = t2.key)
     JOIN table table3 t3 ON (t2.key = t3.key);
```

### Hints for Row number corrections

This hint, named "Rows", changes the row number estimation of joins that comes
from restrictions in the planner.  For example:

```sql
=# /*+ Rows(a b #10) */ SELECT... ; Sets rows of join result to 10
=# /*+ Rows(a b +10) */ SELECT... ; Increments row number by 10
=# /*+ Rows(a b -10) */ SELECT... ; Subtracts 10 from the row number.
=# /*+ Rows(a b *10) */ SELECT... ; Makes the number 10 times larger.
```

### Hints for parallel plans

This hint, named `Parallel`, enforces parallel execution configuration
on scans.  The third parameter specifies the strength of the enforcement.
`soft` means that `pg_hint_plan` only changes `max_parallel_worker_per_gather`
and leaves all the others to the planner to set.  `hard` changes other planner
parameters so as to forcibly apply the update.  This can affect ordinary
tables, inheritance parents, unlogged tables and system catalogs. External
tables, table functions, `VALUES` clauses, CTEs, views and subqueries are
not affected.  Internal tables of a view can be specified by its real
name or its alias as the target object.  The following example shows
that the query is enforced differently on each table:

```sql
=# EXPLAIN /*+ Parallel(c1 3 hard) Parallel(c2 5 hard) */
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

=# EXPLAIN /*+ Parallel(tl 5 hard) */ SELECT sum(a) FROM tl;
                                    QUERY PLAN
-----------------------------------------------------------------------------------
 Finalize Aggregate  (cost=693.02..693.03 rows=1 width=8)
   ->  Gather  (cost=693.00..693.01 rows=5 width=8)
         Workers Planned: 5
         ->  Partial Aggregate  (cost=693.00..693.01 rows=1 width=8)
               ->  Parallel Seq Scan on tl  (cost=0.00..643.00 rows=20000 width=4)
```

### GUC parameters set during planning

`Set` hints change GUC parameters just while planning.  GUC parameter shown in
[Query Planning](http://www.postgresql.org/docs/current/static/runtime-config-query.html)
can have the expected effects on planning unless an other hint conflicts with
the planner method configuration parameters.  When multiple hints change the
same GUC, the last hint takes effect.
[GUC parameters for `pg_hint_plan`](#guc-parameters-for-pg_hint_plan) are also
settable by this hint but it may not work as expected.
See [Functional limitations](#functional-limitations) for details.

```sql
=# /*+ Set(random_page_cost 2.0) */
   SELECT * FROM table1 t1 WHERE key = 'value';
...
```

(guc-parameters-for-pg_hint_plan)=
## GUC parameters for `pg_hint_plan`

The following GUC parameters affect the behavior of `pg_hint_plan`:

| Parameter name | Description | Default |
|:---------------|:------------|:--------|
| `pg_hint_plan.enable_hint` | True enables `pg_hint_plan`. | `on` |
| `pg_hint_plan.enable_hint_table` | True enables hinting by table. | `off` |
| `pg_hint_plan.parse_messages` | Specifies the log level of hint parse error.  Valid values are `error`, `warning`, `notice`, `info`, `log`, `debug`. | `INFO` |
| `pg_hint_plan.debug_print` | Controls debug print and verbosity. Valid vaiues are `off`, `on`, `detailed` and `verbose`. | `off` |
| `pg_hint_plan.message_level` | Specifies message level of debug print. Valid values are `error`, `warning`, `notice`, `info`, `log`, `debug`. | `INFO` |
