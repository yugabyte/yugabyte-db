---
title: Export metrics from YugabyteDB Anywhere universes
headerTitle: Export metrics
linkTitle: Export metrics
description: Export universe metrics to third-party tools.
headcontent: Export universe metrics to third-party tools
menu:
  stable_yugabyte-platform:
    identifier: anywhere-metrics-export
    parent: anywhere-export-configurations
    weight: 10
type: docs
---

You can export [universe metrics](../anywhere-metrics/) to third-party tools for analysis and customization.

## Prerequisite

- In versions earlier than v2026.1.2.0, enable metrics export by setting the **Enable Metrics Export** Global Configuration option (config key `yb.universe.metrics_export_enabled`) to true. Refer to [Manage runtime configuration settings](../../administer-yugabyte-platform/manage-runtime-config/). Starting in v2026.1.2.0, metrics export is enabled by default.

- Create an export configuration. A configuration defines the sign in credentials and settings for the tool that you want to export metrics to. Refer to [Manage export configurations](../anywhere-export-configuration/).

## Limitations

On Kubernetes universes, metrics export requires YugabyteDB v2026.1.2.0 (or preview v2.31.0.0) or later, and the [OpenTelemetry Operator](https://opentelemetry.io/docs/platforms/kubernetes/operator/#getting-started) on the cluster.

You can export metrics from YB-Master, YB-TServer, YSQL, YCQL, and the OpenTelemetry Collector. Node and Node agent metrics cannot be exported on Kubernetes; those sources are not present in the database pods.

## Export universe metrics

{{< tabpane text=true >}}

{{% tab header="New UI" lang="new" %}}

{{<tags/ui/new>}}To enable or modify metrics export for a universe, do the following:

1. On the universe **Settings** tab, select **Telemetry Export** and click **Export Metrics**.

1. Select the [export configuration](../anywhere-export-configuration/) for the tool you want to export to.

1. Set the **Collection Settings**:

    - Collection interval (in seconds)
    - Collection Timeout (in seconds)
    - Collection Level
    - Metric Sources

1. Click **Export Metrics** when you are done.

To stop metrics export from a universe, on the universe **Settings** tab, select **Telemetry Export** and click **Disable Metrics Export**.

{{% /tab %}}

{{% tab header="Classic UI" lang="classic" %}}

{{<tags/ui/classic>}}To enable or modify metrics export for a universe, do the following:

1. On the universe **Metrics** tab, click the gear icon and choose **Export Metrics**.
1. Enable the **Export Metrics from this Universe** option.
1. Select the [export configuration](../anywhere-export-configuration/) for the tool you want to export to.
1. Click **Apply Changes**.

To stop metrics export from a universe, disable the **Export Metrics from this Universe** option.

{{% /tab %}}

{{< /tabpane >}}
