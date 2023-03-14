# The hint table

Hints are described in a comment in a special form in the above section. This
is inconvenient in the case where queries cannot be edited. In the case hints
can be placed in a special table named `"hint_plan.hints"`. The table consists
of the following columns.

| column                | description                                                                                                                                                                                     |
| :-------------------- | :--------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------   |
| `id`                  | Unique number to identify a row for a hint. This column is filled automatically by sequence.                                                                                                    |
| `norm_query_string`   | A pattern matches to the query to be hinted. Constants in the query have to be replace with '?' as in the following example. White space is significant in the pattern.                         |
| `application_name`    | The value of `application_name` of sessions to apply the hint. The hint in the example below applies to sessions connected from psql. An empty string means sessions of any `application_name`. |
| `hints`               | Hint phrase. This must be a series of hints excluding surrounding comment marks.                                                                                                                |

The following example shows how to operate with the hint table.

```sql
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
```

The hint table is owned by the creator user and having the default privileges
at the time of creation. during `CREATE EXTENSION`. Table hints are prioritized
over comment hits.

## The types of hints

Hinting phrases are classified into six types based on what kind of object and
how they can affect planning. Scanning methods, join methods, joining order,
row number correction, parallel query, and GUC setting. You will see the lists
of hint phrases of each type in [Hint list](#hints-list).

### Hints for scan methods

Scan method hints enforce specific scanning method on the target table.
`pg_hint_plan` recognizes the target table by alias names if any. They are
`SeqScan`, `IndexScan` and so on in this kind of hint.

Scan hints are effective on ordinary tables, inheritance tables, UNLOGGED
tables, temporary tables and system catalogs. External (foreign) tables, table
functions, VALUES clause, CTEs, views and subquiries are not affected.

```sql
postgres=# /*+
postgres*#     SeqScan(t1)
postgres*#     IndexScan(t2 t2_pkey)
postgres*#  */
postgres-# SELECT * FROM table1 t1 JOIN table table2 t2 ON (t1.key = t2.key);
```

### Hints for join methods

Join method hints enforce the join methods of the joins involving specified
tables.

This can affect on joins only on ordinary tables, inheritance tables, UNLOGGED
tables, temporary tables, external (foreign) tables, system catalogs, table
functions, VALUES command results and CTEs are allowed to be in the parameter
list. But joins on views and sub query are not affected.

### Hint for joining order

This hint "Leading" enforces the order of join on two or more tables. There are
two ways of enforcing. One is enforcing specific order of joining but not
restricting direction at each join level. Another enfoces join direction
additionaly. Details are seen in the [hint list](#hints-list) table.

```sql
postgres=# /*+
postgres*#     NestLoop(t1 t2)
postgres*#     MergeJoin(t1 t2 t3)
postgres*#     Leading(t1 t2 t3)
postgres*#  */
postgres-# SELECT * FROM table1 t1
postgres-#     JOIN table table2 t2 ON (t1.key = t2.key)
postgres-#     JOIN table table3 t3 ON (t2.key = t3.key);
```

### Hint for row number correction

This hint "Rows" corrects row number misestimation of joins that comes from
restrictions of the planner.

```sql
postgres=# /*+ Rows(a b #10) */ SELECT... ; Sets rows of join result to 10
postgres=# /*+ Rows(a b +10) */ SELECT... ; Increments row number by 10
postgres=# /*+ Rows(a b -10) */ SELECT... ; Subtracts 10 from the row number.
postgres=# /*+ Rows(a b *10) */ SELECT... ; Makes the number 10 times larger.
```

### Hint for parallel plan

This hint `Parallel` enforces parallel execution configuration on scans. The
third parameter specifies the strength of enfocement. `soft` means that
`pg_hint_plan` only changes `max_parallel_worker_per_gather` and leave all
others to planner. `hard` changes other planner parameters so as to forcibly
apply the number. This can affect on ordinary tables, inheritnce parents,
unlogged tables and system catalogues. External tables, table functions, values
clause, CTEs, views and subqueries are not affected. Internal tables of a view
can be specified by its real name/alias as the target object. The following
example shows that the query is enforced differently on each table.

```sql
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
```

### GUC parameters temporarily setting

`Set` hint changes GUC parameters just while planning. GUC parameter shown in
[Query
Planning](http://www.postgresql.org/docs/current/static/runtime-config-query.html)
can have the expected effects on planning unless any other hint conflicts with
the planner method configuration parameters. The last one among hints on the
same GUC parameter makes effect. [GUC parameters for
`pg_hint_plan`](#guc-parameters-for-pg_hint_plan) are also settable by this
hint but it won't work as your expectation. See [Restrictions](#restrictions)
for details.

```sql
postgres=# /*+ Set(random_page_cost 2.0) */
postgres-# SELECT * FROM table1 t1 WHERE key = 'value';
...
```

(guc-parameters-for-pg_hint_plan)=
## GUC parameters for `pg_hint_plan`

GUC parameters below affect the behavior of `pg_hint_plan`.

|         Parameter name         |                                               Description                                                             | Default   |
|:-------------------------------|:----------------------------------------------------------------------------------------------------------------------|:----------|
| `pg_hint_plan.enable_hint`       | True enbles `pg_hint_plan`.                                                                                         | `on`      |
| `pg_hint_plan.enable_hint_table` | True enbles hinting by table. `true` or `false`.                                                                    | `off`     |
| `pg_hint_plan.parse_messages`    | Specifies the log level of hint parse error. Valid values are `error`, `warning`, `notice`, `info`, `log`, `debug`. | `INFO`    |
| `pg_hint_plan.debug_print`       | Controls debug print and verbosity. Valid vaiues are `off`, `on`, `detailed` and `verbose`.                         | `off`     |
| `pg_hint_plan.message_level`     | Specifies message level of debug print. Valid values are `error`, `warning`, `notice`, `info`, `log`, `debug`.      | `INFO`    |
