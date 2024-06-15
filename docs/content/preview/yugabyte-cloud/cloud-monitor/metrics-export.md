---
title: Export metrics from YugabyteDB Managed clusters
headerTitle: Export metrics
linkTitle: Export metrics
description: Export cluster metrics to third-party tools.
headcontent: Export cluster metrics to third-party tools
menu:
  preview_yugabyte-cloud:
    identifier: export-metrics
    parent: cloud-monitor
    weight: 605
type: docs
---

You can export [cluster metrics](../overview/) to third-party tools for analysis and customization.

1. Create an export configuration. An export configuration defines the settings and login information for the tool that you want to export your metrics to.
1. Assign a configuration to the cluster. Once created, you can assign an export configuration to one or more clusters. While the connection is active, metrics are automatically streamed to the tool.

Currently, YugabyteDB Managed supports export to [Datadog](https://docs.datadoghq.com/), [Grafana Cloud](https://grafana.com/docs/grafana-cloud/), and [Sumo Logic](https://www.sumologic.com). Metrics export is not available for Sandbox clusters.

Exporting metrics may incur costs for network transfer, especially for cross-region and internet-based transfers. Refer to [Data transfer costs](../../cloud-admin/cloud-billing-costs/#data-transfer-costs).

## Prerequisites

Create an integration configuration. A configuration defines the sign in credentials and settings for the tool that you want to export your logs to. Refer to [Integrations](../managed-integrations).

## Export cluster metrics

To enable metrics export for a cluster, do the following:

1. On the cluster **Settings** tab, select **Export Metrics**.
1. Click **Export Metrics**.
1. Select the [export configuration](../managed-integrations/) for the tool you want to export to.
1. Click **Export Metrics**.

To modify the metrics export configuration, on the cluster **Settings** tab, select **Edit Metrics Export Configuration** and choose a different Export Configuration.

To pause or resume metrics export from a cluster, on the cluster **Settings** tab, select **Edit Metrics Export Configuration** and choose **Pause Metrics Export** or **Resume Metrics Export**.

To remove metrics export from a cluster, on the cluster **Settings** tab, select **Edit Metrics Export Configuration** and choose **Disable Metrics Export**.
