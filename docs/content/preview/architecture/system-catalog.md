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

System catalogs, also known as system tables or system views, play a crucial role in the internal organization and management of the database and serve as the backbone of YugabyteDB's architecture. YugabyteDB builds upon the system catalog of [PostgreSQL](https://www.postgresql.org/docs/current/catalogs.html). These catalogs form a centralized repository that stores metadata about the database itself, such as tables, indexes, columns, constraints, functions, users, privileges, extensions, query statistics, and more. All the system catalog tables and views are organized under the _pg_catalog_ schema.

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

{{<note title="information_schema">}}
In most cases, developers and applications interact with _information_schema_ for querying database metadata in a portable manner, while pg_catalog is primarily used for advanced PostgreSQL administration and troubleshooting tasks.

_information_schema_ provides a standardized, SQL-compliant view of database metadata that is portable across different database systems, while pg_catalog offers detailed, PostgreSQL-specific system catalogs for internal database operations and management.
{{</note>}}

Let us look at some of the most important system catalog tables and views in detail followed by a summary of other members.

## pg_attribute

The _pg_attribute_ table stores metadata about the columns (attributes) of all relations (tables, views, indexes, and so on) in the database. It is extensively used by the query optimizer to generate efficient execution plans, by the database engine to enforce data integrity rules, and by administrators for tasks such as database documentation, maintenance, and troubleshooting.

## pg_class

The _pg_class_ table serves as a comprehensive repository for storing metadata about all relations (tables, views, indexes, sequences, and other relation types) in the database. It is used to generate efficient execution plans, by storage managers to handle data files and disk usage, and by administrators for tasks such as monitoring, tuning, and maintaining the database.

## pg_constraint

The _pg_constraint_ table stores information about constraints on tables. Constraints are rules that specify certain conditions that the data in a table must fulfill. These can include unique constraints, check constraints, primary key constraints, and foreign key constraints.

## pg_database

The _pg_database_ table serves as a centralized repository for database metadata, including database names, encoding, and other properties. It facilitates tasks such as database configuration, user access control, and cross-database operations.

## pg_index

The _pg_index_ table provides crucial insights into indexing strategies and query optimization. It stores metadata about indexes, including details such as the indexed columns, index types, and index properties like uniqueness and inclusion of nullable values.
This data can be used to make informed decisions about index maintenance, creation, or modification based on workload characteristics and query requirements.

## pg_locks

The _pg_locks_ view provides detailed information about current locks held by active transactions, including lock types (for example, shared, exclusive), lock modes, and the associated database objects being locked. This view can be used to monitor lock escalation, detect long-running transactions holding locks, and optimize transaction isolation levels to minimize lock contention and improve database concurrency.

{{<note>}}
The pg_locks view doesn't have a documented view definition that you can directly inspect in the database. This is because the view definition relies on internal data structures used by the lock manager, and these structures aren't intended for direct user access.
{{</note>}}

{{<tip>}}
[pg_locks](#pg-locks) view can be joined to [pg_stat_activity](#pg-stat-activity) view on the _pid_ column to get more information on the session holding or awaiting each lock. To learn more about how the pg_locks can be used to get insights on transaction locks, see [Lock insights](../../explore/observability/pg-locks).
{{</tip>}}

## pg_namespace

The _pg_namespace_ catalog stores metadata about schemas, including schema names, owner information, and associated privileges. By querying _pg_namespace_, users can retrieve information about existing schemas, verify ownership, and grant or revoke privileges at the schema level.

## pg_proc

The _pg_proc_ catalog stores metadata about database procedures, including their names, argument types, return types, source code, and associated permissions. It enables developers and administrators can inspect function definitions, review function dependencies, and monitor usage statistics to optimize query performance and database operations.

## pg_roles

The _pg_roles_ catalog stores metadata about database roles, including role names, privileges, membership, and login capabilities. This catalog table enables administrators to query and manage user roles, set role-specific permissions, and control database access.

## pg_settings

The _pg_settings_ view provides a centralized location for retrieving information about current configuration settings, including database-related parameters and their respective values. It is essentially an alternative interface to the SHOW and SET commands. These parameters can be changed at server start, reload, session, or transaction level. _pg_settings_ allows administrators and developers to inspect runtime settings, such as memory allocation, logging options, connection limits, and performance-related parameters.

{{<note>}}
The pg_settings view isn't based on underlying tables. Instead, it retrieves information from a combination of sources including the server configuration file, command-line arguments, environment variables, and internal data structures.
{{</note>}}

## pg_stat_activity

The _pg_stat_activity_ view shows detailed information about active sessions, including process IDs, application names, client addresses, and the SQL statements being executed. This is used to monitor database performance, identify long-running or blocked queries, and diagnose concurrency issues.

{{<note>}}
The pg_stat_activity view is not based on any specific tables. Instead, it provides real-time information about the current activity of each session based on internal data structures. This includes information such as the user, current query, state of the query (active, idle, and more), and other session-level information.
{{</note>}}

{{<tip>}}
To learn more about how the pg_stat_activity can be used to monitor live queries, see [View live queries](../../explore/observability/pg-stat-activity).
{{</tip>}}

## pg_stat_all_tables

The _pg_stat_all_tables_ view provides insights into various metrics, including the number of rows inserted, updated, deleted, and accessed via sequential or index scans. It enables administrators to assess table-level activity, identify high-traffic tables, and optimize database performance based on usage patterns.

## pg_stat_database

The _pg_stat_database_  view offers insights into various aspects of database utilization, including the number of commits, rollbacks, block reads, and block writes for each database. This data can be used to monitor transaction rates, buffer usage, and I/O activity across databases, facilitating proactive performance tuning and capacity planning.

This view will return one row for each database in the system, plus one row for shared system catalogs (the database [OID](../key-concepts/#oid) is 0 for this row), and one row for each unconnected database.

## pg_stat_statements

The _pg_stat_statements_ view provides detailed statistical insights into SQL query performance by tracking query execution statistics over time. It records metrics such as query execution counts, total runtime, average runtime, and resource consumption (for example, CPU time, I/O) for individual SQL statements. Using _pg_stat_statements_, you can prioritize optimization efforts based on query frequency and resource consumption, improving overall database efficiency and response times.

{{<note>}}
By default, only _min_, _max_, _mean_, and _stddev_ of the execution times are associated with a query. This has proved insufficient to debug large volumes of queries. To get a better insight, YugabyteDB introduces an additional column, [yb_latency_histogram](../../explore/query-1-performance/pg-stat-statements#yb-latency-histogram-column), that stores a list of latency ranges and the number of query executions in that range.
{{</note>}}

{{<tip>}}
To understand how to improve query performance using these stats, see [Query tuning](../../explore/query-1-performance/pg-stat-statements/)
{{</tip>}}

## pg_tables

The _pg_tables_ view provides essential information about table names, schema names, owner details, and other table attributes. This view is valuable for database administrators and developers to inspect and manage tables, including checking table ownership, and permissions, verifying table ownership, and understanding the overall table structure.

## pg_views

The _pg_views_ view provides information about view names, associated schemas, view definitions, and view owners. It is based on the [pg_class](#pg-class) and [pg_namespace](#pg-namespace) tables. This can be used to understand the structure and properties of existing views, verify view definitions, and ownership details, and analyze view dependencies.

## Other tables

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