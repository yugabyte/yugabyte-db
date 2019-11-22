# YSQL Tablet Colocation

> **Note:** This is new feature that is still in design phase.

## Motivation
A lot of applications using relational databases have a high number of tables and indexes (1000+). These tables are typically small and single database size is less than 500 GB. These applications still want to be able to use distributed SQL database like YugabyteDB to leverage high availability and data resilience.

Some of these applications also create multiple databases - for example, 1 DB per customer. In such cases, using the current architecture will result in a huge number of tablets. For example, creating 1000 such DBs will result in 8 million tablets (assuming that each DB has 1000 tables and each table has 8 tablets).

We’ve seen practical limitations to the number of tablets that YugabyteDB can handle per node. Typically, we recommend that we have 3000 tablets / node at max. Also, creating 8 tablets for small tables which have few GB of data is wasteful.

For such use cases, we want to provide an ability to do tablet colocation. With this feature, we’ll create 1 tablet for a database and all tables in the DB will be colocated on the same tablet. This will reduce the overhead from creating multiple tablets for small tables and help us scale such use cases.

## Requirements

### Ability to specify that a database is colocated.

__Syntax:__

CREATE DATABASE name WITH colocated = true | false

We’ll also provide a gflag --ysql_colocation which if enabled will create colocated tablet whenever a new YSQL DB is created.

### Ability for table to opt out of colocation
This is useful if the DB has 1-2 large tables and several small tables. In this case, the small tables can be colocated in a single tablet while the large tables have their own tablets.

__Syntax:__
```
CREATE TABLE name (columns) WITH colocated = true | false
```
Note that this property is only used when the parent DB is colocated. It has no effect otherwise.

### Ability to specify that schema is colocated (P1)

__Syntax:__
```
CREATE SCHEMA name WITH colocated = true | false
```
In some situations, it may be useful for applications to create multiple schemas (instead of multiple DBs) and use 1 tablet per schema.
Using this configuration:
* Enables applications to use PG connection pooling. Typically, connection pools are created per database.
So, if applications have a large number of databases, they cannot use connection pooling effectively.
Connection pools become important for scaling applications since we have a limit of the maximum number of connections that each tserver can accept (300).
* Reduces master overhead. Creating multiple databases adds more overhead on the master since postgres creates 200+ system tables per database.

## Design

### Single vs multiple RocksDB
Today, there is one RocksDB created per tablet. This RocksDB only has data for a single tablet. With multiple tables in a single tablet, we have two options:
* Use single RocksDB for the entire tablet (i.e. for all tables).
* Use multiple RocksDBs with one RocksDB per table.

More analysis on this can be found here.

We decided to use single RocksDB for entire tablet. This is because:
* It enables us to leverage code that was written for postgres system tables. Today, all postgres system tables are colocated on a single tablet in master and uses a single RocksDB. We can leverage a lot of that code.
* We may hit other scaling limits with multiple Rocksdb. For example, it’s possible that having 1 million RocksDBs (1000 DBs, with 1000 tables per DB) will cause other side effects.

### Create and Drop DB / Table

#### Create Database
When a DB is created with colocated=true, catalog manager will need to create a tablet for this database. Catalog manager’s NamespaceInfo and TableInfo objects will need to maintain colocated property.

Today, tablet’s RaftGroupReplicaSuperBlockPB has a primary_table_id. For system tables, this is the table ID of sys catalog table. Primary table ID seems to be used in two ways:
Rows of primary table ID are not prefixed with table ID while writing to RocksDB. All other table rows are prefixed with cotable ID.
Remote bootstrap client checks that tablet being bootstrapped has a primary table. (It’s not clear why it needs this).

Since there is no “primary table” in a colocated DB, we have two options:
Make this field optional. We’ll need to check some dependencies like remote bootstrap to see if this is possible.
Create a dummy table for the database and make that the primary table.

Tablet creation requires a Schema and partition range to be specified. In this case, Schema will empty and partition range will be [-infinity, infinity).

Currently, RocksDB files are created in the folder tserver/data/table-id/tablet-id/. Since this tablet will have multiple tables, the directory structure will change to tserver/data/tablet-id/.

#### Create Table
When a table is created in a colocated database, catalog manager should add that table to the tablet that was created for the database and not create new tablets.
It’ll need to invoke ChangeMetadataRequest to replicate the table addition.

