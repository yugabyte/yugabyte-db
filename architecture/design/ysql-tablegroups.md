# Tablegroups

> **Note:** This is a new feature in Beta mode.

"In workloads that do very little IOPS and have a small data set, the bottleneck shifts from CPU/disk/network to the number of tablets one can host per node. Since each table by default requires at least one tablet per node, a YugabyteDB cluster with 5000 relations (tables, indexes) will result in 5000 tablets per node. There are practical limitations to the number of tablets that YugabyteDB can handle per node since each tablet adds some CPU, disk and network overhead. If most or all of the tables in YugabyteDB cluster are small tables, then having separate tablets for each table unnecessarily adds pressure on CPU, network and disk.

To help accomodate such relational tables and workloads, we've added support for colocating SQL tables. Colocating tables puts all of their data into a single tablet, called the  _colocation tablet_. This can dramatically increase the number of relations (tables, indexes, etc) that can be supported per node while keeping the number of tablets per node low. Note that all the data in the colocation tablet is still replicated across 3 nodes (or whatever the replication factor is)." - From the existing design of [colocated tables](https://github.com/yugabyte/yugabyte-db/blob/master/architecture/design/ysql-colocated-tables.md).

We have extended this concept to something aptly titled "tablegroups." A tablegroup is a group of tables that will be colocated together on a single *colocation tablet*. This can be specified at the language level in YSQL. This will allow users to create multiple colocation tablets per database.
## Motivation
The motivation behind having multiple groups of co-located tables within a database is similar to that of specifying a database as co-located. Currently, in specifying a database as co-located, we are able to generate a single tablet that all future tables (that do not opt out of co-location) are added to. This allows a user to co-locate closely related tables together and avoid paying the cost of additional round trips when making joins etc. amongst the related data or paying the storage/compute overhead of creating a tablet for every relation, improving performance.

However, this current implementation has a few pitfalls. There is sometimes a need to horizontally scale groups of tables together, especially as the colocation tablet containing them grows excessively large. There are also needs to co-locate multiple different groups of tables (i.e. per-schema) without requiring them all to be on one tablet. This can help improve scalability while still benefitting from the latency improvements afforded by colocation.

**Pros**

* The number of relations per node can increase while keeping the number of tablets low.
* Users can take advantage of colocation without completely losing the ability to scale out. If there are multiple independent groups of tables that need to be colocated, the status quo (database-level colocation) limits scalability.

**Cons:**

- All tables in the tablegroup are placed on a single tablet (one RocksDB) and are replicated throughout the cluster but cannot be partitioned.
- Tables in the tablegroup cannot be horizontally scaled.

# Implementation Details

## Catalog Manager Changes

