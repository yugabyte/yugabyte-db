---
title: Export PostgreSQL logs from YugabyteDB Managed clusters
headerTitle: Export logs
linkTitle: Export logs
description: Export cluster metrics to third-party tools.
headcontent: Export PostgreSQL logs from YugabyteDB Managed clusters
menu:
  preview_yugabyte-cloud:
    identifier: export-logs
    parent: cloud-monitor
    weight: 610
type: docs
---

Export YSQL database logs to third-party tools for security monitoring, to build operations and health dashboards, troubleshooting, and more. You can export the following types of logs:

- Database query logging. This is the standard [PostgreSQL logging](https://www.postgresql.org/docs/11/runtime-config-logging.html) facility. Using these settings you can log query statements and errors.
<!-- Database audit logging. Using the [PostgreSQL Audit Extension](https://www.pgaudit.org/#) ([pgaudit](https://github.com/pgaudit/pgaudit/blob/1.3.2/README.md)), the audit log provides the exact database transactions, which is a compliance requirement for government, financial, or ISO certifications.
-->
Note that YugabyteDB is based on PostgreSQL 11<!-- and uses pgaudit v1.3.2-->.

Exporting logs may incur additional costs for network transfer in a cloud region, between cloud regions, and across the Internet. Refer to [Data transfer costs](../../cloud-admin/cloud-billing-costs/#data-transfer-costs).

## Prerequisites

Create an integration. An integration defines the settings and login information for the tool that you want to export your logs to. Refer to [Integrations](../managed-integrations).

## Recommendations

- Configuring logging [requires a restart](../../cloud-clusters/#locking-operations) of your cluster. Configure logging when the cluster isn't experiencing heavy traffic.
- Configuring logging blocks other cluster operations, such as backups and maintenance. Avoid changing your settings before maintenance windows and during scheduled backups. The operation will block a backup from running.
- To limit performance impact and control costs, log and export only what you need. The default settings are based on best practices from open source PostgreSQL, the broader community, and YugabyteDB Managed testing to ensure the impact is bounded and insignificant.

## Database Query Logging

To enable database query logging for a cluster, do the following:

1. On the cluster **Settings** tab, select **Database Query Logging**.
1. Click **Enable Database Query Logging**.
1. Set the [logging settings](#logging-settings).
1. Select the [export configuration](../managed-integrations/) to use.
1. Click **Enable YSQL Query Logging**.

YugabyteDB Managed begins the (rolling) restart.

Logs are exported to the third-party tool in near real time. After the setup is complete and YSQL database queries are submitted, verify that the YSQL database query logs are visible in the tool; they should be available in minutes. Logs are exported with preset tags and a [log line prefix](#include-in-the-log-prefix-log_line_prefix) so that you can filter them further by cloud, region, availability zone, cluster_id, node-type, and node-name. Depending on your tool, you can also perform text searches of the logs.

### Logging settings

Database query logging provides access to the following subset of the standard [PostegreSQL logging settings](https://www.postgresql.org/docs/11/runtime-config-logging.html).

##### Log SQL statements (log_statement)

Turn this option on to log SQL statements by type. You can choose the following options:

- ddl - log data definition statements CREATE, ALTER, and DROP.
- mod - in addition to ddl statements, log data-modifying statements INSERT, UPDATE, DELETE, TRUNCATE, and COPY FROM.
- all - log all statements.

Statements that fail before the execute phase, or that have syntax errors, are not included; to log error statements, use [Log SQL statements with severity](#log-sql-statements-with-severity-log-min-error-statement).

Note that if this option is off, statements may still be logged, depending on the other logging settings.

##### Include in the log prefix (log_line_prefix)

Add metadata, such as the user or database name, to the start of each log line. This is applied to logs captured on YugabyteDB nodes and exported to your monitoring dashboard.

To build the prefix, click **Edit** to open the **Edit Log Line Prefix** dialog. To add prefix items, click **Add Prefix** and choose the prefix items; these can also include punctuation. Click and drag items added to the log line prefix to arrange them in the order you want in the log.

| Prefix | Description | Default |
| :--- | :--- | :--- |
| %p | Process ID | Always on |
| %t | Timestamp of the log | Always on |
| %e | SQLSTATE error code | off |
| %r | Remote hostname or IP address, and remote port | on |
| %a | Application name | off |
| %u | Username | on |
| %d | Database name | on |
| : | Colon |  |
| [] | Brackets |  |
| () | Parentheses |  |
| @ | Ampersand |  |

The default prefix is as follows:

```output
%m : %r : %u @ %d :[%p]:

timestamp : remote hostname and port : username@database : [process ID]:
```

##### Log SQL statements with severity (log_min_error_statement)

Controls which SQL statements that cause an error condition are logged. The current SQL statement is included in the log entry for any message of the specified severity or higher. This parameter is set to ERROR, which means statements causing errors, log messages, fatal errors, or panics are logged.

##### Set verbosity (log_error_verbosity)

Set the amount of detail for each log statement. Valid values are TERSE, DEFAULT, and VERBOSE, each adding more fields to displayed messages. TERSE excludes the logging of DETAIL, HINT, QUERY, and CONTEXT error information. VERBOSE output includes the SQLSTATE error code and the source code file name, function name, and line number that generated the error.

##### Log the duration of all completed statements (log_duration)

Log the duration of all completed statements. Statement text is not included. Use this option with the following option to log all durations, and the statement text for statements exceeding a specified duration. Use this option for performance analysis.

##### Log all statements with duration (log_min_duration_statement)

Log the duration and statement text of all statements that ran for the specified duration (in ms) or longer. Use this setting to identify slow queries. If a statement has been logged for [Log SQL statements](#log-sql-statements-log-statement), the text is not repeated in the duration log message.

Setting this option to 0 logs all statements, with their duration, which is not recommended unless you have low traffic. You should set this to a reasonable value for your application (for example, 1000 milliseconds)<!--, or use [log sampling](#sample-statements-with-duration-log_min_duration_sample-and-log_statement_sample_rate).This setting overrides [the sampling setting](#sample-statements-with-duration-log_min_duration_sample-and-log_statement_sample_rate); queries exceeding the minimum duration are not subject to sampling and are always logged -->.

<!--
##### Sample statements with duration (log_min_duration_sample and log_statement_sample_rate)

Log a sampling of statements that ran for a specified duration (in ms) or longer. These options are used together, typically to identify slow queries while minimizing the performance impact on high traffic clusters.

For example, to log 25% of queries exceeding 1000ms, set the sample rate to 25 per cent, and set the duration to 1000.

When duration is off, the sample rate has no effect.
-->

##### Log the internal representation of the query plan (debug_print_plan)

Log the debug-level execution plan used by the parser. Used for debugging. Not recommended for production.

##### Log connections (log_connections)

Log all connection attempts, along with successfully completed client authentication and authorization.

##### Log disconnections (log_disconnections)

Log session termination and duration of the session.

<!--
## Database Audit Log

To enable database audit logging for a cluster, do the following:

1. On the cluster **Settings** tab, select **Database Audit Log**.
1. Click **Enable Database Audit Logging**.
1. Select the YSQL statements to log.

    - **Read** - SELECT and COPY when the source is a relation or a query.
    - **Write** - INSERT, UPDATE, DELETE, TRUNCATE, and COPY when the destination is a relation.
    - **Function** - Function calls and DO blocks.
    - **Role** - Statements related to roles and privileges: GRANT, REVOKE, and CREATE/ALTER/DROP ROLE.
    - **DDL** - All DDL that is not included in the ROLE class.
    - **Misc** - Miscellaneous commands, such as DISCARD, FETCH, CHECKPOINT, VACUUM, and SET.

1. Configure the [YSQL audit log settings](#ysql-audit-log-settings).

1. Select the export configuration to use.

1. Click **Enable Database Audit Logging**.

YugabyteDB Managed begins the rolling restart.

### YSQL audit log settings

The YSQL audit logging settings are derived from the settings for logging used by the pgaudit extension. Statements are always logged.

| Option | Description | Default |
| :----- | :----- | :------ |
| pgaudit.log_catalog | Log statements for the PostgreSQL system catalog relations in `pg_catalog`. These system catalog tables record system (as opposed to user) activity, such as metadata lookups and from third-party tools performing lookups.<br>These statements aren't required for typical auditing and you can disable this option to reduce noise in the log. | ON |
| pgaudit.log_client | Enable this option to echo log messages directly to clients such as [ysqlsh](../../../admin/ysqlsh/) and psql. Log messages are printed directly to the shell, which can be helpful for debugging.<br>When enabled, you can set the level of logs that are output using `pgaudit.log_level`. | OFF |
| pgaudit.log_level | Sets the [severity level](https://www.postgresql.org/docs/11/runtime-config-logging.html#RUNTIME-CONFIG-SEVERITY-LEVELS) of logs written to clients when `pgaudit.log_client` is on. Use this setting for debugging and testing.<br>Values: DEBUG1 .. DEBUG5, INFO, NOTICE, WARNING, LOG.<br>ERROR, FATAL, and PANIC are not allowed.<br>`pgaudit.log_level` only applies when `pgaudit.log_client` is on; otherwise the default LOG level is used. | LOG |
| pgaudit.log_parameter | Include the parameters that were passed with the statement in the logs. When parameters are present, they are included in CSV format after the statement text. | OFF |
| pgaudit.log_relation | Create separate log entries for each relation (TABLE, VIEW, and so on) referenced in a SELECT or DML statement. This is a shortcut for exhaustive logging without using [object audit logging](../../../secure/audit-logging/object-audit-logging-ysql/). | OFF |
| pgaudit.log_statement_once | Ordinarily, statement text (and, if enabled, parameters) are included with every log entry. Enable this setting to only include statement text and parameters for the first entry for a statement or sub-statement combination. This makes for less verbose logging, but can make it more difficult to determine the statement that generated a log entry. | OFF |

## Learn more

- [Logging in YugabyteDB](../../../secure/audit-logging/)
- [PostgreSQL Error Reporting and Logging](https://www.postgresql.org/docs/11/runtime-config-logging.html)
- [Annotated PostgreSQL configuration settings](https://github.com/jberkus/annotated.conf)
-->