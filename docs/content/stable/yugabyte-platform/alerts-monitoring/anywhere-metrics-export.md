---
title: Export metrics from YugabyteDB Anywhere universes
headerTitle: Export metrics
linkTitle: Export metrics
description: Export universe metrics to third-party tools.
headcontent: Export universe metrics to third-party tools
tags:
  feature: early-access
menu:
  stable_yugabyte-platform:
    identifier: anywhere-metrics-export
    parent: anywhere-export-configurations
    weight: 10
type: docs
---

You can export [universe metrics](../anywhere-metrics/) to third-party tools for analysis and customization.

## Prerequisites

- The Metrics export feature is {{<tags/feature/ea idea="441">}}. To enable the feature in YugabyteDB Anywhere, set the **Enable Metrics Export** Global Configuration option (config key `yb.universe.metrics_export_enabled`) to true. Refer to [Manage runtime configuration settings](../../administer-yugabyte-platform/manage-runtime-config/). Note that only a Super Admin user can modify Global configuration settings. The flag can't be turned off if metrics export is enabled on a universe.

- Create an export configuration. A configuration defines the sign in credentials and settings for the tool that you want to export metrics to. Refer to [Manage export configurations](../anywhere-export-configuration/).

## Limitations

Metrics export is not available for Kubernetes universes.

## Export universe metrics

To enable or modify metrics export for a universe, do the following:

1. On the universe **Metrics** tab, click the gear icon and choose **Export Metrics**.
1. Enable the **Export Metrics from this Universe** option.
1. Select the [export configuration](../anywhere-export-configuration/) for the tool you want to export to.
1. Click **Apply Changes**.

To remove metrics export from a universe, disable the **Export Metrics from this Universe** option.
