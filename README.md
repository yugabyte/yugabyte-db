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
- decompress the tarball
- sudo make install
- In every needed database: CREATE EXTENSION hypopg;

Usage
-----

NOTE: The hypothetical indexes are contained in a single backend. Therefeore,
if you add multiple hypothetical indexes, concurrent connexions doing
EXPLAIN won't be bothered by your hypothetical indexes.

Assuming a simple test case:

    rjuju=# CREATE TABLE hypo AS SELECT id, 'line ' || id AS val FROM generate_series(1,10000) id;
    rjuju=# EXPLAIN SELECT * FROM hypo WHERE id = 1;
                          QUERY PLAN
    -------------------------------------------------------
     Seq Scan on hypo  (cost=0.00..180.00 rows=1 width=13)
       Filter: (id = 1)
    (2 rows)


The easiest way to create an hypothetical index is to use the
**hypopg_create_index** functions with a regular CREATE INDEX statement as arg.

For instance:

    rjuju=# SELECT * FROM hypopg_create_index('CREATE INDEX ON hypo (id)');

NOTE: Some information of the CREATE INDEX statement will be ignored, such as
the index name if provided. Some of ignored informations will be handled in
future release.

You can check the available hypothetical indexes in your own backend:

    rjuju=# SELECT * FROM hypopg_list_indexes();
     indexrelid |                 indexname                 | nspname | relname | amname
     -----------+-------------------------------------------+---------+---------+--------
         205101 | <41072>btree_hypo_id                    | public  | hypo    | btree


If you need more technical informations on the hypothetical indexes, the
hypopg() function will return the hypothetical indexes in a similar way as
pg_index system catalog.

And now, let's see if your previous EXPLAIN statement would use such an index:

    rjuju=# EXPLAIN SELECT * FROM hypo WHERE id = 1;
                                         QUERY PLAN
    ------------------------------------------------------------------------------------
     Index Scan using <41072>hypo_btree_hypo_id on hypo  (cost=0.29..8.30 rows=1 width=13)
       Index Cond: (id = 1)
    (2 rows)


Of course, only EXPLAIN without analyze will use hypothetical indexes:

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
call hypopg_reset() to remove all at once or just close your current connection.
