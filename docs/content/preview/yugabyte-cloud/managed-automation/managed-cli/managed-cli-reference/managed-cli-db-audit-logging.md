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

- enable, disable, and update database audit logging
- get information about database audit logging

## Syntax

```text
Usage: ybm cluster db-audit-logging [command] [flags]
```

## Commands

### enable

Enable database audit logging for a cluster.

| Flag | Description |
| :--- | :--- |
| --integration-name | Required. Name of the integration. |
| --ysql-config | Required. The YSQL config to set up database audit logging.<br>Provide key-value pairs as follows:<br>`log_catalog=<boolean>, log_level=<LOG_LEVEL>, log_client=<boolean>, log_parameter=<boolean>, log_relation=<boolean>, log_statement_once=<boolean>`.<br>Default is `[]`. |
| --statement_classes | Required. The YSQL config statement classes.<br>Please provide key value pairs as follows:<br>`statement_classes=READ,WRITE,MISC`. |

### disable

Disable database audit logging for a cluster if enabled.

| Flag | Description |
| :--- | :--- |
| -f, --force | Optional. Bypass the prompt for non-interactive usage. |

### describe

Fetch detailed information about the audit logging configuration for a cluster.

### update

Update the database audit logging configuration.

| Flag | Description |
| :--- | :--- |
| --integration-name | Required. Name of the integration. |
| --ysql-config | Required. The YSQL config to set up database audit logging.<br>Provide key-value pairs as follows:<br>`log_catalog=<boolean>, log_level=<LOG_LEVEL>, log_client=<boolean>, log_parameter=<boolean>, log_relation=<boolean>, log_statement_once=<boolean>`.<br>Default is `[]`. |
| --statement_classes | Required. The YSQL config statement classes.<br>Provide values as follows:<br>`statement_classes=READ,WRITE,MISC`. |