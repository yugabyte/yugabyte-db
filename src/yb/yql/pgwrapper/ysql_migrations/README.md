General information
-------------------

This directory contains YSQL migrations - special SQL scripts used to upgrade existing clusters when
creating a fresh cluster is not an option. Migrations are invoked by user
through `yb-admin upgrade_ysql`.

Migrations are run in a special YSQL upgrade mode that changes some expectations about SQL being
executed and allows some new grammar - see the next section for details.

Migration scripts should be designed idempotent - i.e. in such a way so that re-running them
multiple times would be okay.

The script will be run in the scope of a single transaction, unless you explicitly start a migration
with `BEGIN`. Unfortunately, according to YugabyteDB limitations, DDLs are executed in their own
separate transactions. This should be handled as follows:

* When combining DDLs and DMLs, start with DDLs and follow them with DMLs.
* Use explicit `BEGIN`/`COMMIT` to encompass the DDL block and DML block, otherwise you're
  guaranteed to hit a read restart error in your DML due to the way `libpq` handles batch
  processinng.
* In your code, you should always account for the fact that the table might've been created but not
  populated. This is possible during migration, or if DML fails while DDL succeeds.

Most migrations code should be tested in `org.yb.pgsql.TestYsqlUpgrade` Java test.

Naming and versioning
---------------------

All migrations should follow the same naming pattern:

    V<major version><.optional minor version>__<GitHub issue number>__<short underscore-separated description>.sql

Examples:

    V1__...
    V2__...
    V3__...
    V4__5408__jsonb_path.sql

    // If you ever need to backport V4 to a release which only has V1 and V2
    V2.1__5408__jsonb_path.sql

Here, major version starts at 1 and is incremented by 1 for each subsequent migration. When
introducing a new migration to the latest (master) branch, major version should be incremented; when
adding a migration to a backport (release) branch, minor version should be incremented.

`major.minor` combination should never be reused across all branches. To accomplish that, make sure
that no two stable YB releases (say v2.6 and v2.8) have the same major version. Introduce a NOOP
migration to prevent that before creating v2.8, if necessary.

Example of how different versions would be named in different branches:

    Master branch:     V1, V2, V3, V4, V5, V6
    Release branch A:  V1, V2, V2.1 (V4), V2.2 (V6)
    Release branch B:  V1, V2, V3, V3.1 (V6), V3.2 (V5)

This accounts for the fact that migrations can be backported out of order.

As you'd expect, migrations are applied in the ascending version order.

When you introduce a new migration, please update `pg_yb_migration.dat` file with the latest
version.

How to write migrations
-----------------------

Postgres' SQL grammar was slightly extended to make writing migrations easier. In particular:

* `CREATE TABLE`:
    * Reloptions `table_oid=<OID>` and `row_type_oid=<OID>` are required.
    * Only PK and index constraints are allowed, they should have a name and `table_oid=<OID>`.
    * Creating shared relations is allowed via `TABLESPACE pg_global`.

* `CREATE VIEW`:
    * Reloption `use_initdb_acl=<bool>`, if true, will set the ACL (permissions) as if the view was
      created by initdb using `yb_system_views.sql`.

* Both of the above were changed to imitate what initdb is doing - e.g. dependencies recorded would
  be different than you'd normally expect.

For use cases other than `CREATE TABLE` and `CREATE VIEW` - e.g. for adding a function -
explicit `INSERT INTO` should be used. For that, `INSERT` is allowed to specify oid column,
and `ON CONFLICT DO NOTHING` clause. If an explicit oid value is not available, use `pg_nextoid()`
in the `INSERT`.

When oid is not specified  explicity (for `CREATE VIEW`, or `INSERT` with `pg_nextoid()`), it's
auto-generated sequentially in `[FirstUnpinnedObjectId; FirstNormalObjectId)` range starting at
where initdb left off. Naturally, the created object will be unpinned (droppable). If this is
not desired, explicitly specifiy an oid (< `FirstUnpinnedObjectId`) during the `INSERT`.

Sometimes, `INSERT ON CONFLICT DO NOTHING` alone is not enough - e.g.
`pg_depend` has no sensible primary key to cause conflict, and `pg_amop` has auto-generated OIDs. In
such cases, use `DO` procedure block to check for rows being present already.
See `V3__5408__jsonb_path.sql` for an example of both.

