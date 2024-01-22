---
title: Export metrics from YugabyteDB Managed
headerTitle: Export metrics
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

Currently, YugabyteDB Managed supports export to [Datadog](https://docs.datadoghq.com/), [Grafana Cloud](https://grafana.com/docs/grafana-cloud/), and [Sumo Logic](https://www.sumologic.com). Metrics export is not available for Sandbox clusters.

Exporting metrics counts against your data transfer allowance, and may incur additional costs for network transfer, especially for cross-region and internet-based transfers, if usage exceeds your cluster allowance. Refer to [Data transfer costs](../../cloud-admin/cloud-billing-costs/#data-transfer-costs).

## Prerequisites

### Datadog

The YugabyteDB Managed [Datadog integration](https://docs.datadoghq.com/integrations/yugabytedb_managed/) requires the following:

- Datadog account
- Datadog [API key](https://docs.datadoghq.com/account_management/api-app-keys/)

### Grafana Cloud

- Grafana Cloud account and stack. For best performance and lower data transfer costs, deploy your stack in the region closest to your cluster. For a list of supported regions, see the [Grafana Cloud documentation](https://grafana.com/docs/grafana-cloud/monitor-infrastructure/otlp/send-data-otlp/#send-data-using-opentelemetry-protocol-otlp).
- Access policy token. You need to create an Access policy with metrics:write scope, and then add a token. For more information, see [Grafana Cloud Access Policies](https://grafana.com/docs/grafana-cloud/account-management/authentication-and-permissions/access-policies/authorize-services/) in the Grafana documentation.

### Sumo Logic

- Create an [access ID and access key](https://help.sumologic.com/docs/manage/security/access-keys/) on the **Preferences** page under your profile name.
- [Installation token](https://help.sumologic.com/docs/manage/security/installation-tokens/). These are available under **Administration > Security > Installation Tokens**.
- To use the dashboard template, [install the YugabyteDB app](https://help.sumologic.com/docs/get-started/apps-integrations/) (coming soon) in your Sumo Logic account.

## Export configuration

Create export configurations and assign them to clusters on the **Integrations > Metrics** tab.

![Integrations Metrics tab](/images/yb-cloud/managed-metrics-export.png)

The tab lists any export configurations that you have created, along with the clusters in your account, and the status of any metrics export that has been assigned.

You can also access metrics export on the cluster **Settings** tab under **Export Metrics**.

### Manage export configurations

You can add, edit, and delete export configurations. You can't delete a configuration that is assigned to a cluster.

{{< tabpane text=true >}}

  {{% tab header="Datadog" lang="datadog" %}}

To create an export configuration, do the following:

1. On the **Integrations** page, select the **Metrics** tab.
1. Click **Create Export Configuration** or, if one or more configurations are already available, **Add Export Configuration**.
1. Enter a name for the configuration.
1. Choose Datadog.
1. Enter your Datadog [API key](https://docs.datadoghq.com/account_management/api-app-keys/).
1. Choose the Datadog site to connect to, or choose Self-hosted and enter your URL.
1. Optionally, click **Download** to download the Datadog [dashboard](https://docs.datadoghq.com/dashboards/) template. You can import this JSON format template into your Datadog account and use it as a starting point for visualizing your cluster data in Datadog.
1. Click **Test Configuration** to make sure your connection is working.
1. Click **Create Configuration**.

  {{% /tab %}}

  {{% tab header="Grafana Cloud" lang="grafana" %}}

To create an export configuration, do the following:

1. On the **Integrations** page, select the **Metrics** tab.
1. Click **Create Export Configuration** or, if one or more configurations are already available, **Add Export Configuration**.
1. Enter a name for the configuration.
1. Choose Grafana Cloud.
1. Enter your organization name. This is displayed in the URL when you connect to your Grafana Cloud Portal (for example, `https://grafana.com/orgs/<organization-name>`).
1. Enter your Grafana Cloud [Access policy token](#grafana).
1. Enter your Grafana Cloud instance ID and zone. Obtain these by navigating to the Cloud portal, selecting your stack, and on the Grafana tile, clicking **Details**. They are displayed under **Instance Details**.
1. Optionally, click **Download** to download the Grafana Cloud dashboard template. You can [import this JSON format template](https://grafana.com/docs/grafana-cloud/visualizations/dashboards/manage-dashboards/#export-and-import-dashboards) into your Grafana account and use it as a starting point for visualizing your cluster data in Grafana. The dashboard is also available from the [Grafana Dashboards](https://grafana.com/grafana/dashboards/19887-yugabytedb-managed-clusters/) page.
1. Click **Test Configuration** to make sure your connection is working.
1. Click **Create Configuration**.

  {{% /tab %}}

  {{% tab header="Sumo Logic" lang="sumo" %}}

To create an export configuration, do the following:

1. On the **Integrations** page, select the **Metrics** tab.
1. Click **Create Export Configuration** or, if one or more configurations are already available, **Add Export Configuration**.
1. Enter a name for the configuration.
1. Choose Sumo Logic.
1. Enter your installation token, access ID, and access key.
1. Optionally, click **Download** to download the Sumo Logic dashboard template. After you install the [YugabyteDB app](https://help.sumologic.com/docs/get-started/apps-integrations/) (coming soon) in your Sumo Logic account, you can import this JSON format template and use it as a starting point for visualizing your cluster data.
1. Click **Test Configuration** to make sure your connection is working.
1. Click **Create Configuration**.

  {{% /tab %}}

{{< /tabpane >}}

To edit a configuration, click the **...** button on the configuration and choose **Edit**.

To delete a configuration, click the **...** button on the configuration and choose **Delete**.

### Manage cluster metrics export

You can assign a metrics configuration to one or more clusters to begin exporting metrics from those clusters. You can also pause and resume metrics export for a cluster.

To assign an export configuration to a cluster, in the **Export Metrics by Cluster** table, in the **Export Configurations** column, choose a configuration for the cluster.

To remove an export configuration from a cluster, in the **Export Metrics by Cluster** table, set the **Export Configurations** column to **None**.

To pause or resume metrics export from a cluster, in the **Export Metrics by Cluster** table, click the **...** button for the cluster, and choose **Pause** or **Resume**.
