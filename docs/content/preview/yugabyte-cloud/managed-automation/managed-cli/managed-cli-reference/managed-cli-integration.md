---
title: ybm CLI integration resource
headerTitle: ybm integration
linkTitle: integration
description: YugabyteDB Managed CLI reference integration resource.
headcontent: Manage integration configuration
menu:
  preview_yugabyte-cloud:
    identifier: managed-cli-integration
    parent: managed-cli-reference
    weight: 20
type: docs
---

Use the `integration` resource to create integration configurations for third-party tools. Integrations can then be assigned to clusters to export metrics and logs to third-party tools.

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

Assign the configuration to a cluster:

```sh
ybm integration assign \
    --cluster-name my_cluster \
    --config-name datadog1
```

## Commands

### assign

Assign an integration configuration to the specified cluster.

| Flag | Description |
| :--- | :--- |
| --cluster-name | Required. Name of the cluster. |
| --config-name | Required. Name of the integration configuration. |

### create

Create an integration configuration.

| Flag | Description |
| :--- | :--- |
| --config-name | Required. Name for the integration configuration. |
| --type | Required. The third party tool to export to. Options: DATADOG, GRAFANA, SUMOLOGIC. |
| --datadog-spec | Required for type DATADOG. The Datadog export details, provided as key-value pairs.<br>Arguments:<ul><li>api-key - your Datadog API key.</li><li>site - your Datadog site parameters.</li></ul> |
| --grafana-spec | Required for type GRAFANA. The Grafana Cloud export details, provided as key-value pairs.<br>Arguments:<ul><li>access-policy-token - your Grafana Cloud token.</li><li>org-slug - your organization name.</li><li>instance-id - your Grafana Cloud instance ID.</li><li>zone - your Grafana Cloud instance zone.</li></ul> |
| --sumologic-spec | Required for type SUMOLOGIC. The Sumo Logic export details, provided as key-value pairs.<br>Arguments:<ul><li>access-key - your Sumo Logic access key.</li><li>access-id - your Sumo Logic access ID.</li><li>installation-token - your Sumo Logic installation token.</li></ul> |

### delete

Delete a specified integration configuration. You can't delete configurations that are in use by a cluster.

| Flag | Description |
| :--- | :--- |
| --config-name | Required. Name of the integration configuration. |

### list

List the integration configurations.

### unassign

Remove the integration configuration from the specified cluster.

| Flag | Description |
| :--- | :--- |
| --cluster-name | Required. Name of the cluster. |

### update

Update an integration configuration.

| Flag | Description |
| :--- | :--- |
| --config-name | Required. Name of the integration configuration. |
| --new-config-name | New name for the integration configuration. |
| --type | Required. The third party tool to exported metrics to. Options: DATADOG, GRAFANA, SUMOLOGIC. |
| --datadog-spec | Required for type DATADOG. The Datadog export details, provided as key-value pairs.<br>Arguments:<ul><li>api-key - your Datadog API key.</li><li>site - your Datadog site parameters.</li></ul> |
| --grafana-spec | Required for type GRAFANA. The Grafana Cloud export details, provided as key-value pairs.<br>Arguments:<ul><li>access-policy-token - your Grafana Cloud token.</li><li>org-slug - your organization name.</li><li>instance-id - your Grafana Cloud instance ID.</li><li>zone - your Grafana Cloud instance zone.</li></ul> |
| --sumologic-spec | Required for type SUMOLOGIC. The Sumo Logic export details, provided as key-value pairs.<br>Arguments:<ul><li>access-key - your Sumo Logic access key.</li><li>access-id - your Sumo Logic access ID.</li><li>installation-token - your Sumo Logic installation token.</li></ul> |
