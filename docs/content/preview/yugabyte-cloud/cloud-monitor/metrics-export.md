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

Exporting cluster metrics may incur additional costs for network transfer within a cloud region, between cloud regions, and across the Internet. Refer to [Data transfer costs](../../cloud-admin/cloud-billing-costs/#data-transfer-costs).

## Prerequisites

Create an integration. An integration defines the settings and login information for the tool that you want to export your logs to. Refer to [Integrations](../managed-integrations).

## Export cluster metrics

To enable metrics export for a cluster, do the following:

1. On the cluster **Settings** tab, select **Export Metrics**.
1. Click **Export Metrics**.
1. Select the export configuration to use.
1. Click **Export Metrics**.

You can assign a metrics configuration to one or more clusters to begin exporting metrics from those clusters. You can also pause and resume metrics export for a cluster.

To assign an export configuration to a cluster, in the **Export Metrics by Cluster** table, in the **Export Configurations** column, choose a configuration for the cluster.

To remove an export configuration from a cluster, in the **Export Metrics by Cluster** table, set the **Export Configurations** column to **None**.

To pause or resume metrics export from a cluster, in the **Export Metrics by Cluster** table, click the **...** button for the cluster, and choose **Pause** or **Resume**.
