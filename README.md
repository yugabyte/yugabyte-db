HypoPG
=======

HypoPG is a PostgreSQL extension adding support for hypothetical indexes.

An hypothetical -- or virtual -- index is an index that doesn't really exists, and
thus doesn't cost CPU, disk or any resource to create.  They're useful to know
if specific indexes can increase performance for problematic queries, since
you can know if PostgreSQL will use these indexes or not without having to
spend resources to create them.

For more thorough informations, please consult the [official
documentation](https://hypopg.readthedocs.io).

For other general information, you can also consult [this blog
post](https://rjuju.github.io/postgresql/2015/07/02/how-about-hypothetical-indexes.html).

Installation
------------

- Compatible with PostgreSQL 9.2 and above
- Needs PostgreSQL header files
- Decompress the tarball
- `sudo make install`
- In every needed database: `CREATE EXTENSION hypopg;`

Usage
-----

NOTE: The hypothetical indexes are contained in a single backend. Therefore,
if you add multiple hypothetical indexes, concurrent connections doing
`EXPLAIN` won't be bothered by your hypothetical indexes.

Assuming a simple test case:

    rjuju=# CREATE TABLE hypo AS SELECT id, 'line ' || id AS val FROM generate_series(1,10000) id;
    rjuju=# EXPLAIN SELECT * FROM hypo WHERE id = 1;
                          QUERY PLAN
    -------------------------------------------------------
     Seq Scan on hypo  (cost=0.00..180.00 rows=1 width=13)
       Filter: (id = 1)
    (2 rows)


The easiest way to create an hypothetical index is to use the
`hypopg_create_index` functions with a regular `CREATE INDEX` statement as arg.

For instance:

    rjuju=# SELECT * FROM hypopg_create_index('CREATE INDEX ON hypo (id)');

NOTE: Some information from the `CREATE INDEX` statement will be ignored, such as
the index name if provided. Some of the ignored information will be handled in
a future release.

You can check the available hypothetical indexes in your own backend:

    rjuju=# SELECT * FROM hypopg_list_indexes();
     indexrelid |                 indexname                 | nspname | relname | amname
     -----------+-------------------------------------------+---------+---------+--------
         205101 | <41072>btree_hypo_id                      | public  | hypo    | btree


If you need more technical information on the hypothetical indexes, the
`hypopg()` function will return the hypothetical indexes in a similar way as
`pg_index` system catalog.

And now, let's see if your previous `EXPLAIN` statement would use such an index:

    rjuju=# EXPLAIN SELECT * FROM hypo WHERE id = 1;
                                         QUERY PLAN
    ------------------------------------------------------------------------------------
     Index Scan using <41072>hypo_btree_hypo_id on hypo  (cost=0.29..8.30 rows=1 width=13)
       Index Cond: (id = 1)
    (2 rows)


Of course, only `EXPLAIN` without `ANALYZE` will use hypothetical indexes:

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
`hypopg_drop_index(indexrelid)` with the OID that the `hypopg_list_indexes()`
function returns and call `hypopg_reset()` to remove all at once, or just close
your current connection.
