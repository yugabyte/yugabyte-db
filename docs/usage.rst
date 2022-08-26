.. _usage:

Usage
=====

Introduction
------------

HypoPG is useful if you want to check if some index would help one or multiple
queries.  Therefore, you should already know what are the queries you need to
optimize, and ideas on which indexes you want to try.

Also, the hypothetical indexes that HypoPG will create are not stored in any
catalog, but in your connection private memory.  Therefore, it won't bloat any
table and won't impact any concurrent connection.

Also, since the hypothetical indexes doesn't really exists, HypoPG makes sure
they will only be used using a simple EXPLAIN statement (without the ANALYZE
option).

Install the extension
---------------------

As any other extension, you have to install it on all the databases where you
want to be able to use it.  This is simply done executing the following query,
connected on the database you want to install HypoPG with a user having enough
privileges:

.. code-block:: psql

  CREATE EXTENSION hypopg ;

HypoPG is now available.  You can check easily if the extension is present
using `psql <https://www.postgresql.org/docs/current/static/app-psql.html>`_:

.. code-block:: psql
  :emphasize-lines: 5

  \dx
                       List of installed extensions
    Name   | Version |   Schema   |             Description
  ---------+---------+------------+-------------------------------------
   hypopg  | 1.1.0   | public     | Hypothetical indexes for PostgreSQL
   plpgsql | 1.0     | pg_catalog | PL/pgSQL procedural language
  (2 rows)

As you can see, hypopg version 1.1.0 is installed.  If you need to check using
plain SQL, please refer to the `pg_extension table documentation
<https://www.postgresql.org/docs/current/static/catalog-pg-extension.html>`_.

Configuration
-------------

The following configuration parameters (GUCs) are available, and can be changed
interactively:

hypopg.enabled:
  Default to ``on``.
  Use this parameter to globally enable or disable HypoPG.  When HypoPG is
  disabled, no hypothetical index will be used, but the defined hypothetical
  indexes won't be removed.

hypopg.use_real_oids:
  Default to ``off``.
  By default, HypoPG won't use "real" object identifiers, but instead borrow
  ones from the ~ 14000 / 16384 (respectively the lowest unused oid less then
  FirstNormalObjectId and FirstNormalObjectId) range, which are reserved by
  PostgreSQL for future usage in future releases.  This doesn't cause any
  problem, as the free range is dynamically computed the first time a
  connection uses HypoPG, and has the advantage to work on a standby server.
  But the drawback is that you can't have more than approximately 2500
  hypothetical indexes at the same time, and creating a new hypothetical index
  will become very slow once more than the maximum number of objects has been
  created until ``hypopg_reset()`` is called.

  If those drawbacks are problematic, you can enable this parameter.  HypoPG
  will then ask for a real object identifier, which will need to obtain more
  locks and won't work on a standby, but will allow to use the full range of
  object identifiers.

  Note that switching this parameter doesn't require to reset the entries, both
  can coexist at the same time.

Supported access methods
------------------------

The following access methods are supported:

- btree
- brin
- hash (requires PostgreSQL 10 or above)
- bloom (requires the bloom extension to be installed)

Create a hypothetical index
---------------------------

.. note::

  Using HypoPG require some knowledge on the **EXPLAIN** command.  If you need
  more information about this command, you can check `the official
  documentation
  <https://www.postgresql.org/docs/current/static/using-explain.html>`_.  There
  are also a lot of very good resources available.

For clarity, let's see how it works with a very simple test case:

.. code-block:: psql

  CREATE TABLE hypo (id integer, val text) ;
  INSERT INTO hypo SELECT i, 'line ' || i FROM generate_series(1, 100000) i ;
  VACUUM ANALYZE hypo ;

This table doesn't have any index.  Let's assume we want to check if an index
would help a simple query.  First, let's see how it behaves:

.. code-block:: psql

  EXPLAIN SELECT val FROM hypo WHERE id = 1;
                         QUERY PLAN
  --------------------------------------------------------
   Seq Scan on hypo  (cost=0.00..1791.00 rows=1 width=14)
     Filter: (id = 1)
  (2 rows)

