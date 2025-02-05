---
title: ybm CLI cluster db-query-logging
headerTitle: ybm cluster db-query-logging
linkTitle: cluster db-query-logging
description: YugabyteDB Aeon CLI reference Cluster Database Query Logging Resource.
headcontent: Manage cluster database query logging
menu:
  preview_yugabyte-cloud:
    identifier: managed-cli-db-query-logging
    parent: managed-cli-reference
    weight: 20
type: docs
---

Use the `cluster db-query-logging` resource to perform operations on a YugabyteDB Aeon cluster, including the following:

- enable, disable, and update db query logging
- get information about db query logging

## Prerequisite

Before using these commands, you must have an [integration configuration](../../../../cloud-monitor/logging-export#prerequisites) already set up. This configuration defines the authentication and connection details for the third-party tool where logs will be exported.

## Syntax

```text
Usage: ybm cluster db-query-logging [command] [flags]
```

## Examples

Enable database query logging for a cluster:

```sh
ybm cluster db-query-logging enable \
--cluster-name your-cluster \
--integration-name your-integration \
--log-line-prefix "%m :%r :%u @ %d :[%p] : " \
--log-min-duration-statement -1 \
--log-connections false \
--log-duration false \
--log-error-verbosity DEFAULT \
--log-statement NONE
```

Disable database query logging for a cluster.

```sh
ybm cluster db-query-logging disable \
--cluster-name your-cluster
```

Get information about database query logging for a cluster.

```sh
ybm cluster db-query-logging describe --cluster-name your-cluster
```

Update some fields of the log configuration.

```sh
ybm cluster db-query-logging update \
--cluster-name "your-cluster" \
--integration-name your-integration \
--log-line-prefix "%m :%r :%u @ %d :[%p] :" \
--log-min-duration-statement 60
```

## Commands

### enable

Enable database query logging for a cluster and export the logs to the integration passed in the flag `--integration-name`.

| Flag | Description |
| :--- | :--- |
| --cluster-name | Required. Name of the cluster whose database logging you want to enable. |
| --integration-name | Required. Name of the integration that you want to use to export the logs. |
| --debug-print-plan | Optional. Enables output of debugging output. Default is `false`. |
| --log-min-duration-statement | Optional. Duration (in ms) of each completed statement to be logged if the statement ran for at least the specified amount of time. Default is `-1` (log all statements). |
| --log-connections | Optional. Log connection attempts. Default is `false`. |
| --log-disconnections | Optional. Log session disconnections. Default is `false`. |
| --log-duration | Optional. Log the duration of each completed statement. Default is `false`. |
| --log-error-verbosity | Optional. Controls the amount of detail written in the server log for each message that is logged.<br>Arguments:<br><ul><li>`DEFAULT` - Standard verbosity level.</li><li>`TERSE` - Minimal detail.</li><li>`VERBOSE` - Maximum detail.</li></ul>Default is `DEFAULT`. |
| --log-statement | Optional. Log all statements or specific types of statements.<br>Arguments:<br><ul><li>`NONE` - Do not log any statements.</li><li>`DDL` - Log data definition language statements.</li><li>`MOD` - Log data modification statements.</li><li>`ALL` - Log all statements.</li></ul>Default is `NONE`. |
| --log-min-error-statement | Optional. Minimum error severity for logging the statement that caused it.<br>Arguments:<br><ul><li>`ERROR` - Log statements causing errors.</li></ul>Default is `ERROR`. |
| --log-line-prefix | Optional. A printf-style format string for [log line prefixes](../../../../cloud-monitor/logging-export/#include-in-the-log-prefix-log-line-prefix). Default is `%m :%r :%u @ %d :[%p] :`. |

### disable

Disable database query logging for a cluster.

| Flag | Description |
| :--- | :--- |
| --cluster-name | Required. Name of the cluster whose database logging you want to disable. |
| -f, --force | Optional. Bypass the prompt for non-interactive usage. |

### Describe

Fetch detailed information about the logging configuration for a cluster.

| Flag | Description |
| :--- | :--- |
| --cluster-name | Required. Name of the cluster for which you want to fetch the database query logging configuration. |

### update

Update the database query logging configuration of a cluster.


| Flag | Description |
| :--- | :--- |
| --cluster-name | Required. Name of the cluster with database query logging configuration you want to update. |
| --debug-print-plan | Optional. Enables output of debugging output. |
| --integration-name | Optional. Name of the Integration. |
| --log-connections | Optional. Log connection attempts. |
| --log-disconnections | Optional. Log session disconnections. |
| --log-duration | Optional. Log the duration of each completed statement. |
| --log-error-verbosity | Optional. Controls the amount of detail written in the server log for each message that is logged.<br>Arguments:<br><ul><li>`DEFAULT` - Standard verbosity level.</li><li>`TERSE` - Minimal detail.</li><li>`VERBOSE` - Maximum detail.</li></ul> |
| --log-line-prefix | Optional. A printf-style format string for log line prefixes. |
| --log-min-duration-statement | Optional. Duration (in ms) of each completed statement to be logged if the statement ran for at least the specified amount of time. Default is `-1` (log all statements). |
| --log-min-error-statement | Optional. Minimum error severity for logging the statement that caused it.<br>Arguments:<br><ul><li>`ERROR`</li></ul> |
| --log-statement | Optional. Log all statements or specific types of statements.<br>Arguments:<br><ul><li>`NONE` - Do not log any statements.</li><li>`DDL` - Log data definition language statements.</li><li>`MOD` - Log data modification statements.</li><li>`ALL` - Log all statements.</li></ul> |