If the table is created with colocated=false, then it should go through the current table creation process and create tablets for the table.

#### Drop Table
When a colocated table is dropped, catalog manager should simply mark the table as deleted (and not remove any tablets). It’ll then need to invoke ChangeMetadataRequest to replicate the table removal. Note that currently ChangeMetadata operation does not support table removal and we’ll need to add this capability.

It can then run a background task to delete all rows corresponding to that table from RocksDB.

If the table being dropped has colocated=false, then it should go through the current drop table process and delete the tablets.

#### Drop Database
This should delete the database from sys catalog and also remove the tablets created.

#### Postgres Metadata
It’ll be useful to store colocated property in postgres system tables (pg_database for database and pg_class for table) for two reasons:
YSQL dump and restore can use to generate the same YB schema.
Postgres cost optimizer can use it during query planning and optimization.

We can reuse “tablespace” field of these tables for storing this information. This field in vanilla postgres dictates the directory / on disk location of the table / database. In YB, we can repurpose it to indicate tablet location.

### Client Changes

* Add is_colocated in SysTabletEntryPB to indicate if a tablet is a colocated tablet.
* Add is_colocated in CreateTabletRequestPB
* For SysCatalogTable, Tablet::AddTable is called when creating a new table. There is no corresponding way to do that when the tablet is in a tserver. Hence we need to add an RPC AddTableToTablet in the TabletServerAdminService, and add AsyncAddTableToTablet task to call that RPC.
* Modify RaftGroupMetadata::CreateNew to take is_colocated parameter. If the table is colocated, use Rocksdb/tablet-<id> as the Rocksdb_dir and wal/tablet-<id> as the wal_dir.

### Load balancing
Today, load balancing is looks at all tables and then balances all tablets for each table. We need to make load balancer aware of tablet colocation in order to avoid balancing the same tablet.

### Local and Remote Bootstrap
This does not require any changes.

### Backup and Restore
Since backup and restore is done at the tablet level, for colocated tables, we cannot backup individual tables.
We’ll need to make the backup / restore scripts work for the entire DB instead of per table.

### Postgres system tables bloat
Having a huge number of databases can result in high load on the master since each database will create 200+ postgres system tables.
We need to test the limit for number of databases that we can create without impacting master and cluster performance.

### Master / Tserver UI
No impact on master UI since all views are per table or per tserver.

Tserver UI tables view uses tablet peers to get the table information. Today, it’ll only display data for the primary table. We’ll need to change this to show all tables in colocated tablet.
Additionally, the /tables view shows on disk size for every table. This per table size is going to be inaccurate for colocated tablets. We’ll need to change this view to reflect data for colocated tablets accurately.

### Metrics
TODO

### Pulling out tables from colocated tablet
When table(s) grows large, it’ll be useful to have the ability to pull the table out of colocated tablet in order to scale. We won’t provide an automated way to do this in 2.1. This can be done manually using the following steps:
* Create a table with the same schema as the table to be pulled out.
* Dump contents of original table using ysql_dump or `COPY` command and importing that into the new table.
* Drop original table.
* Rename new table to the same name as the original table.

### CDC / 2DC
Today, CDC and 2DC create change capture streams per table.
Each stream will cause CDC producers and CDC consumers to start up for each tablet of the stream.
With colocated tables, we need to provide an ability to create CDC stream per database.
We’ll also need an ability to filter out rows for tables that the user is not interested in consuming.
Similarly, generating producer-consumer maps is done per table today. That will need to change to account for colocated tables.

### Yugax Platform
Today, YW provides ability to backup tables. This will need to change since we cannot backup individual tables anymore.
We need to provide back up option for a DB. However, this also depends on supporting backups for YSQL tables.

### Dynamic tablet splitting
Current design for tablet splitting won’t work as is for colocated tablets.
The design finds a split key (approximate mid-key) for the tablet and splits the tablet range into two partitions.
Since colocated tablets have multiple tables with different schemas, we cannot find the “mid point” of the tablet.
We could potentially split the tablet such that some tables are in one tablet and other tables are in the second tablet, but this will require some changes to the design.


[![Analytics](https://yugabyte.appspot.com/UA-104956980-4/architecture/design/ysql-tablet-colocation.md?pixel&useReferer)](https://github.com/YugaByte/ga-beacon)

