---
title: Export metrics
linkTitle: Export metrics
description: Export cluster metrics to third-party tools.
headcontent: Export cluster metrics to third-party tools
menu:
  preview_yugabyte-cloud:
    identifier: export-metrics
    parent: cloud-monitor
    weight: 600
type: docs
---

You can export cluster metrics to third-party tools for analysis and customization. Exporting metrics for a cluster is a two-stage process:

1. Create an export configuration. An export configuration defines the settings and login information for the tool that you want to export your metrics to.
1. Assign a configuration to the cluster. Once created, you can assign an export configuration to one or more clusters. While the connection is active, metrics are automatically streamed to the tool.

Currently, YugabyteDB Managed supports export to [Datadog](https://docs.datadoghq.com/). Metrics export is not available for the Sandbox cluster.

Exporting metrics counts against your data transfer allowance, and may incur additional costs for network transfer, especially for cross-region and internet-based transfers, if usage exceeds your cluster allowance. Refer to [Data transfer costs](../../cloud-admin/cloud-billing-costs/#data-transfer-costs).

## Prerequisites

### Datadog

- Datadog account
- Datadog [API key](https://docs.datadoghq.com/account_management/api-app-keys/)

## Export configuration

Create export configurations and assign them to clusters on the **Integrations > Metrics** tab.

![Integrations Metrics tab](/images/yb-cloud/managed-metrics-export.png)

The tab lists any export configurations that you have created, along with the clusters in your account, and the status of any metrics export that has been assigned.

You can also access metrics export on the cluster **Settings** tab under **Metrics Export**.

### Manage export configurations

You can add, edit, and delete export configurations. You can't delete a configuration that is assigned to a cluster.

To create an export configuration, do the following:

1. On the **Integrations** page, select the **Metrics** tab.
1. Click **Create Export Configuration** or, if one or more configurations are already available, **Add Export Configuration**.
1. Enter a name for the configuration.
1. Choose your provider. (Currently, only Datadog is supported.)
1. Enter your Datadog [API key](https://docs.datadoghq.com/account_management/api-app-keys/).
1. Choose the Datadog site to connect to, or choose Self-hosted and enter your URL.
1. Optionally, click **Download** to download the Datadog dashboard template. You can import this JSON format template into your Datadog account and use it as a starting point for visualizing your cluster data in Datadog.
1. Click **Test Configuration** to make sure your connection is working.
1. Click **Create Configuration**.

To edit a configuration, click the **...** button on the configuration and choose **Edit**.

To delete a configuration, click the **...** button on the configuration and choose **Delete**.

### Manage cluster metrics export

You can assign a metrics configuration to one or more clusters to begin exporting metrics from those clusters. You can also pause and resume metrics export for a cluster.

To assign an export configuration to a cluster, in the **Export Metrics by Cluster** table, in the **Export Configurations** column, choose a configuration for the cluster.

To remove an export configuration from a cluster, in the **Export Metrics by Cluster** table, set the **Export Configurations** column to **None**.

To pause or resume metrics export from a cluster, in the **Export Metrics by Cluster** table, click the **...** button for the cluster, and choose **Pause** or **Resume**.
