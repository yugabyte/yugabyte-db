---
title: ybm CLI integration resource
headerTitle: ybm integration
linkTitle: integration
description: YugabyteDB Aeon CLI reference integration resource.
headcontent: Manage integration configuration
menu:
  preview_yugabyte-cloud:
    identifier: managed-cli-integration
    parent: managed-cli-reference
    weight: 20
type: docs
---

Use the `integration` resource to create [export configurations](../../../../cloud-monitor/managed-integrations/) for third-party tools. Export configurations can then be assigned to clusters to export metrics and logs to third-party tools.

## Syntax

```text
Usage: ybm integration [command] [flags]
```

## Example

Create a configuration:

```sh
ybm integration create \
    --config-name datadog1 \
    --type DATADOG \
    --datadog-spec api-key=efXXXXXXXXXXXXXXXXXXXXXXXXXXXXee,site=US1
```

## Commands

### create

Create an integration configuration.

| <div style="width:175px">Flag</div> | Description |
| :--- | :--- |
| --config-name | Required. Name for the integration configuration. |
| --type | Required. The third party tool to export to. Options: DATADOG, GRAFANA, SUMOLOGIC, VICTORIAMETRICS, PROMETHEUS, GOOGLECLOUD. |
| --datadog-spec | Required for type DATADOG. The Datadog export details, provided as key-value pairs.<br>Arguments:<ul><li>api-key - your Datadog API key.</li><li>site - your Datadog site parameters.</li></ul> |
| --grafana-spec | Required for type GRAFANA. The Grafana Cloud export details, provided as key-value pairs.<br>Arguments:<ul><li>access-policy-token - your Grafana Cloud token.</li><li>org-slug - your organization name.</li><li>instance-id - your Grafana Cloud instance ID.</li><li>zone - your Grafana Cloud instance zone.</li></ul> |
| --sumologic-spec | Required for type SUMOLOGIC. The Sumo Logic export details, provided as key-value pairs.<br>Arguments:<ul><li>access-key - your Sumo Logic access key.</li><li>access-id - your Sumo Logic access ID.</li><li>installation-token - your Sumo Logic installation token.</li></ul> |
| --prometheus-spec | Required for type PROMETHEUS. The Prometheus export details, provided as key-value pairs.<br>Arguments:<br><ul><li>endpoint - your Prometheus OTLP endpoint URL.</li></ul>|
| --victoriametrics-spec | Required for type VICTORIAMETRICS. The VictoriaMetrics export details, provided as key-value pairs.<br>Arguments:<br><ul><li>endpoint - your VictoriaMetrics OTLP endpoint URL.</li></ul>|
| --googlecloud-cred-filepath | Required for type GOOGLECLOUD. Provide the absolute file path for Google Cloud service account credentials. |

### delete

Delete a specified integration configuration. You can't delete configurations that are in use by a cluster.

| Flag | Description |
| :--- | :--- |
| --config-name | Required. Name of the integration configuration. |

### list

List the integration configurations.
