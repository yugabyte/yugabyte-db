---
title: ybm CLI cluster db-audit-logging
headerTitle: ybm cluster db-audit-logging
linkTitle: cluster db-audit-logging
description: YugabyteDB Aeon CLI reference Cluster Database Audit Logging Resource.
headcontent: Manage cluster database audit logging
menu:
  preview_yugabyte-cloud:
    identifier: managed-cli-db-audit-logging
    parent: managed-cli-reference
    weight: 20
type: docs
---

Use the `cluster db-audit-logging` resource to perform operations on a YugabyteDB Aeon cluster, including the following:

- enable, disable, and update database audit logging.
- get information about database audit logging.

For information on database audit logging settings, refer to [Database Audit Logging](../../../../cloud-monitor/logging-export/#database-audit-logging).

## Prerequisite

Before using these commands, you must have an [integration configuration](../../../../cloud-monitor/logging-export/#prerequisites) already set up. This configuration defines the authentication and connection details for the third-party tool where logs will be exported.

## Syntax

```text
Usage: ybm cluster db-audit-logging [command] [flags]
```

## Examples

Enable database audit logging for a cluster:

```sh
ybm cluster db-audit-logging enable \
  --cluster-name your-cluster \
  --integration-name datadog1 \
  --statement_classes="READ,WRITE,ROLE" \
  --wait \
  --ysql-config="log_catalog=true,log_client=true,log_level=NOTICE,log_relation=true,log_parameter=true,log_statement_once=true"
```

Disable database audit logging for a cluster.

```sh
ybm cluster db-audit-logging disable \
  --cluster-name your-cluster
```

Get information about database audit logging for a cluster.

```sh
ybm cluster db-audit-logging describe --cluster-name your-cluster
```

Update some fields of the log configuration.

```sh
ybm cluster db-audit-logging update \
  --cluster-name your-cluster \
  --integration-name your-integration \
  --statement_classes="WRITE,MISC" \
  --ysql-config="log_catalog=true,log_client=false,log_level=NOTICE,log_relation=false,log_parameter=true,log_statement_once=true"
```

## Commands

### enable

Enable database audit logging for a cluster and export the logs to the integration passed in the flag `--integration-name`.

| Flag | Description |
| :--- | :--- |
| --cluster-name | Required. Name of the cluster whose database audit logging you want to enable. |
| --integration-name | Required. Name of the integration that you want to use to export the logs. |
| --ysql-config | Required. The [YSQL audit logging settings](../../../../cloud-monitor/logging-export/#ysql-audit-logging-settings), provided as key-value pairs.<br>Arguments:<ul><li>log_catalog</li><li>log_level</li><li>log_client</li><li>log_parameter</li><li>log_relation</li><li>log_statement_once</li></ul> |  
| --statement_classes | Required. The YSQL statements to log, provided as key-value pairs.<br>Arguments:<ul><li>READ</li><li>WRITE</li><li>FUNCTION</li><li>ROLE</li><li>DDL</li><li>MISC</li></ul> For more details, see [Database Audit Logging](../../../../cloud-monitor/logging-export/#database-audit-logging). |

### disable

Disable database audit logging for a cluster.

| Flag | Description |
| :--- | :--- |
| --cluster-name | Required. Name of the cluster whose database audit logging you want to disable. |
| -f, --force | Optional. Bypass the prompt for non-interactive usage. |

### describe

Fetch detailed information about the audit logging configuration for a cluster.


| Flag | Description |
| :--- | :--- |
| --cluster-name | Required. Name of the cluster for which you want to fetch the database audit logging configuration. |

### update

Update the database audit logging configuration.

| Flag | Description |
| :--- | :--- |
| --cluster-name | Required. Name of the cluster with database audit logging configuration you want to update. |
| --integration-name | Required. Name of the integration. |
| --ysql-config | Required. The [YSQL audit logging settings](../../../../cloud-monitor/logging-export/#ysql-audit-logging-settings), provided as key-value pairs.<br>Arguments:<ul><li>log_catalog</li><li>log_level</li><li>log_client</li><li>log_parameter</li><li>log_relation</li><li>log_statement_once</li></ul> |
| --statement_classes | Required. The YSQL statements to log, provided as key-value pairs.<br>Arguments:<ul><li>READ</li><li>WRITE</li><li>FUNCTION</li><li>ROLE</li><li>DDL</li><li>MISC</li></ul> For more details, see [Database Audit Logging](../../../../cloud-monitor/logging-export/#database-audit-logging). |