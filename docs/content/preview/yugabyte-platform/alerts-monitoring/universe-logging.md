---
title: Export YSQL logs from YugabyteDB Anywhere universes
headerTitle: Export audit logs
linkTitle: Export logs
description: Export universe audit logs to third-party tools.
headcontent: Configure pgaudit logging
badges: ea
menu:
  preview_yugabyte-platform:
    identifier: universe-logging
    parent: alerts-monitoring
    weight: 70
type: docs
---

Export YSQL database logs to third-party tools for security monitoring, to build operations and health dashboards, troubleshooting, and more. You can export the following types of logs:

- Database audit logging. Using the [PostgreSQL Audit Extension](https://www.pgaudit.org/#) ([pgaudit](https://github.com/pgaudit/pgaudit/blob/1.3.2/README.md)), the audit log provides the exact database transactions, which is a compliance requirement for government, financial, or ISO certifications.

Note that YugabyteDB is based on PostgreSQL 11 and uses PostgreSQL Audit Extension v1.3.2.

## Prerequisites

The Audit log export feature is {{<badge/ea>}}. To enable the feature in YugabyteDB Anywhere, set the **Enable DB Audit Logging** Global Configuration option (config key `yb.universe.audit_logging_enabled`) to true. Refer to [Manage runtime configuration settings](../../administer-yugabyte-platform/manage-runtime-config/). Note that only a Super Admin user can modify Global configuration settings. The flag can't be turned off if audit logging is enabled on a universe.

To export logs, you need to first create an export configuration. A configuration defines the sign in credentials and settings for the tool that you want to export your logs to. Refer to [Manage export configurations](../anywhere-export-configuration).

If you want to set pgaudit.log_level to a [severity level](https://www.postgresql.org/docs/11/runtime-config-logging.html#RUNTIME-CONFIG-SEVERITY-LEVELS) greater than WARNING (that is, DEBUG1-5, INFO, NOTICE), you must set the YB-TServer [ysql_log_min_messages](../../../reference/configuration/yb-tserver/#ysql-log-min-messages) flag accordingly.

## Limitations

- Log export is only supported for cloud-based universes and automatically provisioned on-premises universes. Manually provisioned universes and Kubernetes universes are not supported.
- Only YSQL database audit logs can be exported, not YCQL.

## Best practices

- Configuring logging restarts your universe. Configure logging when the cluster isn't experiencing heavy traffic.
- Configuring logging blocks other universe operations, such as backups and maintenance. Avoid changing your settings before maintenance windows and during scheduled backups. The operation will block a backup from running.
- To limit performance impact and control costs, log and export only what you need. The default settings are based on best practices from open source PostgreSQL, the broader community, and YugabyteDB testing to ensure the impact is bounded and insignificant.

## Database audit log

To enable database audit logging for a universe, do the following:

1. On the universe **Logs** tab, click **Enable Database Audit Logging**.

    ![Configure Audit logging](/images/yp/log-export/configure-audit-logging.png)

1. Select the YSQL statements to log.

    - **Read** - SELECT and COPY when the source is a relation or a query.
    - **Write** - INSERT, UPDATE, DELETE, TRUNCATE, and COPY when the destination is a relation.
    - **Function** - Function calls and DO blocks.
    - **Role** - Statements related to roles and privileges: GRANT, REVOKE, and CREATE/ALTER/DROP ROLE.
    - **DDL** - All DDL that is not included in the ROLE class.
    - **Misc** - Miscellaneous commands, such as DISCARD, FETCH, CHECKPOINT, VACUUM, and SET.

1. Configure the [YSQL audit log settings](#ysql-audit-log-settings).

1. Select the export configuration to use.

1. Click **Enable and Export Database Audit Log**.

YugabyteDB Anywhere begins the rolling restart.

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
