---
title: ybm CLI metrics-exporter resource
headerTitle: ybm metrics-exporter
linkTitle: metrics-exporter
description: YugabyteDB Managed CLI reference metrics-exporter resource.
headcontent: Manage metrics export configuration
menu:
  preview_yugabyte-cloud:
    identifier: managed-cli-metrics-exporter
    parent: managed-cli-reference
    weight: 20
type: docs
---

Use the `metrics-exporter` resource to create metrics export configurations for third-party tools, and assign them to clusters.

## Syntax

```text
Usage: ybm metrics-exporter [command] [flags]
```

## Example

Create a configuration:

```sh
ybm metrics-exporter create \
    --config-name datadog1 \
    --type DATADOG \
    --datadog-spec api-key=efXXXXXXXXXXXXXXXXXXXXXXXXXXXXee,site=US1
```

Assign the configuration to a cluster:

```sh
ybm metrics-exporter attach \
    --config-name datadog1 \
    --cluster-name my_cluster
```

## Commands

### attach

Assign an export configuration to the specified cluster.

| Flag | Description |
| :--- | :--- |
| --config-name | Required. Name of the export configuration. |
| --cluster-name | Required. Name of the cluster. |

### create

Create an export configuration.

| Flag | Description |
| :--- | :--- |
| --config-name | Required. Name for the export configuration. |
| --type | Required. The third party tool to exported metrics to. Options: DATADOG or GRAFANA. |
| --datadog-spec | Required for type DATADOG. The Datadog export details, provided as key-value pairs.<br>Arguments:<ul><li>api-key - your Datadog API key.</li><li>site - your Datadog site parameters.</li></ul> |
| --grafana-spec | Required for type GRAFANA. The Grafana export details, provided as key-value pairs.<br>Arguments:<ul><li>access-policy-token - your Grafana token.</li><li>org-slug - your organization name.</li><li>instance-id - your Grafana instance ID.</li><li>zone - your Grafana instance zone.</li></ul> |

### delete

Delete a specified export configuration. You can't delete configurations that are in use by a cluster.

| Flag | Description |
| :--- | :--- |
| --config-name | Required. Name of the export configuration. |

### list

Display the export configurations.

### pause

Pause the export of metrics from the specified cluster.

| Flag | Description |
| :--- | :--- |
| --cluster-name | Required. Name of the cluster. |

### remove-from-cluster

Remove the export configuration from the specified cluster.

| Flag | Description |
| :--- | :--- |
| --cluster-name | Required. Name of the cluster. |

### resume

Resume the export of metrics from the specified cluster.

| Flag | Description |
| :--- | :--- |
| --cluster-name | Required. Name of the cluster. |

### update

Update an export configuration.

| Flag | Description |
| :--- | :--- |
| --config-name | Required. Name for the export configuration. |
| --type | Required. The third party tool to exported metrics to. Options: DATADOG or GRAFANA. |
| --datadog-spec | Required for type DATADOG. The Datadog export details, provided as key-value pairs.<br>Arguments:<ul><li>api-key - your Datadog API key.</li><li>site - your datadog site parameters.</li></ul> |
| --grafana-spec | Required for type GRAFANA. The Grafana export details, provided as key-value pairs.<br>Arguments:<ul><li>access-policy-token - your Grafana token.</li><li>org-slug - your organization name.</li><li>instance-id - your Grafana instance ID.</li><li>zone - your Grafana instance zone.</li></ul> |
