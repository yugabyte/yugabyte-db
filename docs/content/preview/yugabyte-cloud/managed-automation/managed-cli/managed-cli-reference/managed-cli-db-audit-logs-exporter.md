---
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
---

Use the `db-audit-logs-exporter` resource to create database audit log export configurations for third-party tools, and assign them to clusters.

## Syntax

```text
Usage: ybm db-audit-logs-exporter [command] [flags]
```

## Example

Create a configuration:

```sh
ybm db-audit-logs-exporter create \
    --config-name datadog1 \
    --type DATADOG \
    --datadog-spec api-key=efXXXXXXXXXXXXXXXXXXXXXXXXXXXXee,site=US1
```

Assign the configuration to a cluster:

```sh
ybm db-audit-logs-exporter assign \
    --config-name datadog1 \
    --cluster-name my_cluster
```

## Commands

### assign

Assign a database audit log export configuration to the specified cluster.

| Flag | Description |
| :--- | :--- |
| --cluster-name | Required. Name of the cluster. |
| --statement-classes | Required. The YSQL statements to log, provided as key-value pairs. <br>Arguments:<ul><li>statement_classes=READ, WRITE, or MISC</li></ul> |
| --telemetry-provider-id | Required. The ID of the provider you are exporting the logs to. |
| --ysql-config | Required. The pgaudit options, provided as key-value pairs.<br>Arguments:<ul><li>log_catalog=BOOLEAN</li><li>log_level=LOG_LEVEL</li><li>log_client=BOOLEAN</li><li>log_parameter=BOOLEAN</li><li>log_relation=BOOLEAN</li><li>log_statement_once=BOOLEAN</li></ul> |

### list

List the database audit log export configuration for a cluster.

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
| --statement-classes | Required. The YSQL statements to log, provided as key-value pairs. <br>Arguments:<ul><li>statement_classes=READ, WRITE, or MISC</li></ul> |
| --telemetry-provider-id | Required. The ID of the provider you are exporting the logs to. |
| --ysql-config | Required. The pgaudit options, provided as key-value pairs.<br>Arguments:<ul><li>log_catalog=BOOLEAN</li><li>log_level=LOG_LEVEL</li><li>log_client=BOOLEAN</li><li>log_parameter=BOOLEAN</li><li>log_relation=BOOLEAN</li><li>log_statement_once=BOOLEAN</li></ul> |
