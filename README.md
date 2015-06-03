HypoPG
=======

This software is EXPERIMENTAL and therefore NOT production ready. Use at your
own risk.

HypoPG is a PostgreSQL extension adding support for hypothetical indexes.

This project is sponsored by [Dalibo](http://dalibo.com).

Installation
------------

- Compatible with PostgreSQL 9.2 and above
- Needs PostgreSQL header files
- sudo make install
- create extension hypopg

Usage
-----

NOTE: The hypothetical indexes are contained in a single backend. Therefeore,
if you add multiple hypothetical indexes, concurrent connexions doing
EXPLAIN won't be bother by your indexes.

Assuming a simple test case:

```
rjuju=# CREATE TABLE hypo AS SELECT id, 'line ' || id AS val FROM generate_series(1,10000) id;
rjuju=# EXPLAIN SELECT * FROM hypo WHERE id = 1;
                      QUERY PLAN
-------------------------------------------------------
 Seq Scan on hypo  (cost=0.00..180.00 rows=1 width=13)
   Filter: (id = 1)
(2 rows)

```

The easiest way to create an hypothetical index is to use the
**hypopg_create_index** functions is a regular CREATE INDEX statement.

For instance:

```
rjuju=# SELECT * FROM hypopg_create_index('CREATE INDEX ON hypo (id)');
```

NOTE: Some information of the CREATE INDEX statement will be ignored, such as
the index name if provided. Some of ignored informations will be handled in
future release.

You can check the available hypothetical indexes in your own backend:

```
rjuju=# SELECT * FROM hypopg();
  oid   |       indexname        | relid  | amid
--------+------------------------+--------+------
 141869 | idx_hypo_btree_hypo_id | 141863 |  403

```

And now, let's see if your previous EXPLAIN statement would use such an index:

```
rjuju=# EXPLAIN SELECT * FROM hypo WHERE id = 1;
                                     QUERY PLAN
------------------------------------------------------------------------------------
 Index Scan using idx_hypo_btree_hypo_id on hypo  (cost=0.29..8.30 rows=1 width=13)
   Index Cond: (id = 1)
(2 rows)

```

Of course, only EXPLAIN without analyze will use hypothetical indexes:

```
rjuju=# EXPLAIN ANALYZE SELECT * FROM hypo WHERE id = 1;
                                           QUERY PLAN
-------------------------------------------------------------------------------------------------
 Seq Scan on hypo  (cost=0.00..180.00 rows=1 width=13) (actual time=0.036..6.072 rows=1 loops=1)
   Filter: (id = 1)
   Rows Removed by Filter: 9999
 Planning time: 0.109 ms
 Execution time: 6.113 ms
(5 rows)

To remove your backend's hypothetical indexes, you can use the function
**hypopg_drop_index(indexid)** with the OID that **hypopg()** function returns,
or just close your current connection.
