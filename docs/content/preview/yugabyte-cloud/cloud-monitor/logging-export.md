---
title: Export audit and PostgreSQL logs from YugabyteDB Managed clusters
headerTitle: Export logs
linkTitle: Export logs
description: Export cluster metrics to third-party tools.
headcontent: Export audit and PostgreSQL logs from YugabyteDB Managed clusters
menu:
  preview_yugabyte-cloud:
    identifier: export-logs
    parent: cloud-monitor
    weight: 610
type: docs
---

Export cluster database logs to third-party tools for analysis and customization. You can export the following types of logs:

- Audit logs. Audit logs are generated using the [PostgreSQL Audit Extension](https://www.pgaudit.org/#) ([pgaudit](https://github.com/pgaudit/pgaudit/blob/1.3.2/README.md)).
- Query logs. These are the standard PostrgreSQL logs.

Exporting logs may incur additional costs for network transfer, especially for cross-region and internet-based transfers. Refer to [Data transfer costs](../../cloud-admin/cloud-billing-costs/#data-transfer-costs).

## Prerequisites

Create an export configuration. An export configuration defines the settings and login information for the tool that you want to export your logs to. Refer to [Export configuration](../metrics-export/#export-configuration).

## Database audit logs

To enable database audit logging for a cluster, do the following:

1. On the cluster **Settings** tab, select **Database Audit Log**.
1. Click **Enable Database Audit Logging**.
1. Select the YSQL statements to log.

    - **Read** - SELECT and COPY when the source is a relation or a query.
    - **Write** - INSERT, UPDATE, DELETE, TRUNCATE, and COPY when the destination is a relation.
    - **Function** - Function calls and DO blocks.
    - **Role** - Statements related to roles and privileges: GRANT, REVOKE, CREATE/ALTER/DROP ROLE.
    - **DDL** - All DDL that is not included in the ROLE class.
    - **Misc** - Miscellaneous commands, such as DISCARD, FETCH, CHECKPOINT, VACUUM, SET.

1. Configure the YSQL [audit logging settings](#audit-settings).

1. Select the export configuration to use.

1. Click **Enable Database Audit Logging**.

### Audit settings

| Option | Description | Default |
| :----- | :----- | :------ |
| pgaudit.log_catalog | Log statements for the PostgreSQL system catalog relations in `pg_catalog`. These system catalog tables record system (as opposed to user) activity, such as metadata lookups and from third-party tools performing lookups.<br>These statements aren't required for typical auditing and you can disable this option to reduce noise in the log. | ON |
| pgaudit.log_client | Enable this option to echo log messages directly to clients such as [ysqlsh](../../../admin/ysqlsh/) and psql. Log messages are printed directly to the shell, which can be helpful for debugging.<br>When enabled, you can set the level of logs that are output using `pgaudit.log_level`. | OFF |
| pgaudit.log_level | Sets the [severity level](https://www.postgresql.org/docs/16/runtime-config-logging.html#RUNTIME-CONFIG-SEVERITY-LEVELS) of logs written to clients when `pgaudit.log_client` is on. Use this setting for debugging and testing.<br>Values: DEBUG1 .. DEBUG5, INFO, NOTICE, WARNING, LOG.<br>ERROR, FATAL, and PANIC are not allowed.<br>`pgaudit.log_level` only applies when `pgaudit.log_client` is on; otherwise the default LOG level is used. | LOG |
| pgaudit.log_parameter | Include the parameters that were passed with the statement in the logs. When parameters are present, they are included in CSV format after the statement text. | OFF |
| pgaudit.log_relation | Create separate log entries for each relation (TABLE, VIEW, and so on) referenced in a SELECT or DML statement. This is a shortcut for exhaustive logging without using [object audit logging](../../../secure/audit-logging/object-audit-logging-ysql/). | OFF |
| pgaudit.log_statement_once | Ordinarily, statement text (and, if enabled, parameters) are included with every log entry. Enable this setting to only include statement text and parameters for the first entry for a statement or sub-statement combination. This makes for less verbose logging, but can make it more difficult to determine the statement that generated a log entry. | OFF |

## Database query logs

To enable database query logging for a cluster, do the following:

1. On the cluster **Settings** tab, select **Database Query Log**.
1. Click **Enable Database Query Logging**.
1. Select the YSQL statements to log.

    - **Read** - SELECT and COPY when the source is a relation or a query.
    - **Write** - INSERT, UPDATE, DELETE, TRUNCATE, and COPY when the destination is a relation.
    - **Function** - Function calls and DO blocks.
    - **Role** - Statements related to roles and privileges: GRANT, REVOKE, CREATE/ALTER/DROP ROLE.
    - **DDL** - All DDL that is not included in the ROLE class.
    - **Misc** - Miscellaneous commands, such as DISCARD, FETCH, CHECKPOINT, VACUUM, SET.

1. Configure the YSQL audit logging.

1. Select the export configuration to use.

1. Click **Enable Database Query Logging**.

## Learn more

- [Configure audit logging in YSQL](../../../secure/audit-logging/audit-logging-ysql/)