A plain sequential scan is used, since no index exists on the table.  A simple
btree index on the **id** column should help this query.  Let's check with
HypoPG.  The function **hypopg_create_index()** will accept any standard
**CREATE INDEX** statement(s) (any other statement passed to this function will be
ignored), and create a hypothetical index for each:

.. code-block:: psql

  SELECT * FROM hypopg_create_index('CREATE INDEX ON hypo (id)') ;
   indexrelid |      indexname
  ------------+----------------------
        18284 | <18284>btree_hypo_id
  (1 row)

The function returns two columns:

- the object identifier of the hypothetical index
- the generated hypothetical index name

We can run the EXPLAIN again to see if PostgreSQL would use this index:

.. code-block:: psql
  :emphasize-lines: 4

  EXPLAIN SELECT val FROM hypo WHERE id = 1;
                                      QUERY PLAN
  ----------------------------------------------------------------------------------
   Index Scan using <18284>btree_hypo_id on hypo  (cost=0.04..8.06 rows=1 width=10)
     Index Cond: (id = 1)
  (2 rows)

Yes, PostgreSQL would use such an index.  Just to be sure, let's check that the
hypothetical index won't be used to acually run the query:

.. code-block:: psql

  EXPLAIN ANALYZE SELECT val FROM hypo WHERE id = 1;
                                              QUERY PLAN
  ---------------------------------------------------------------------------------------------------
   Seq Scan on hypo  (cost=0.00..1791.00 rows=1 width=10) (actual time=0.046..46.390 rows=1 loops=1)
     Filter: (id = 1)
     Rows Removed by Filter: 99999
   Planning time: 0.160 ms
   Execution time: 46.460 ms
  (5 rows)

That's all you need to create hypothetical indexes and see if PostgreSQL would
use such indexes.

Manipulate hypothetical indexes
-------------------------------

Some other convenience functions and views are available:

- **hypopg_list_indexes**: view that lists all hypothetical indexes that have
  been created

.. code-block:: psql

  SELECT * FROM hypopg_list_indexes ;
   indexrelid |      indexname       | nspname | relname | amname
  ------------+----------------------+---------+---------+--------
        18284 | <18284>btree_hypo_id | public  | hypo    | btree
  (1 row)

- **hypopg()**: function that lists all hypothetical indexes that have
  been created with the same format as **pg_index**

.. code-block:: psql

  SELECT * FROM hypopg() ;
        indexname       | indexrelid | indrelid | innatts | indisunique | indkey | indcollation | indclass | indoption | indexprs | indpred | amid
  ----------------------+------------+----------+---------+-------------+--------+--------------+----------+-----------+----------+---------+------
   <18284>btree_hypo_id |      13543 |    18122 |       1 | f           | 1      | 0            | 1978     | <NULL>    | <NULL>   | <NULL>  |  403
  (1 row)

- **hypopg_get_indexdef(oid)**: function that lists the CREATE INDEX statement
  that would recreate a stored hypothetical index

.. code-block:: psql

  SELECT indexname, hypopg_get_indexdef(indexrelid) FROM hypopg_list_indexes ;
        indexname       |             hypopg_get_indexdef
  ----------------------+----------------------------------------------
   <18284>btree_hypo_id | CREATE INDEX ON public.hypo USING btree (id)
  (1 row)

- **hypopg_relation_size(oid)**: function that estimates how big a hypothetical
  index would be:

.. code-block:: psql

  SELECT indexname, pg_size_pretty(hypopg_relation_size(indexrelid))
    FROM hypopg_list_indexes ;
        indexname       | pg_size_pretty
  ----------------------+----------------
   <18284>btree_hypo_id | 2544 kB
  (1 row)

- **hypopg_drop_index(oid)**: function that removes the given hypothetical
  index
- **hypopg_reset()**: function that removes all hypothetical indexes