When a tablegroup is created, the catalog manager should create a parent table and parent tablet for this tablegroup for colocation. This reuses code from the initial commit of database-level colocation to support multiple tables for a single tablet. Same idea of a dummy table as the system tables using the sys catalog table (and kSysCatalogTableId as primary_table_id). These changes are similar to the `CREATE DATABASE` changes found [here](https://github.com/yugabyte/yugabyte-db/blob/master/architecture/design/ysql-colocated-tables.md).

Catalog manager has a per-namespace map of `tablegroup_id` to the `TabletInfo` for that tablegroup. This `tablegroup_id` is a 16-byte Yugabyte UUID that is derived from the `tablegroup oid` and `database oid` in the same way that `PgsqlTableId` is derived.
This looks like `map<tablegroup_id, tablet_id> tablegroup_tablets`; and `map<namespace_uuid, tablegroup_tablets> tablegroup_ids_map`.

Catalog manager also maintains some in-memory metadata for tablegroups.

In the event of a restart, `catalog_loaders` can reload this information based on the persistent table and tablet info.

If the user supplies a tablespace for the tablegroup, we also add an entry to the `table_to_tablespace_id` map that associates the parent table with the given tablespace. The load balancer moves the tablegroup tablets as required, using the same logic it uses for regular tables with assigned tablespaces.

## Postgres system metadata
There are two things that need to be done here:
1. Add a new system catalog to store metadata about the tablegroups within a database.
2. Either add a column to `pg_class` or re-use existing columns in `pg_class` to store the per-relation tablegroup information.

We added a `pg_yb_tablegroup` system catalog that contains the `oid, grpname, grptablespace, grpacl, grpowner, and grpoptions` of each tablegroup. In order to ensure backwards compatibility we check whether the physical table exists in `postinit` and if so, set a Postgres global. Elsewhere in the codebase anytime where a tablegroup-related operation needs to be performed, this PG global is checked to guard opening the relation.

Currently, we use the `reloptions` column of `pg_class` to store the per-relation tablegroup ID information. We have added a new `reloption` of type `OID` to store this information.

Alternatively, we could add another column that would represent the tablegroup that a relation is a part of. This could introduce some issues such as needing a default tablegroup / having to create this column for all prod databases. Until we have an full `initdb` upgrade path in place, we cannot introduce a `reltablegroup`.

### CREATE TABLEGROUP

```
CREATE TABLEGROUP name [TABLESPACE tablespacename]
```

In order to create a tablegroup, a user must either be a superuser or have create privileges on the database the tablegroup is to be created in. At time of tablegroup creation, an OID is generated and existence is checked via a lookup to `pg_yb_tablegroup`. If the tablegroup does not exist, a new tuple is inserted.

We have created a flow for `CREATE TABLEGROUP` as well as corresponding new RPCs. On the catalog manager side, we create a parent table for the tablegroup with a single tablet. Catalog manager adds entries to the appropriate maps. The prefix for the table name and ID is the tablegroup ID and the suffix is a const tablegroup parent ID/name suffix. The colocated property is set for the table and tablet in order to take advantage of existing code.


### DROP TABLEGROUP

To be dropped, a tablegroup must be empty (no relations associated with it) unless the user also provides the `CASCADE` keyword.
```
DROP TABLEGROUP [IF EXISTS] group_name [RESTRICT | CASCADE]
```

On the catalog manager side, corresponding entries are removed from the maps and the parent table & tablet are deleted.

### ALTER TABLEGROUP NAME/OWNER

This amounts to changing the Postgres metadata after checking permissions on the tablegroup.

### GRANT / REVOKE / ALTER DEFAULT PRIVILEGES

RBAC for the tablegroups themselves - `ACL_CREATE` privileges are grantable for tablegroups.

### CREATE TABLE
If the table is created as CREATE TABLE [...] TABLEGROUP tablegroup_name; then the table will be colocated with the rest of the tables in that tablegroup.

To help avoid confusion between colocated db and tablegroups, both are not be allowed within the same namespace. `COLOCATED=true/false` as part of the `CREATE TABLE` ddl will throw an error. If the database was created with `COLOCATED=true`, then any `CREATE TABLE` ddl with `TABLEGROUP` specified will throw an error. Over time we will deprecate `COLOCATED=true/false` from both `CREATE TABLE` and `CREATE DATABASE` and transition to only using `TABLEGROUP`.

We must also check to make sure that a SPLIT clause is not included.

Logical flow for CREATE TABLE looks like this:

1. First check for tablegroup existence and permissions. A user must either be an owner of the tablegroup, a superuser, or have create permissions on the tablegroup in order to create a table within one.
2. From the CreateTableRequestPB, catalog manager is passed the tablegroup ID.
3. Perform a lookup for the tablet that this table is to be added to using catalog manager maps.
4. We have to convert hash columns to range columns since the tablet will not be able to dynamically split if it is colocated.
5. Set the colocated metadata for the table.
6. Add the table to the tablet (using the AddTableToTablet async RPC added with colocated database commit, ChangeMetadataRequest to replicate the table addition).

### CREATE INDEX
There are a couple options available for indexes:
1. By default, an index will follow the tablegroup of its indexed table.
2. The index can opt-out and create its own tablets by providing `NO TABLEGROUP`
3. The index can select a new tablegroup through the `TABLEGROUP group_name` syntax.

### DROP TABLE

We remove the map entries from catalog manager and the corresponding info from the Postgres system metadata. Physical removal of table data will happen during compactions.  By setting the colocated property in table and tablet metadata, we take advantage of existing code.

### TRUNCATE TABLE

As in the [colocated tables design](https://github.com/yugabyte/yugabyte-db/blob/master/architecture/design/ysql-colocated-tables.md), we create a table-level tombstone. Reads are already aware of this table-level tombstone.

### ALTER TABLE (add/remove/modify the tablegroup of a table)

This is currently in the works for the status quo implementation of colocation. We will not support this in v1 for tablegroups.

Here are a couple ideas on how to support this:
1.  Extend ALTER TABLE grammar to support new alter_table_action which include removing association with a tablegroup as well as adding association with a tablegroup post creation.
2.  Support an ALTER TABLEGROUP ddl to do the above.

### UI Changes
The master tables UI displays a table for the parent table information and YSQL OID's of the tablegroups (only when tablegroups are present).

Similarly, when tablegroups are present, a new column will be in the UI for user tables and index tables that will display the parent OID, if any.

## Future Work
-   Supporting the ability to alter the tablegroup of a table/index.
-   Supporting the ability to alter the tablespace of a tablegroup.
-   Automatic tablet splitting of tablegroup parent tablets.
-   Ability to create a CDC stream per-tablegroup.