You should use modifications other than `CREATE TABLE`, `CREATE VIEW`, `CREATE EXTENSION`, `INSERT`
and `UPDATE` with great care, as they weren't tested to work as expected. Also, don't forget to
explicitly specify `pg_catalog` schema everywhere where it's appropriate.

Note that YugabyteDB doesn't support nested transactions, and DDL are performed outside of the current
transaction. If you want to mix and match DDLs and DML statements, you have to separate them and use
explicit `BEGIN`/`COMMIT`s - this will disable automatic transaction wrapping by libpq and will make
DDL changes visible to DML.

To allow DMLs to modify system tables directly, use

    SET LOCAL yb_non_ddl_txn_for_sys_tables_allowed TO true;

in a transaction after DDLs are done.

Tips
----

* Try to use a similar existing migration as a basis for your new one.

* You can get an idea of how your initdb change alters the resulting state of system catalog by
  running `ysql_dump --oids --column-inserts --schema=pg_catalog --dbname=template1 > output.sql`
  after `reinitdb`, and following predefined OIDs you've introduced. Note that it will most likely
  add rows to `pg_depend` as well.

* Modifying system functions is possible but should be done with care to not to break user's schema.
  One frequent use case is adding trailing output columns for internal YB system functions.
  However, doing so requires tweaking the function's backing C code to dynamically adapt to the
  number of output columns expected by the client. Use `V17__7378__colocation_id.sql` and
  `yb_table_properties(PG_FUNCTION_ARGS)` as a reference.

Create a new system table
-------------------------
For an example, here is the creation of the pg_yb_tablegroup table.
The important files are
- src/postgres/src/backend/catalog/Makefile
- src/postgres/src/backend/catalog/genbki.pl
- src/postgres/src/include/catalog/catalog.h
- src/postgres/src/include/catalog/pg_yb_migration.dat
- src/postgres/src/tools/pgindent/typedefs.list
- src/postgres/src/include/catalog/new_header_file.h
- src/yb/yql/pgwrapper/ysql_migrations/V##_#####_new_migration.sql

new_header_file.h will reference new_header_file_d.h.
This file is generated by genbki.pl as part of ybd.
It is important that all OIDs and whitespace match exactly what is expected, otherwise the
genbki.pl script will silently fail to generate the _d.h file, resulting in build failures.

Testing
-------

There's a `TestYsqlUpgrade#migratingIsEquivalentToReinitdb` test that would make sure your migration
does the same as initdb.

To check a migration manually:

* Using a pre-diff system catalog, apply a migration:
  `yb-admin upgrade_ysql`

* Take a YSQL dump with OIDs:
  `ysql_dump --oids --column-inserts --schema=pg_catalog --dbname=template1 > output_before.sql`

* Re-generate catalog and re-create the cluster:
  `ybd reinitdb && ./bin/yb-ctl destroy && ./bin/yb-ctl create --rf=1`

* Take another YSQL dump and compare them. Make sure only auto-generated OIDs (those
  in `[10000; 16384)` range) differ, not the row values.

Known issues
------------

* Creating index on an existing catalog table is technically possible but shouldn't be done. This is
  because index backfilling for system catalog tables is disabled, but locking table doesn't work
  because, at the time of this writing, we don't support `LOCK TABLE`. `SELECT FOR UPDATE` works,
  but it doesn't stop single-row transactions from going through. Enabling backfill is posible, but
  would require additional testing effort, so it was left for later.

* `DROP TABLE pg_catalog.xxx` is currently not implemented, so you can't roll back table creation
  even if you want to!

* When modifying an existing system entity, keep in mind that clients might keep older version
  stored in cache. To work around that, force clients to refresh their caches using
  ```
  UPDATE pg_catalog.pg_yb_catalog_version SET current_version = current_version + 1;
  ```

* Modifying a function used in a system view query might cause upgrade test to fail with a
  mismatch on `pg_rewrite`. This is because a rewrite node tree contains all columns, even ones not
  used by the view, and we try to minimize differences to a fresh cluster.
  Solution is to re-create a view using `CREATE OR REPLACE VIEW`, even if underlying SQL is exactly
  the same.
