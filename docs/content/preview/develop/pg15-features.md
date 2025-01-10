---
title: PostgreSQL 15 Features
linkTitle: PG15 features
description: Use PostgreSQL 15 features in your applications
menu:
  preview:
    identifier: yb-postgresql-15
    parent: develop
    weight: 700
type: docs
rightNav:
  hideH3: true
---

YugabyteDB has been a compatible with [PostgreSQL-11](https://www.yugabyte.com/tech/postgres-compatibility/) for a long time. With {{<release "2.25">}}, YugabyteDB brings in many of the PG-15 features into the world of distributed datases. Some of the significant features and improvements are listed below.

## Major features

{{%table%}}

| Feature | Description |
| --------| ----------- |

| [Stored Generated Columns](../../api/ysql/the-sql-language/statements/ddl_create_table#stored-generated-columns)
| Define columns whose values are automatically calculated and stored based on expressions involving other columns
|

|[Before row triggers on partitioned tables](../../api/ysql/the-sql-language/statements/ddl_create_trigger#partitioned-tables)
| Enforce custom business logic, perform validation, or even modify row data before it is written to the appropriate partition
|

| [Non distinct NULLs in a Unique index](../../api/ysql/the-sql-language/statements/ddl_create_index#nulls-not-distinct)
| Enforce that only one NULL value is permitted instead of allowing multiple NULL entries in a unique index
|

| [COPY FROM with WHERE clause](../../api/ysql/the-sql-language/statements/cmd_copy#where)
| Selectively filter rows while importing data into a table and eliminate preprocessing data files
|

| [UUID generation](../../api/ysql/exprs/func_gen_random_uuid/)
| By providing `gen_random_uuid` natively, the need for external libraries or custom implementations for UUID generation is eliminated.
|

| [Foreign Keys on Partitioned Tables](../../explore/ysql-language-features/advanced-features/partitions/#foreign-key-references)
| Enforce referential integrity directly on partitioned tables, ensuring consistency across large-scale datasets that benefit from partitioning for performance and scalability.
|

| [Multi-range and Range Aggregates](../../api/ysql/datatypes/type_range/#multirange)
| Simplifies handling complex range-based data by grouping multiple discrete ranges together while maintaining their distinctness
|

| [Pipeline mode](https://www.postgresql.org/docs/14/libpq-pipeline-mode.html)
| Enables a client to send multiple queries to the server without waiting for each query's response before sending the next one. This reduces the round-trip time between client and server and significantly boosts the performance when many queries in quick succession
|

| [Replace triggers](../../api/ysql/the-sql-language/statements/ddl_create_trigger#or-replace)
| Replace an existing trigger with a new one without first having to drop the old trigger and create a new one
|

{{%/table%}}

## Query Execution Optimizations

{{%table%}}

| Feature | Description |
| --------| ----------- |

| [Incremental Sort](../../architecture/query-layer/#optimizations)
| On a sorted resultset, additional sorting in done on the remaining keys |

| [Memoization](../../architecture/query-layer/#optimizations)
| Store results in memory when the inner side of a nested-loop join is small |

| [Disk-based Hash Aggregation](../../architecture/query-layer/#optimizations)
| Opt for disk-based aggregation when hash table grows beyond `work_mem` |

{{%/table%}}

## Observability

{{%table%}}

| Feature | Description |
| --------| ----------- |

| [Query ID](../../architecture/query-layer/#query-id)
| Unique query ID to track a query across pg_stat_activity, EXPLAIN VERBOSE, and pg_stat_statement |

| [Stats on planning times](../../explore/query-1-performance/pg-stat-statements/)
| `pg_stat_statements` can now track the planning time of statements |

| [Granular stats reset](../../explore/query-1-performance/pg-stat-statements/#reset-statistics)
| Resetting statistics via `pg_stat_statements_reset` is now granular at user, database, and query levels |

| [Sampled logging](../../explore/observability/logging/#log-management)
| Log a fraction of the statements rather than all statements |

{{%/table%}}

## Security

{{%table%}}

| Feature | Description |
| --------| ----------- |

| [Stricter public schema](../../api/ysql/the-sql-language/statements/dcl_create_user/#public-schema-privileges)
| CREATE Privilege for PUBLIC Role has been revoked ensuring tighter control over object creation, reducing the risk of accidental or malicious interference in shared schemas.|

| [Simplified privilege management](../../api/ysql/the-sql-language/statements/dcl_grant/#predefined-roles)
| New roles have been added to streamline permission assignments by grouping commonly needed privileges for read and write operations|

{{%/table%}}

## Features coming soon

The following PG15 features are not yet implemneted but will be released in the future.

{{%table%}}

| Feature | Description |
| --------| ----------- |

| [Extended statistics](https://www.postgresql.org/docs/15/planner-stats.html#PLANNER-STATS-EXTENDED)
| Gather additional statistics using the [CREATE STATISTICS](https://www.postgresql.org/docs/15/sql-createstatistics.html) command|

| [Merge command](https://www.postgresql.org/docs/current/sql-merge.html)
| INSERT, UPDATE or DELETE in one statement |

| [Scram authentication as default](https://www.postgresql.org/docs/15/auth-password.html)
| Scram authentication is [supported](../../secure/authentication/password-authentication/#scram-sha-256) in YugabyteDB but still has md5 as default authentication method |

| [Nondeterministic Collations](https://www.postgresql.org/docs/12/collation.html#COLLATION-NONDETERMINISTIC)
| Consider strings to be equal even if they consist of different bytes, eg : case-insensitive, or accent-insensitive comparisons |

{{%/table%}}
