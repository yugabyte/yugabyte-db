---
title: System catalog tables and views
linkTitle: System catalog
headcontent: Tables and views that show information about the database
image: fa-sharp fa-thin fa-album-collection
menu:
  preview:
    identifier: architecture-system-catalog
    parent: architecture
    weight: 550
showRightNav: true
type: docs
---

System catalogs, also known as system tables or system views, play a crucial role in the internal organization and management of the database, and serve as the backbone of YugabyteDB's architecture. YugabyteDB builds upon the system catalog of [PostgreSQL](https://www.postgresql.org/docs/current/catalogs.html). These catalogs form a centralized repository that stores metadata about the database itself, such as tables, indexes, columns, constraints, functions, users, privileges, extensions, query statistics, and more. All the system catalog tables and views are organized under the `pg_catalog` schema.

To list the tables in the system catalog, you can execute the following command:

```sql
SELECT tablename FROM pg_catalog.pg_tables WHERE schemaname='pg_catalog';
```

To list all the views in the system catalog, you can execute the following command:

```sql
SELECT viewname FROM pg_catalog.pg_views WHERE schemaname='pg_catalog';
```

To get the details of the names and type information of columns in a table, you can run the following command:

```sql
\d+ <table-name>
```

Let us look at some of the most important system catalog tables and views in detail followed by a summary of other members.

## pg_attribute

The `pg_attribute` table is a crucial component of PostgreSQL's system catalogs, serving as the central repository for storing metadata about the columns (attributes) of all relations (tables, views, indexes, and so on) in the database. This table plays a vital role in various aspects of the database management system, including query optimization, data integrity enforcement, schema management, and introspection capabilities.

By maintaining detailed information about column names, data types, constraints, and other properties, the `pg_attribute` table enables the database to understand and manage database objects correctly. It is extensively used by the query optimizer to generate efficient execution plans, by the DBMS to enforce data integrity rules, and by administrators and developers for tasks such as database documentation, maintenance, and troubleshooting.

<details> <summary>Overview of significant columns.</summary>

