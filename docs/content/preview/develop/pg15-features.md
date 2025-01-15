---
title: PostgreSQL 15 features
headerTitle: PostgreSQL 15 features
linkTitle: PG15 features
description: Use PostgreSQL 15 features in your applications
headContent: Use PostgreSQL 15 features in your applications
menu:
  preview:
    identifier: yb-postgresql-15
    parent: develop
    weight: 700
type: docs
---

The YugabyteDB [YSQL API](../../api/ysql/) reuses a fork of the PostgreSQL [query layer](../../architecture/query-layer/). This architecture allows YSQL to support most PostgreSQL features, such as data types, queries, expressions, operators and functions, stored procedures, triggers, extensions, and so on, all of which are expected to work identically on both database systems.

Initially based on PostgreSQL 11.2, YSQL has been continuously enhanced with features, improvements, and security fixes pulled from PostgreSQL 12+, in addition to our own improvements. With {{<release "2.25">}}, YugabyteDB now adds support for PostgreSQL 15, which brings many new features and improvements.

{{<lead link="https://www.yugabyte.com/blog/yugabytedb-moves-beyond-postgresql-11/">}}
Learn more about the [journey to PostgreSQL 15](https://www.yugabyte.com/blog/yugabytedb-moves-beyond-postgresql-11/).
{{</lead>}}

## What's new

### Major features

{{%table%}}

| Feature | Description |
| --------| ----------- |

| [Stored generated columns](../../api/ysql/the-sql-language/statements/ddl_create_table#stored-generated-columns)
| Define columns whose values are automatically calculated and stored based on expressions involving other columns.
|

|[Before&nbsp;row&nbsp;triggers&nbsp;on partitioned tables](../../api/ysql/the-sql-language/statements/ddl_create_trigger#partitioned-tables)
| Enforce custom business logic, perform validation, or even modify row data before it is written to the appropriate partition.
|

| [Non distinct NULLs in a Unique index](../../api/ysql/the-sql-language/statements/ddl_create_index#nulls-not-distinct)
| Enforce that only one NULL value is permitted instead of allowing multiple NULL entries in a unique index.
|

| [COPY FROM with WHERE clause](../../api/ysql/the-sql-language/statements/cmd_copy#where)
| Selectively filter rows while importing data into a table and eliminate preprocessing data files.
|

| [UUID generation](../../api/ysql/exprs/func_gen_random_uuid/)
| Provide `gen_random_uuid` natively, eliminating the need for external libraries or custom implementations to generate UUIDs.
|

| [Foreign keys on partitioned tables](../../explore/ysql-language-features/advanced-features/partitions/#foreign-key-references)
| Enforce referential integrity directly on partitioned tables, ensuring consistency across large-scale datasets that benefit from partitioning for performance and scalability.
|

| [Multi-range and range aggregates](../../api/ysql/datatypes/type_range/#multirange)
| Simplify handling complex range-based data by grouping multiple discrete ranges together while maintaining their distinctness.
|

| [Pipeline mode](https://www.postgresql.org/docs/14/libpq-pipeline-mode.html)
| Enable a client to send multiple queries to the server without waiting for each query's response before sending the next one. This reduces the round-trip time between client and server and significantly boosts the performance when running many queries in quick succession.
|

| [Replace triggers](../../api/ysql/the-sql-language/statements/ddl_create_trigger#or-replace)
| Replace an existing trigger with a new one without first having to drop the old trigger and create a new one.
|

{{%/table%}}

### Query execution optimizations

{{%table%}}

| Feature | Description |
| --------| ----------- |

| [Incremental sort](../../architecture/query-layer/#optimizations)
| On a sorted result set, perform additional sorting on the remaining keys. |

| [Memoization](../../architecture/query-layer/#optimizations)
| Store results in memory when the inner side of a nested-loop join is small. |

| [Disk-based hash aggregation](../../architecture/query-layer/#optimizations)
| Opt for disk-based aggregation when hash table grows beyond `work_mem`. |

{{%/table%}}

### Observability

{{%table%}}

| Feature | Description |
| --------| ----------- |

| [Query ID](../../architecture/query-layer/#query-id)
| Unique query ID to track a query across pg_stat_activity, EXPLAIN VERBOSE, and pg_stat_statements. |

| [Stats on planning times](../../explore/query-1-performance/pg-stat-statements/)
| pg_stat_statements can now track the planning time of statements. |

| [Granular stats reset](../../explore/query-1-performance/pg-stat-statements/#reset-statistics)
| Resetting statistics via `pg_stat_statements_reset` is now granular at user, database, and query levels. |

| [Sampled logging](../../explore/observability/logging/#log-management)
| Log a fraction of the statements rather than all statements. |

{{%/table%}}

### Security

{{%table%}}

| Feature | Description |
| --------| ----------- |

| [Stricter public schema](../../api/ysql/the-sql-language/statements/dcl_create_user/#public-schema-privileges)
| CREATE Privilege for PUBLIC Role has been revoked ensuring tighter control over object creation, reducing the risk of accidental or malicious interference in shared schemas.|

| [Simplified&nbsp;privilege management](../../api/ysql/the-sql-language/statements/dcl_grant/#predefined-roles)
| New roles have been added to streamline permission assignments by grouping commonly needed privileges for read and write operations. |

{{%/table%}}

### Coming soon

The following PG15 features are not yet implemented but are planned for the future.

{{%table%}}

| Feature | Description |
| --------| ----------- |

| [Extended statistics](https://www.postgresql.org/docs/15/planner-stats.html#PLANNER-STATS-EXTENDED)
| Gather additional statistics using the [CREATE STATISTICS](https://www.postgresql.org/docs/15/sql-createstatistics.html) command. |

| [Merge command](https://www.postgresql.org/docs/current/sql-merge.html)
| INSERT, UPDATE or DELETE in one statement. |

| [Scram authentication as default](https://www.postgresql.org/docs/15/auth-password.html)
| Scram authentication is [supported](../../secure/authentication/password-authentication/#scram-sha-256) in YugabyteDB but still has md5 as default authentication method. |

| [Nondeterministic collations](https://www.postgresql.org/docs/12/collation.html#COLLATION-NONDETERMINISTIC)
| Consider strings to be equal even if they consist of different bytes, for example, case-insensitive, or accent-insensitive comparisons. |

{{%/table%}}
