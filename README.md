[![PGXN version](https://badge.fury.io/pg/pg_partman.svg)](https://badge.fury.io/pg/pg_partman)

PG Partition Manager
====================

pg_partman is an extension to create and manage both time-based and serial-based table partition sets. Native partitioning in PostgreSQL 10 is supported as of pg_partman v3.0.1 and much more extensively as of 4.0.0 along with PostgreSQL 11. Note that all the features of trigger-based partitioning are not yet supported in native, but performance in both reads & writes is significantly better.

Child table creation is all managed by the extension itself. For non-native, trigger function maintenance is also handled. For non-native partitioning, tables with existing data can have their data partitioned in easily managed smaller batches. For native partitioning, the creation of a new partitioned parent must be done first and the data migrated over after setup is complete.

Optional retention policy can automatically drop partitions no longer needed for both native and non-native partitioning.

A background worker (BGW) process is included to automatically run partition maintenance without the need of an external scheduler (cron, etc) in most cases.

Bug reports & feature requests can be directed to the Issues section on Github - <https://github.com/pgpartman/pg_partman/issues>

For questions, comments, or if you're just not sure where to post, please use the Discussions section on Github. Feel free to post here no matter how minor you may feel your issue or question may be - <https://github.com/pgpartman/pg_partman/discussions>

If you're looking for a partitioning system that handles any range type beyond just time & serial, the new native partitioning features in PostgreSQL 10+ are likely the best method for the foreseeable future. If this is something critical to your environment, start planning your upgrades now!

If you're still trying to evaluate whether partitioning is a good choice for your environment, keep an eye on the HypoPG project. Version 2 will have a hypothetical partitioning feature that will let you evaluate different partitioning schemes without requiring you to actually partition your data. I may see about integrating this feature into pg_partman once it is available. - <https://hypopg.readthedocs.io>

INSTALLATION
------------
Requirement: 

 * PostgreSQL >= 10

Recommended: 

 * Native partitioning is highly recommended over trigger-based and PG11+ is HIGHLY recommended over PG10.
 * [pg_jobmon](https://github.com/omniti-labs/pg_jobmon) (>=v1.4.0). PG Job Monitor will automatically be used if it is installed and setup properly.

In the directory where you downloaded pg_partman, run

```sh
make install
```

If you do not want the background worker compiled and just want the plain PL/PGSQL functions, you can run this instead:


```sh
make NO_BGW=1 install
```

The background worker must be loaded on database start by adding the library to shared_preload_libraries in postgresql.conf

    shared_preload_libraries = 'pg_partman_bgw'     # (change requires restart)

You can also set other control variables for the BGW in postgresql.conf. "dbname" is required at a minimum for maintenance to run on the given database(s). These can be added/changed at anytime with a simple reload. See the documentation for more details. An example with some of them:

    pg_partman_bgw.interval = 3600
    pg_partman_bgw.role = 'keith'
    pg_partman_bgw.dbname = 'keith'

Log into PostgreSQL and run the following commands. Schema is optional (but recommended) and can be whatever you wish, but it cannot be changed after installation. If you're using the BGW, the database cluster can be safely started without having the extension first created in the configured database(s). You can create the extension at any time and the BGW will automatically pick up that it exists without restarting the cluster (as long as shared_preload_libraries was set) and begin running maintenance as configured.

```sql
CREATE SCHEMA partman;
CREATE EXTENSION pg_partman SCHEMA partman;
```

As of version 4.1.0, pg_partman no longer requires a superuser to run for native partitioning. Trigger-based partitioning still requires it, so if you want to not require superuser, look to migrating to native partitioning. Superuser is still required to install pg_partman. It is recommended that a dedicated role is created for running pg_partman functions and to be the owner of all partition sets that pg_partman maintains. At a minimum this role will need the following privileges (assuming pg_partman is installed to the "partman" schema and that dedicated role is called "partman"):

```sql
CREATE ROLE partman WITH LOGIN;
GRANT ALL ON SCHEMA partman TO partman;
GRANT ALL ON ALL TABLES IN SCHEMA partman TO partman;
GRANT EXECUTE ON ALL FUNCTIONS IN SCHEMA partman TO partman;
GRANT EXECUTE ON ALL PROCEDURES IN SCHEMA partman TO partman;  -- PG11+ only
GRANT ALL ON SCHEMA my_partition_schema TO partman;
GRANT TEMPORARY ON DATABASE mydb to partman; -- allow creation of temp tables to move data out of default 
```

If you need the role to also be able to create schemas, you will need to grant create on the database as well. In general this shouldn't be required as long as you give the above role CREATE privileges on any pre-existing schemas that will contain partition sets.

```sql
GRANT CREATE ON DATABASE mydb TO partman;
```

I've received many requests for being able to install this extension on Amazon RDS. As of PostgreSQL 12.5, RDS has made the pg_partman extension available. Many thanks to the RDS team for including this extension in their environment!

<https://docs.aws.amazon.com/AmazonRDS/latest/UserGuide/PostgreSQL_Partitions.html>

UPGRADE
-------

Run "make install" same as above to put the script files and libraries in place. Then run the following in PostgreSQL itself:

```sql
ALTER EXTENSION pg_partman UPDATE TO '<latest version>';
```

If you are doing a `pg_dump`/`pg_restore` and you've upgraded pg_partman in place from previous versions, it is recommended you use the `--column-inserts` option when dumping and/or restoring pg_partman's configuration tables. This is due to ordering of the configuration columns possibly being different (upgrades just add the columns onto the end, whereas the default of a new install may be different).

If upgrading between any major versions of pg_partman (2.x -> 3.x, etc), please carefully read all intervening version notes in the [CHANGELOG](CHANGELOG.txt), especially those notes for the major version. There are often additional instructions (Ex. updating trigger functions) and other important considerations for the updates.

IMPORTANT NOTE: Some updates to pg_partman must drop and recreate its own database objects. If you are revoking PUBLIC privileges from functions/procedures, that can be added back to objects that are recreated as part of an update. If restrictions from PUBLIC use are desired for pg_partman, it is recommended to install it into its own schema as shown above and the revoke undesired access to that schema. Otherwise you may have to add an additional step to your extension upgrade procedures to revoke PUBLIC access again.

EXAMPLES
--------
For setting up native partitioning with pg_partman on a brand new table, or to migrate an existing normal table to native partitioning, see [pg_partman_howto_native.md](doc/pg_partman_howto_native.md).

For migrating a trigger-based partitioned table to native partitioning using pg_partman, see [migrate_to_native.md](doc/migrate_to_native.md).

Other HowTo documents are available in the [doc](doc) folder.

See [pg_partman.md file](doc/pg_partman.md) in the [doc](doc) folder for full details on all commands and options for pg_partman.


TESTING
-------
This extension can use the pgTAP unit testing suite to evaluate if it is working properly (http://www.pgtap.org).
WARNING: You MUST increase max_locks_per_transaction above the default value of 64. For me, 128 has worked well so far. This is due to the sub-partitioning tests that create/destroy several hundred tables in a single transaction. If you don't do this, you risk a cluster crash when running subpartitioning tests.

