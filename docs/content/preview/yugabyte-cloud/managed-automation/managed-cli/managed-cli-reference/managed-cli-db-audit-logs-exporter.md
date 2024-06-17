<!---
title: ybm CLI db-audit-logs-exporter resource
headerTitle: ybm db-audit-logs-exporter
linkTitle: db-audit-logs-exporter
description: YugabyteDB Managed CLI reference db-audit-logs-exporter resource.
headcontent: Manage database audit log export configuration
menu:
  preview_yugabyte-cloud:
    identifier: managed-cli-db-audit-logs-exporter
    parent: managed-cli-reference
    weight: 20
type: docs
private: true
--->

Use the `db-audit-logs-exporter` resource to configure [database audit logs](../../../../cloud-monitor/logging-export/) for export to third-party tools.

## Syntax

```text
Usage: ybm db-audit-logs-exporter [command] [flags]
```

## Example

Assign a configuration to a cluster:

```sh
ybm db-audit-logs-exporter assign \
    --cluster-name my_cluster \
    --integration-name datadog1 \
    --statement_classes=READ,WRITE \
    --ysql-config==log_catalog=true,log_client=false,log_level=INFO,log_parameter=true
```

## Commands

### assign

Export the specified cluster's database audit logs using the specified integration configuration.

| Flag | Description |
| :--- | :--- |
| --cluster-name | Required. Name of the cluster. |
| --integration-name | Required. The name of the integration configuration you are using to export the logs to. |
| --statement-classes | Required. The YSQL statements to log, provided as key-value pairs. <br>Arguments:<ul><li>statement_classes=READ, WRITE, or MISC</li></ul> |
| --ysql-config | Required. The pgaudit options, provided as key-value pairs.<br>Arguments:<ul><li>log_catalog=BOOLEAN</li><li>log_level=LOG_LEVEL</li><li>log_client=BOOLEAN</li><li>log_parameter=BOOLEAN</li><li>log_relation=BOOLEAN</li><li>log_statement_once=BOOLEAN</li></ul> |

### list

List the database audit log export settings for a cluster.

| Flag | Description |
| :--- | :--- |
| --cluster-name | Required. Name of the cluster. |

### unassign

Remove the database audit log export configuration from the specified cluster.

| Flag | Description |
| :--- | :--- |
| --cluster-name | Required. Name of the cluster. |
| --export-config-id | Required. The ID of the DB audit export configuration. |

### update

Update a database audit log export configuration.

| Flag | Description |
| :--- | :--- |
| --cluster-name | Required. Name of the cluster. |
| --export-config-id | Required. The ID of the DB audit export configuration. |
| --integration-name | Required. The name of the integration configuration you are using to export the logs. |
| --statement-classes | Required. The YSQL statements to log, provided as key-value pairs. <br>Arguments:<ul><li>statement_classes=READ, WRITE, or MISC</li></ul> |
| --ysql-config | Required. The pgaudit options, provided as key-value pairs.<br>Arguments:<ul><li>log_catalog=BOOLEAN</li><li>log_level=LOG_LEVEL</li><li>log_client=BOOLEAN</li><li>log_parameter=BOOLEAN</li><li>log_relation=BOOLEAN</li><li>log_statement_once=BOOLEAN</li></ul> |