1. **Identification** :

    - _attrelid_ : The [OID](../key-concepts/#oid) of the table this column belongs to.
    - _attname_ : The name of the column.
    - _attnum_ : The number of the column.

2. **Data type information** :

    - _atttypid_ : The [OID](../key-concepts/#oid) of the data type of the column.
    - _attlen_ : The length of the column's data type.
    - _atttypmod_ : The type-specific auxiliary data for the column.
    - _attndims_ : The number of dimensions, if the column is an array type.
    - _attbyval_ : Indicates whether the type is passed by value.
    - _attstorage_ : Shows the [TOAST](https://www.postgresql.org/docs/current/storage-toast.html) strategy of the column (extended, external, main, or plain).

3. **Default value and constraints** :

    - _atthasdef_ : Indicates whether the column has a default value.
    - _attidentity_ : Shows if the column is an identity column (always, default, not, or null).
    - _attnotnull_ : Indicates whether the column is marked NOT NULL.
    - _atthasmissing_ : Indicates whether the column has a missing value.

4. **Statistics and optimization** :

    - _attstattarget_ : The target number of statistics data entries to collect during `ANALYZE`.
    - _attcollation_ : The [OID](../key-concepts/#oid) of the collation of the column (if applicable).
    - _attacl_ : Access privileges; this is an array of access control entries (ACE).
    - _attoptions_ : Column-level options. A list of options in the form name=value, separated by commas.
    - _attfdwoptions_ : Options for foreign-data wrapper use.

</details>

## pg_class

The `pg_class` table serves as a comprehensive repository for storing metadata about all relations (tables, views, indexes, sequences, and other relation types) in the database. This table plays a crucial role in various aspects of database management, including query optimization, data storage and access, and overall system administration.

By maintaining detailed information about relation names, types, properties, storage locations, and access permissions, the `pg_class` table allows to understand and manage database objects correctly. It is extensively used by the query optimizer to generate efficient execution plans, by storage managers to handle data files and disk usage, and by administrators for tasks such as monitoring, tuning, and maintaining the database.

<details> <summary>Overview of significant columns.</summary>

1. **Identification** :
    - _relname_ : The name of the table, index, sequence, or view.
    - _relnamespace_ : The [OID](../key-concepts/#oid) of the namespace that contains this table.
    - _reltype_ : The [OID](../key-concepts/#oid) of the data type of this table.
    - _reloftype_ : The [OID](../key-concepts/#oid) of the original data type of this table.
    - _relowner_ : The [OID](../key-concepts/#oid) of the user who owns this table.
    - _relam_ : The [OID](../key-concepts/#oid) of the access method the table uses.

2. **File system** :
    - _relfilenode_ : The file system level name of this table.
    - _reltablespace_ : The [OID](../key-concepts/#oid) of the tablespace in which this table is stored.
    - _relpages_ : The number of disk pages used by this table.
    - _reltuples_ : The estimated number of rows in this table.
    - _relallvisible_ : The number of pages that are marked for all visible entries.

3. **Access statistics** :
    - _reltoastrelid_ : The [OID](../key-concepts/#oid) of the "TOAST table" associated with this table.
    - _relhasindex_ : Indicates if this table has any secondary indexes.
    - _relisshared_ : Indicates if this table is shared across all databases.

4. **Physical statistics** :
    - _relpersistence_ : This column stores a single character value indicating the persistence status of a table. The possible values could be:
        - `p` : This means that the table is "permanent", that is, it is expected to be present and retain its data across PostgreSQL server restarts.
        - `u` : This stands for "unlogged". An unlogged table is expected to be faster than a permanent table because changes are not written to the write-ahead log (which can slow down data modification operations), but they are not crash-safe: an unlogged table is automatically truncated after a crash or unclean shutdown.
        - `t` : This designates a "temporary" table. Temporary tables are automatically dropped at the end of a session, or optionally at the end of the current transaction (see ON COMMIT below). Existing permanent tables with the same name are not visible to the current session while the temporary table exists, unless they are referenced with schema-qualified names.
    - _relkind_ : This is a single-character column that identifies the specific kind of relation. The possible values are:
        - `r` : ordinary table
        - `i` : index
        - `S` : sequence
        - `v` : view
        - `m` : materialized view
        - `c` : composite type
        - `t` : TOAST table
        - `f` : foreign table
        - `p` : partitioned table
        - `I` : partitioned index
    - _relnatts_ : The number of columns in this table.
    - _relchecks_ : The number of checks on this table.

5. **Constraints** :
    - _relhasrules_ : Indicates if this table has any rewrite rules.
    - _relhastriggers_ : Indicates if this table has any triggers.
    - _relhassubclass_ : Indicates if this table has any child tables.
    - _relrowsecurity_ : Indicates if row level security is enabled on this table.
    - _relforcerowsecurity_ : Indicates if row level security should be forced on this table.
    - _relispartition_ : Indicates if this table is a partition.
    - _relreplident_  :  A one-character value about how rows in the table can be uniquely identified for logical replication. Possible values include:
        - `d`: The default rules for the `ROW IDENTITY` are applied. If a primary key exists, it is used, otherwise, no row identity is available.
        - `n`: No row identity column is active, logical replication will not be able to identify individual rows.
        - `f`: The first suitable unique constraint, including the primary key if one exists, is used.
        - `i`: A user-defined index, identified by the `relreplidentidx` attribute, is used.
    - _relispopulated_ : Indicates if this table is populated with data.

</details>

## pg_constraints

The `pg_constraint` table stores information about constraints on tables. Constraints are rules that specify certain conditions that the data in a table must fulfill. These can include unique constraints, check constraints, primary key constraints, and foreign key constraints.

<details> <summary>Overview of significant columns.</summary>

1. **Identification** :
    - _conname_ : The name of the constraint as a string.
    - _connamespace_ : The [OID](../key-concepts/#oid) of the namespace that contains the constraint.

2. **Type and properties** :
     - _contype_ : Signifies the type of the constraint. The possible values:
        - `c` denotes a Check constraint on a table
        - `f` denotes a foreign key constraint
        - `p` denotes a primary key constraint
        - `u` denotes a unique constraint
        - `t` denotes a constraint trigger
        - `x` denotes an exclusion constraint
        - `o` denotes a referential (foreign-key) constraint that was originally specified as a column constraint. This is not creatable, but it can be an outcome of column merging.
    - _condeferrable_ : True if the constraint can be deferred.
    - _condeferred_ : True if the constraint is initially deferred.

3. **Constraint keys** :
    - _conrelid_ : The [OID](../key-concepts/#oid) of the relation that this constraint is on.
    - _confrelid_ : For foreign key constraints, this is the [OID](../key-concepts/#oid) of the referenced table.
    - _conkey_ : An array of integer column numbers that the constraint is on.
    - _confkey_ : For foreign key constraints, this is an array of integer column numbers in the referenced table.
    - _conpfeqop_ : For foreign key constraints, this is an array of [OIDs](../key-concepts/#oid) of the equality operators for the referenced table.

4. **Constraint expression** :
    - _consrc_ : The source of the constraint, as a string.
    - _conbin_ : The internal form of the constraint expression, as a node tree.

5. **Constraint actions** :
    - _confupdtype_ : The action that is taken when the referenced data is updated. The possible values for this column are:
        - 'a' : NO ACTION - no action will be taken to maintain the integrity of the data in the table with the constraint
        - 'r' : RESTRICT - prevents the update if there is any row in the table that references the updated data
        - 'c' : CASCADE - updates all rows in the table that reference the updated data
        - 'n' : SET NULL - sets the foreign key to NULL
        - 'd' : SET DEFAULT - sets the foreign key to its default value
    - _confdeltype_ : The action to perform when the referenced data is deleted. Same as `confupdtype`.
    - _confmatchtype_ : The match type of the foreign key constraint. The possible values are 'f', 'p', 'u', 's', 'b' which stand for `FULL`, `PARTIAL`, `SIMPLE`, `MATCH FULL`, `MATCH PARTIAL`, `MATCH SIMPLE` respectively. The match type determines how NULL values in a composite foreign key are treated. For example, in MATCH FULL, both NULLs are considered equal, while in MATCH SIMPLE, NULLs are not considered equal.

6. **Validation** :
    - _convalidated_ : True if the constraint has been validated, false otherwise.

</details>

## pg_database

The `pg_database` table serves as a centralized repository for database metadata, including database names, encoding, and other properties. It facilitates tasks such as database configuration, user access control, and cross-database operations.

<details> <summary>Overview of significant columns.</summary>

1. **Identification** :

    - _datname_  : The name of the database.
    - _datdba_  : The ID of the user who owns the database.

2. **Physical storage** :

    - _datistemplate_  : Boolean indicating whether the database is a template.
    - _datallowconn_  : Boolean indicating whether connections are allowed to the database.
    - _datconnlimit_  : The maximum number of concurrent connections allowed to this database (-1 means no limit).
    - _datlastsysoid_  : The highest [OID](../key-concepts/#oid) that system objects in the database can have.

3. **Character set** :

    - _encoding_  : The character encoding for this database.
    - _datcollate_  : The LC_COLLATE setting for this database.
    - _datctype_  : The LC_CTYPE setting for this database.

</details>

## pg_index

The `pg_index` table provides crucial insights into indexing strategies and query optimization. It stores metadata about indexes, including details such as the indexed columns, index types, and index properties like uniqueness and inclusion of nullable values.

This data can be used to analyze index usage patterns, identify potential inefficiencies or bloat in index structures, and optimize database performance. By querying `pg_index`,you can make informed decisions about index maintenance, creation, or modification based on workload characteristics and query requirements.

<details> <summary>Overview of significant columns.</summary>

1. **Identification** :
    - _indexrelid_ : [OID](../key-concepts/#oid) of the index relation.
    - _indrelid_ : [OID](../key-concepts/#oid) of the table this index is on.

2. **Index key information** :
    - _indnatts_ : The number of columns in the index key.
    - _indnkeyatts_ : The number of columns this index compares by value.
    - _indkey_ : An array of `indnatts` values that give the column numbers of the table columns in the index.
    - _indcollation_ : An array of `indnatts` [OIDs](../key-concepts/#oid), telling which collations are used for each column of the index.

3. **Index constraint information** :
    - _indisunique_ : True if index is a unique constraint.
    - _indisprimary_ : True if index is a primary key constraint.
    - _indisexclusion_ : True if index supports an exclusion constraint.
    - _indimmediate_ : True if uniqueness is enforced immediately.

4. **Index storage information** :
    - _indisclustered_ : True if table is clustered on this index.
    - _indisvalid_ : True if index is currently valid for queries.
    - _indcheckxmin_ : True if xmin value should be checked when using the index.
    - _indisready_ : True if index can be used in queries.
    - _indislive_ : True if index can still have new entries added to it.
    - _indisreplident_ : True if index is a replica identity index.

5. **Index Expression and Predicate Information** :
    - _indexprs_ : Expression trees (as a list of Node nodes) for partial indexes.
    - _indpred_ : Expression tree (as a List of Node nodes) for predicate of a partial index.

</details>

## pg_locks

The `pg_locks` table plays a crucial role in maintaining data consistency and concurrency control. It provides detailed information about current locks held by active transactions, including lock types (for example, shared, exclusive), lock modes, and the associated database objects being locked.

This view is invaluable for diagnosing and troubleshooting locking-related issues, such as deadlocks or contention. By querying `pg_locks`, you can monitor lock escalation, detect long-running transactions holding locks, and optimize transaction isolation levels to minimize lock contention and improve database concurrency.

{{<tip>}}
[pg_locks](#pg-locks) view can be joined to [pg_stat_activity](#pg-stat-activity) view on the `pid` column to get more information on the session holding or awaiting each lock.
{{</tip>}}

{{<note>}}
The pg_locks view doesn't have a documented view definition that you can directly inspect in the database. This is because the view definition relies on internal data structures used by the lock manager, and these structures aren't intended for direct user access.
{{</note>}}

<details> <summary>Overview of significant columns.</summary>

1. **Identification** :

    - _locktype_ : The type of object being locked. Possible values include:
        - `relation`: A table, index, sequence, or toast table
        - `extend`: A table extension
        - `page`: A specific page in a specific relation
        - `tuple`: A specific tuple in a specific relation
        - `transactionid`: A transaction ID (only used for shared and exclusive row-level locks taken by a currently running transaction)
        - `virtualxid`: A virtual transaction ID (only used for shared and exclusive row-level locks taken by a currently running transaction)
        - `object`: A database object
        - `userlock`: A user lock
        - `advisory`: An advisory lock
    - _database_ : The [OID](../key-concepts/#oid) of the database that contains the object to be locked. For locktypes other than `relation`, `page`, `tuple`, `object`, and `userlock`, this column is null.
    - _relation_ : The [OID](../key-concepts/#oid) of the table, index, or TOAST table to be locked. For locktypes other than `relation`, `page`, `tuple`, and `object`, this column is null.
    - _page_ : Number of disk pages to be locked. For locktypes other than `page`, this column is null.
    - _tuple_ : Number of tuples to be locked. For locktypes other than `tuple`, this column is null.
    - _virtualxid_ : The virtual transaction ID to be locked. For locktypes other than `virtualxid` and `transactionid`, this column is null.
    - _transactionid_ : The transaction ID to be locked. For locktypes other than `transactionid`, this column is null.
    - _classid_ : The [OID](../key-concepts/#oid) of the system catalog containing the object to be locked. For locktypes other than `object`, this column is null.
    - _objid_ : The [OID](../key-concepts/#oid) of the specific system object to be locked. For locktypes other than `object`, this column is null.
    - _objsubid_ : This column contains a further specifier of the system object to be locked. For locktypes other than `object`, this column is null.

2. **Properties** :
    - _fastpath_ : A boolean that indicates whether this is a fast-path lock. Fast-path locks are a performance optimization for certain commonly used lock types.
    - _granted_ : A boolean that indicates whether the lock has been granted. If `false`, the backend is still waiting for this lock.
    - _mode_ : The type of lock that is held or awaited. The possible values include:
        - `AccessShareLock`: This lock is required for reading a table or view. Multiple AccessShareLocks can be held at the same time on the same table or view.
        - `RowShareLock`: This lock is required for scanning a table for update or delete. Multiple RowShareLocks can be held at the same time on the same table.
        - `RowExclusiveLock`: This lock is required for updating or deleting a row. Multiple RowExclusiveLocks can be held at the same time on the same table.
        - `ShareUpdateExclusiveLock`: This lock protects against concurrent schema changes and VACUUM runs.
        - `ShareLock`: This lock is required for VACUUM, CREATE INDEX, and other commands that need to block writes but not reads of a table.
        - `ShareRowExclusiveLock`: This lock is required for commands that have the same conflict behavior as ShareLock, but also block a concurrent ShareLock.
        - `ExclusiveLock`: This lock is used for commands that need to block both reads and writes of a table.
        - `AccessExclusiveLock`: This lock blocks all other locks, used for ALTER TABLE, DROP TABLE, TRUNCATE, REINDEX, CLUSTER, and VACUUM FULL.
    - _pid_ : The process ID of the backend that is holding or waiting for this lock.
    - _virtualtransaction_ : The backend's local transaction ID, also known as the virtual transaction ID.

{{<tip>}}
To learn more about how the pg_locks can be used get insights on transaction locks, see [Lock insights](../../explore/observability/pg-locks).
{{</tip>}}

</details>

## pg_namespace

The `pg_namespace` catalog stores metadata about schemas, including schema names, owner information, and associated privileges. By querying `pg_namespace`, users can retrieve information about existing schemas, verify ownership, and grant or revoke privileges at the schema level.

<details> <summary>Overview of significant columns.</summary>

1. **Identification** :

    - _nspname_ :  The name of the namespace.
    - _nspowner_ :  The ID of the user who owns the namespace.
    - _nspacl_ :  Access privileges of the namespace. This is an array type that holds the Access Control Lists (ACL).

</details>

## pg_proc

The `pg_proc` catalog stores metadata about database procedures, including their names, argument types, return types, source code, and associated permissions. It enables users to query and understand the structure and properties of stored procedures in the database.

Database developers and administrators rely on `pg_proc` to retrieve information about existing procedures, verify input and output types, and manage procedural objects efficiently. By querying `pg_proc`, you can inspect function definitions, review function dependencies, and monitor usage statistics to optimize query performance and database operations.

<details> <summary>Overview of significant columns.</summary>

1. **Identification** :
    - _pronamespace_ : The [OID](../key-concepts/#oid) of the namespace (schema) that contains this function.
    - _proowner_ : The [OID](../key-concepts/#oid) of the user who owns this function.

2. **Function Definition** :
    - _proname_ : The name of the function.
    - _prolang_ : The [OID](../key-concepts/#oid) of the language that this function is implemented in.
    - _prorettype_ : The [OID](../key-concepts/#oid) of the type of the value that the function returns.
    - _proargtypes_ : An array of [OIDs](../key-concepts/#oid) of the types of the function's arguments.
    - _prosrc_ : The source code of the function (for functions written in interpreted languages).
    - _probin_ : The binary code of the function (for functions written in compiled languages).

3. **Properties** :
    - _proisagg_ : A boolean flag indicating whether this function is an aggregate function.
    - _prosecdef_ : A boolean flag indicating whether this function is a security definer.
    - _proiswindow_ : A boolean flag indicating whether this function is a window function.
    - _proretset_ : A boolean flag indicating whether this function returns a set of values.
    - _provolatile_ : This column indicates the volatility classification of the function. The possible values are:
        - `i` for `immutable` functions, which means that the function cannot modify the database and always returns the same result given the same argument values. It does not do database lookups or otherwise use information not directly present in its argument list. If all its arguments are immutable expressions and all its columns are basic function arguments, the function is also immutable.
        - `s` for `stable` functions, which means that the function cannot modify the database, and in a single table scan it will consistently return the same result for the same argument values but may return different results in different transactions for the same argument values. This is the default category for functions that have any side effects or that depend on the database state (such as the current time).
        - `v` for `volatile` functions, which means that the function value can change even in a single table scan, so no optimizations can be made based on it. A function must be classified as volatile if it has side-effects, as it may be important to guarantee that the side-effects happen in a certain order. Also, a function may be volatile if its results depend on the database state that may change in a single query.
    - _proleakproof_ : A boolean flag indicating whether this function is leak-proof.

4. **Cost and Optimization** :
    - _procost_ : The estimated execution cost of the function.
    - _prorows_ : The estimated number of rows that the function will return. It is only relevant for set-returning functions.

5. **Function Arguments** :
    - _proallargtypes_ : An array of [OIDs](../key-concepts/#oid) of the types of all the function's arguments, including both input and output arguments.
    - _proargmodes_  :  This column is used to store the mode of each argument of a function. It is of type `char[]`, an array of characters. If this column is null, then all arguments are assumed to be `IN` arguments. The possible values that can be stored in this column are:
        - `i` stands for `IN` mode. This means that the argument is an input argument and will only be used to pass values into the function.
        - `o` stands for `OUT` mode. This means that the argument is an output argument and will be used to return values from the function.
        - `b` stands for `INOUT` mode. This means that the argument can be used both to pass values into the function and to return values from the function.
        - `v` stands for `VARIADIC` mode. This means that the argument is a variadic argument, which can accept a variable number of values.
        - `t` stands for `TABLE` mode. This is used for table functions that return a set of rows.
    - _proargnames_ : An array of names of the function's arguments.

6. **Support Functions** :
    - _prosupport_ : The [OID](../key-concepts/#oid) of the support function for this function, if any. Support functions are used by some types to implement certain functionality.

</details>

## pg_roles

The `pg_roles` catalog stores metadata about database roles, including role names, privileges, membership, and login capabilities. This catalog table enables administrators to query and manage user roles, set role-specific permissions, and control database access.

By querying `pg_roles`, administrators can retrieve information about existing roles, view role attributes, and grant or revoke permissions to control data access and security.

<details> <summary>Overview of significant columns.</summary>

1. **Identification** :
    - _rolname_ : The name of the role.
    - _oidvector_ : An array containing the [OIDs](../key-concepts/#oid) of the roles that this role is a member of.

2. **Privilege** :
    - _rolsuper_ : Boolean that signifies if the role can override all access restrictions.
    - _rolinherit_ : Boolean that signifies if the role automatically inherits privileges of roles it is a member of.
    - _rolcreaterole_ : Boolean that signifies if the role can create more roles.
    - _rolcreatedb_ : Boolean that signifies if the role can create databases.
    - _rolcanlogin_ : Boolean that signifies if the role can log in. That is, this role can be given as the initial session authorization identifier.
    - _rolbypassrls_ : Boolean that signifies if the role bypasses row-level security policies.

3. **Password** :
    - _rolpassword_ : If password-based authentication is used, this column will contain the encrypted password.
    - _rolvaliduntil_ : Time until the password is valid.

4. **Settings** :
    - _rolconnlimit_ : Number of connections the role can make.
    - _rolconfig_ : An array containing per-role configuration settings.

</details>

## pg_settings

The `pg_settings` view provides a centralized location for retrieving information about current configuration settings, including database-related parameters and their respective values. It is essentially an alternative interface to the SHOW and SET commands. These parameters can be changed at server start, reload, session, or transaction level.

`pg_settings` allows administrators and developers to inspect and modify runtime settings, such as memory allocation, logging options, connection limits, and performance-related parameters. By querying `pg_settings`, users can dynamically adjust server settings, analyze parameter values, and troubleshoot database performance bottlenecks.

{{<note>}}
The pg_settings view isn't based on underlying tables. Instead, it retrieves information from a combination of sources including the server configuration file, command-line arguments, environment variables, and internal data structures.
{{</note>}}

<details> <summary>Overview of significant columns.</summary>

1. **Identification** :
    - _name_ : The name of the configuration parameter.
    - _setting_ : The current value of the configuration parameter.
    - _unit_ : The unit in which the parameter value is expressed, if any.

2. **Source** :
    - _source_ : The method used to set the value of the parameter. The possible values include:
        - `default` — The current setting is the built-in default.
        - `postgresql.conf` — The value was set from the configuration file postgresql.conf.
        - `startup file` — The value was set from a file specified at server start.
        - `user` — The value was set by a `SET` command, either in the current session or globally.
        - `override` — The value was set by `ALTER SYSTEM` command.
        - `postgres-auto.conf` — The value was set by the system because of an automatic tuning operation.
    - _sourcefile_ : The configuration file in which the parameter was last set.
    - _sourceline_ : The line number in the source file where the parameter was last set.

3. **Characteristics** :
    - _boot_val_ : Initial value of the configuration parameter as set at server start or reset
    - _context_ : This indicates when the parameter can be set. The possible values are:
        - `internal` : Parameter can only be set at server start.
        - `postmaster` : Parameter can be set at server start, or in the `postgresql.conf` file.
        - `sighup` : Parameter can be set at server start, in the `postgresql.conf` file, or with the SIGHUP signal.
        - `superuser` : Parameter can be set by superusers at any time.
        - `user` : Parameter can be set by any user at any time.
    - _enumvals_ : If the parameter is of type `enum`, this is an array of the allowed values.
    - _max_val_ : The maximum allowed value for the parameter. This is `null` for non-numeric parameters.
    - _min_val_ : The minimum allowed value for the parameter. This is `null` for non-numeric parameters.
    - _reset_val_ : The value the parameter would have if RESET were issued.
    - _vartype_ : The type of the parameter's value: `bool`, `integer`, `real`, `string`, `enum`.

4. **Description** :
    - _short_desc_ : A brief description of the configuration parameter.
    - _extra_desc_ : An additional description of the configuration parameter.

</details>

## pg_stat_activity

The `pg_stat_activity` view contains detailed information about active sessions, including process IDs, application names, client addresses, and the SQL statements being executed. This is used to monitor database performance, identify long-running or blocked queries, and diagnose concurrency issues.

Using `pg_stat_activity`, you can monitor connection states, track query execution times, and terminate problematic sessions to optimize database performance.

{{<note>}}
The pg_stat_activity view is not based on any specific tables. Instead, it provides real-time information about the current activity of each session based on internal data structures. This includes information such as the user, current query, state of the query (active, idle, and more), and other session-level information.
{{</note>}}

<details> <summary>Overview of significant columns.</summary>

1. **Process Information** :
    - _datid_ : The [OID](../key-concepts/#oid) of the database this backend is connected to. Null if no database is selected.
    - _datname_ : The name of the database this backend is connected to. Null if no database is selected.
    - _pid_ : Process ID of this backend.
    - _usesysid_ : The [OID](../key-concepts/#oid) of the user who runs this backend.
    - _usename_ : The name of the user who runs this backend.
    - _application_name_ : The name of the application that is connected to this backend.
    - _client_addr_ : The IP address of the client connected to this backend.
    - _client_hostname_ : The resolved hostname of the client connected to this backend. Null if `log_hostname` is off.
    - _client_port_ : The TCP port on the client machine.
    - _backend_start_ : The time when this process was started, that is, when the backend was forked off.

2. **Backend State** :
    - _backend_xid_ : The transaction ID of the current or last completed top-level transaction.
    - _backend_xmin_ : The smallest still-visible XID, that is, the upper bound of the vacuumable XIDs.
    - _state_ : It represents the current overall state of the backend. The possible values for this column are:
        - `active` : The backend is executing a query.
        - `idle` : The backend is waiting for a new client command.
        - `idle in transaction` : The backend is in a transaction, but it is not currently executing a query.
        - `idle in transaction (aborted)` : This state indicates that the backend is in a transaction that has already been aborted, but the backend is still holding on to the transaction.
        - `fastpath function call` : The backend is executing a fast-path function.
        - `disabled` : This state signifies that the information related to backend activity is not currently available because track_activities is turned off in this backend.

3. **Backend Wait Event** :
    - _wait_event_ : The name of the event for which the backend is waiting, if any; otherwise NULL.
    - _wait_event_type_ : This column provides a higher-level grouping of the wait event for which the process is currently waiting, if any. When a backend is not waiting, this field is NULL. Possible values for this column include:
        - `LWLockNamed`: The process is waiting for a named lightweight lock
        - `LWLockTranche`: The process is waiting for a tranche lightweight lock
        - `Lock`: The process is waiting for a heavyweight lock
        - `BufferPin`: The process is waiting for a buffer pin
        - `Activity`: The process is waiting for a client activity
        - `Extension`: The process is waiting for an extension
        - `Client`: The process is waiting for a client
        - `IPC`: The process is waiting for inter-process communication
        - `Timeout`: The process is waiting for a timeout
        - `I/O`: The process is waiting for disk I/O.

4. **Current Query Information** :
    - _query_ : Text of the currently executing query. If the state is `active` then this field shows what the backend is executing. If state is not `active` then this field shows the last executed query.
    - _query_start_ : The time when the current query was started, or null if no query is running.
    - _state_change_ : The time when the state was last changed. It can be used to figure out how long the current process has been running for in its current state.
    - _xact_start_ : The time when the current transaction was started, or null if no transaction is active.

{{<tip>}}
To learn more about how the pg_stat_activity can be used to monitor live queries, see [View live queries](../../explore/observability/pg-stat-activity).
{{</tip>}}

</details>

## pg_stat_all_tables

The `pg_stat_all_tables` view provides insights into various metrics, including the number of rows inserted, updated, deleted, and accessed via sequential or index scans. It enables administrators to assess table-level activity, identify high-traffic tables, and optimize database performance based on usage patterns.

Using `pg_stat_all_tables`, you can evaluate the impact of queries on individual tables, monitor compaction activity, optimize table access patterns, and make informed decisions about indexing and data maintenance tasks.

<details> <summary>Overview of significant columns.</summary>

1. **Identification** :
    - _relid_ : The [OID](../key-concepts/#oid) of the table.
    - _relname_ : The name of the table.
    - _schemaname_ : The name of the schema that contains the table.

2. **Access Statistics** :
    - _idx_scan_ : The number of index scans initiated on this table.
    - _idx_tup_fetch_ : The number of live rows fetched by index scans.
    - _seq_scan_ : The number of sequential scans initiated on this table.
    - _seq_tup_read_ : The number of live rows fetched by sequential scans.

3. **Modification Statistics** :
    - _n_tup_del_ : Number of rows deleted.
    - _n_tup_hot_upd_ : Number of rows HOT updated (Heap-Only Tuple).
    - _n_tup_ins_ : Number of rows inserted.
    - _n_tup_upd_ : Number of rows updated.

4. **Row Statistics** :
    - _n_dead_tup_ : Estimated number of dead rows.
    - _n_live_tup_ : Estimated number of live rows.

5. **Last operation Statistics** :
    - _last_analyze_ : Time of last analyze operation; null if never.
    - _last_autoanalyze_ : Time of last autoanalyze operation; null if never.
    - _last_autovacuum_ : Time of the last autovacuum operation; null if never.
    - _last_vacuum_ : Time of the last vacuum operation; null if never.

6. **Other statistics** :
    - _vacuum_count_ : Number of manual vacuums.
    - _autovacuum_count_ : Number of autovacuums.
    - _analyze_count_ : Number of manual analyzes.
    - _autoanalyze_count_ : Number of autoanalyzes.

</details>

## pg_stat_database

The `pg_stat_database`  view offers insights into various aspects of database utilization, including the number of commits, rollbacks, block reads, and block writes for each database. This data can be used to monitor transaction rates, buffer usage, and I/O activity across databases, facilitating proactive performance tuning and capacity planning.

This view will return one row for each database in the system, plus one row for shared system catalogs (the database [OID](../key-concepts/#oid) is 0 for this row), and one row for each unconnected database.

<details> <summary>Overview of significant columns.</summary>

1. **Identification** :
    - _datid_ : The [OID](../key-concepts/#oid) of the database this row is about.
    - _datname_ : The name of the database this row is about.

2. **Transaction** :
    - _xact_commit_ : The number of transactions in this database that have been committed.
    - _xact_rollback_ : The number of transactions in this database that have been rolled back.

3. **Tuples** :
    - _tup_returned_ : The number of rows returned by queries in this database.
    - _tup_fetched_ : The number of rows fetched by queries in this database.
    - _tup_inserted_ : The number of rows inserted by queries in this database.
    - _tup_updated_ : The number of rows updated by queries in this database.
    - _tup_deleted_ : The number of rows deleted by queries in this database.

4. **I/O** :
    - _blks_read_ : The number of disk blocks read in this database.
    - _blks_hit_ : The number of buffer hits in this database.

5. **Connection Information** :
    - _numbackends_ : The number of backends currently connected to this database.

6. **Activities** :
    - _last_autovacuum_ : The last time at which any table in the database was auto-vacuumed. For auto-vacuum, this is the end time.
    - _last_autoanalyze_ : The last time at which any table in the database was auto-analyzed. For auto-analyze, this is the end time.

7. **Miscellaneous Information** :
    - _deadlocks_ : The number of deadlocks detected in this database.
    - _temp_files_ : The number of temporary files created by queries in this database. All temporary files are counted, regardless of why the temporary file was created (for example, sorting or hashing), and regardless of the log_temp_files setting.
    - _temp_bytes_ : The total amount of data written to temporary files by queries in this database. All temporary files are counted, regardless of why the temporary file was created.
    - _conflicts_ : The number of queries canceled due to conflicts with recovery in this database. Conflicts occur only on standby servers.
    - _stats_reset_ : The time at which these statistics were last reset.

</details>

## pg_stat_statements

The `pg_stat_statements` extension provides detailed statistical insights into SQL query performance by tracking query execution statistics over time. It records metrics such as query execution counts, total runtime, average runtime, and resource consumption (for example, CPU time, I/O) for individual SQL statements. Using `pg_stat_statements`, you can prioritize optimization efforts based on query frequency and resource consumption, improving overall database efficiency and response times.

{{<note>}}
By default, only _min_, _max_, _mean_ and _stddev_ of the execution times are associated with a query. This has proved insufficient to debug large volumes of queries. To get a better insight, YugabyteDB introduces an additional column, [yb_latency_histogram](../../explore/query-1-performance/pg-stat-statements#yb_latency_histogram-column) that stores a list of latency ranges and the number of query executions in that range.
{{</note>}}

{{<tip>}}
To understand how to improve query performance using these stats, see [Query tuning](../../explore/query-1-performance/pg-stat-statements/)
{{</tip>}}

<details> <summary>Overview of significant columns.</summary>

1. **Identification**:
    - _userid_ : The [OID](../key-concepts/#oid) of the user who executed the statement. This can be joined with the pg_authid system catalog table to get more information about the user.
    - _dbid_ : The [OID](../key-concepts/#oid) of the database in which the statement was executed. This can be joined with the [pg_database](#pg_database) table to get more information about the database.
    - _queryid_ : The hash code identifying the statement. This is unique in a single server run.
    - _query_ : The text of a representative statement.

2. **Execution Counts**:
    - _calls_ : The total number of times the statement was executed.
    - _rows_ : The total number of rows retrieved or affected by the statement.

3. **Time Statistics**:
    - _total_time_ : The total time spent in the statement, in milliseconds.
    - _min_time_ : The minimum time spent in the statement, in milliseconds.
    - _max_time_ : The maximum time spent in the statement, in milliseconds.
    - _mean_time_ : The mean time spent in the statement, in milliseconds.
    - _stddev_time_ : The standard deviation of the time spent in the statement, in milliseconds.
    - **_yb_latency_histogram_** : List of latency ranges and the number of query executions in that range. For details, see [yb_latency_histogram](../../explore/query-1-performance/pg-stat-statements#yb_latency_histogram-column)

4. **Block I/O**:
    - _shared_blks_hit_ : The total number of shared block cache hits by the statement.
    - _shared_blks_read_ : The total number of shared blocks read by the statement.
    - _shared_blks_dirtied_ : The total number of shared blocks dirtied by the statement.
    - _shared_blks_written_ : The total number of shared blocks written by the statement.
    - _local_blks_hit_ : The total number of local block cache hits by the statement.
    - _local_blks_read_ : The total number of local blocks read by the statement.
    - _local_blks_dirtied_ : The total number of local blocks dirtied by the statement.
    - _local_blks_written_ : The total number of local blocks written by the statement.
    - _temp_blks_read_ : The total number of temp blocks read by the statement.
    - _temp_blks_written_ : The total number of temp blocks written by the statement.
    - _blk_read_time_ : The total time the statement spent reading blocks, in milliseconds (if track_io_timing is enabled, otherwise zero).
    - _blk_write_time_ : The total time the statement spent writing blocks, in milliseconds (if track_io_timing is enabled, otherwise zero).

</details>

## pg_tables

The `pg_tables` provides essential information about table names, schema names, owner details, and other table attributes. This view is valuable for database administrators and developers to inspect and manage tables, including checking table ownership, and permissions, verifying table ownership, and understanding the overall table structure.

<details> <summary>Overview of significant columns.</summary>

1. **Identification** :
    - _schemaname_  :  The name of the schema that contains the table.
    - _tablename_  :  The name of the table.
    - _tableowner_  : The name of the user who owns the table.

1. **Related objects and features** :
    - _tablespace_  : The name of the tablespace in which the table is stored.
    - _hasindexes_  : Flag indicating whether the table has any indexes.
    - _hasrules_  : Flag indicating whether there are rewrite rules on the table.
    - _hastriggers_  : Flag indicating whether there are triggers on the table.
    - _rowsecurity_  : Flag indicating whether row-level security is enabled on the table.

</details>

## pg_views

The `pg_views` view contains information about the views in the database. It is based on the [pg_class](#pg-class) and [pg_namespace](#pg-namespace) tables.

The `pg_views` view stores information about view names, associated schemas, view definitions, and view owners, enabling users to query and manage views effectively. This can be used to understand the structure and properties of existing views, verify view definitions, ownership details, and analyze view dependencies.

<details> <summary>Overview of significant columns.</summary>

1. **Identification** :
    - _schemaname_  :  The name of the schema that contains the view.
    - _viewname_  :  The name of the view.
    - _viewowner_  :  The username of the role that owns the view.

2. **Definition** :
    - _definition_  :  The query expression that defines the view.

</details>

## Other tables and views
|           Name           | Purpose |
| -----------------------: | ------- |
| pg_aggregate             | Stores information about aggregate functions, including their names, owner, and associated transition functions used to compute the aggregates.        |
| pg_am                    | Defines available access methods, such as lsm, hash, ybgin, and more, providing crucial details like their names and supported functions.        |
| pg_amop                  | Associates access methods with their supported operators, detailing the operator families and the strategy numbers.        |
| pg_amproc                | Associates access methods with their supported procedures, detailing the operator families and the procedures used for various operations within the access method.        |
| pg_attrdef               | Stores default values for columns, containing information about which column has a default, and the actual default expressions.        |
| pg_authid                | Stores information about database roles, including role names, passwords, and privileges.        |
| pg_auth_members          | Records the membership of roles, specifying which roles are members of other roles along with the associated administrative options.        |
| pg_cast                  | Lists available casting rules, specifying which data types can be cast to which other data types and the functions used for casting.        |
| pg_collation             | Records collations available for sorting and comparing string values, specifying names, encodings, and source providers.        |
| pg_conversion            | Stores information on encoding conversions, detailing names, source and destination encodings, and functions used for the conversion.        |
| pg_db_role_setting       | Saves customized settings per database and per role, specifying which settings are applied when a certain role connects to a specific database.        |
| pg_default_acl           | Defines default access privileges for new objects created by users, specifying the type of object and the default privileges granted.        |
| pg_depend                | Tracks dependencies between database objects to ensure integrity, such as which objects rely on others for their definition or operation.        |
| pg_description           | Stores descriptions for database objects, allowing for documentation of tables, columns, and other objects.        |
| pg_enum                  | Manages enumerations, recording labels for enum types and their associated internal values.        |
| pg_event_trigger         | Keeps track of event triggers, detailing the events that invoke them and the functions they call.        |
| pg_extension             | Manages extensions, storing data about installed extensions, their versions, and the custom objects they introduce.        |
| pg_foreign_data_wrapper  | Lists foreign-data wrappers, detailing the handlers and validation functions used for foreign data access.        |
| pg_foreign_server        | Documents foreign servers, providing connection options and the foreign-data wrapper used.        |
| pg_foreign_table         | Catalogs foreign tables, displaying their server associations and column definitions.        |
| pg_inherits              | Records inheritance relationships between tables, indicating which tables inherit from which parents.        |
| pg_init_privs            | Captures initial privileges for objects when they are created, used for reporting and restoring original privileges.        |
| pg_language              | Lists available programming languages for stored procedures, detailing the associated handlers.        |
| pg_largeobject           | Manages large objects, storing the actual chunks of large binary data in a piecewise fashion.        |
| pg_largeobject_metadata  | Stores metadata about large objects, including ownership and authorization information.        |
| pg_opclass               | Defines operator classes for access methods, specifying how data types can be used with particular access methods.        |
| pg_operator              | Defines available operators in the database, specifying their behavior with operands and result types.        |
| pg_opfamily              | Organizes operator classes into families for compatibility in access methods        |
| pg_partitioned_table     | Catalogs partitioning information for partitioned tables, including partitioning strategies.        |
| pg_policy                | Enforces row-level security by defining policies on tables for which rows are visible or modifiable per role.        |
| pg_publication           | Manages publication sets for logical replication, specifying which tables are included.        |
| pg_publication_rel       | Maps publications to specific tables in the database, assisting replication setup.        |
| pg_range                 | Defines range types, mapping subtypes and their collation properties.        |
| pg_replication_origin    | Tracks replication origins, aiding in monitoring and managing data replication across systems.        |
| pg_rewrite               | Manages rewrite rules for tables, detailing which rules rewrite queries and the resulting actions.        |
| pg_seclabel              | Applies security labels to database objects, connecting them with security policies for fine-grained access control.        |
| pg_sequence              | Describes sequences, recording properties like increment and initial values.        |
| pg_shdepend              | Tracks shared dependency relationships across databases to maintain global database integrity.        |
| pg_shdescription         | Provides descriptions for shared objects, enhancing cross-database object documentation.        |
| pg_shseclabel            | Associates security labels with shared database objects, furthering security implementations across databases.        |
| pg_statistic             | Collects statistics on database table contents, aiding query optimization with data distributions and other metrics.        |
| pg_statistic_ext         | Organizes extended statistics about table columns for more sophisticated query optimization.        |
| pg_statistic_ext_data    | Stores actual data related to extended statistics, providing a base for advanced statistical calculations.        |
| pg_subscription          | Manages subscription information for logical replication, including subscription connections and replication sets.        |
| pg_tablespace            | Lists tablespaces, specifying storage locations for database objects to aid in physical storage organization.        |
| pg_transform             | Manages transforms for user-defined types, detailing type conversions to and from external formats.        |
| pg_trigger               | Records triggers on tables, specifying the trigger behavior and associated function execution.        |
| pg_ts_config             | Documents text search configurations, laying out how text is processed and searched.        |
| pg_ts_config_map         | Maps tokens to dictionaries within text search configurations, directing text processing.        |
| pg_ts_dict               | Catalogs dictionaries used in text searches, detailing the options and templates used for text analysis.        |
| pg_ts_parser             | Describes parsers for text search, specifying tokenization and normalization methods.        |
| pg_ts_template           | Outlines templates for creating text search dictionaries, providing a framework for text analysis customization.        |
| pg_type                  | Records data types defined in the database, detailing properties like internal format and size.        |
| pg_user_mapping          | Manages mappings between local and foreign users, facilitating user authentication and authorization for accesses to foreign servers.        |

## Learn more

- [List of PostgreSQL system tables](https://www.postgresql.org/docs/current/catalogs.html)