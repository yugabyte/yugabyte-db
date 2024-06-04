---
title: Integrate with third-party tools for exporting cluster data
headerTitle: Integrations
linkTitle: Integrations
description: Set up links to third-party tools.
headcontent: Set up links to third-party tools
menu:
  preview_yugabyte-cloud:
    identifier: managed-integrations
    parent: cloud-monitor
    weight: 600
type: docs
---

You can export cluster metrics and logs to third-party tools for analysis and customization. Exporting data is a two-stage process:

1. Create an export configuration. A configuration defines the sign in credentials and settings for the tool that you want to export to.
1. Use the configuration to export data from a cluster. While the connection is active, metrics or logs are automatically streamed to the tool.

Currently, you can export data to the following tools:

- [Datadog](https://docs.datadoghq.com/)
- [Grafana Cloud](https://grafana.com/docs/grafana-cloud/)
- [Sumo Logic](https://www.sumologic.com)

Exporting cluster metrics and logs counts against your data transfer allowance. This may incur additional costs for network transfer, especially for cross-region and internet-based transfers, if usage exceeds your cluster allowance. Refer to [Data transfer costs](../../cloud-admin/cloud-billing-costs/#data-transfer-costs).

For information on how to export metrics and logs from a cluster using an integration, refer to [Export metrics](../metrics-export/) and [Export logs](../logging-export/).

## Configure integrations

Create and manage export configurations on the **Integrations** page.

![Integrations](/images/yb-cloud/managed-integrations.png)

The page lists the configured and available third-party integrations.

### Manage integrations

You can add, edit, and delete export configurations for the following tools. You can't delete a configuration that is assigned to a cluster.

{{< tabpane text=true >}}

  {{% tab header="Datadog" lang="datadog" %}}

The Datadog integration requires the following:

- Datadog account
- Datadog [API key](https://docs.datadoghq.com/account_management/api-app-keys/)

To create an export configuration, do the following:

1. On the **Integrations** page, click **Configure** for the Datadog provider or, if a configuration is already available, **Add Configuration**.
1. Enter a name for the configuration.
1. Enter your Datadog [API key](https://docs.datadoghq.com/account_management/api-app-keys/).
1. Choose the Datadog site to connect to, or choose Self-hosted and enter your URL.
1. Optionally, click **Download** to download the Datadog [dashboard](https://docs.datadoghq.com/dashboards/) template. You can import this JSON format template into your Datadog account and use it as a starting point for visualizing your cluster data in Datadog.
1. Click **Test Configuration** to make sure your connection is working.
1. Click **Create Configuration**.

  {{% /tab %}}

  {{% tab header="Grafana Cloud" lang="grafana" %}}

The Grafana Cloud integration requires the following:

- Grafana Cloud account and stack. For best performance and lower data transfer costs, deploy your stack in the region closest to your cluster. For a list of supported regions, see the [Grafana Cloud documentation](https://grafana.com/docs/grafana-cloud/account-management/regional-availability/).
- Access policy token. You need to create an Access policy with metrics:write scope, and then add a token. For more information, see [Grafana Cloud Access Policies](https://grafana.com/docs/grafana-cloud/account-management/authentication-and-permissions/access-policies/authorize-services/) in the Grafana documentation.

To create an export configuration, do the following:

1. On the **Integrations** page, click **Configure** for the Grafana Cloud provider or, if a configuration is already available, **Add Configuration**.
1. Enter a name for the configuration.
1. Enter your organization name. This is displayed in the URL when you connect to your Grafana Cloud Portal (for example, `https://grafana.com/orgs/<organization-name>`).
1. Enter your Grafana Cloud [Access policy token](#grafana-cloud).
1. Enter your Grafana Cloud instance ID and zone. Obtain these by navigating to the Grafana Cloud portal, selecting your stack, and on the Grafana tile, clicking **Details**. They are displayed under **Instance Details**.
1. Optionally, click **Download** to download the Grafana Cloud dashboard template. You can [import this JSON format template](https://grafana.com/docs/grafana-cloud/visualizations/dashboards/manage-dashboards/#export-and-import-dashboards) into your Grafana account and use it as a starting point for visualizing your cluster data in Grafana. The dashboard is also available from the [Grafana Dashboards](https://grafana.com/grafana/dashboards/19887-yugabytedb-managed-clusters/) page.
1. Click **Test Configuration** to make sure your connection is working.
1. Click **Create Configuration**.

  {{% /tab %}}

  {{% tab header="Sumo Logic" lang="sumo" %}}

The Sumo Logic integration requires the following:

- Create an [access ID and access key](https://help.sumologic.com/docs/manage/security/access-keys/) on the **Preferences** page under your profile name.
- [Installation token](https://help.sumologic.com/docs/manage/security/installation-tokens/). These are available under **Administration > Security > Installation Tokens**.
- To use the dashboard template, [install the YugabyteDB app](https://help.sumologic.com/docs/get-started/apps-integrations/) (coming soon) in your Sumo Logic account.

To create an export configuration, do the following:

1. On the **Integrations** page, click **Configure** for the Sumo Logic provider or, if a configuration is already available, **Add Configuration**.
1. Enter a name for the configuration.
1. Enter your installation token, access ID, and access key.
1. Optionally, click **Download** to download the Sumo Logic dashboard template. After you install the [YugabyteDB app](https://help.sumologic.com/docs/get-started/apps-integrations/) (coming soon) in your Sumo Logic account, you can import this JSON format template and use it as a starting point for visualizing your cluster data.
1. Click **Test Configuration** to make sure your connection is working.
1. Click **Create Configuration**.

  {{% /tab %}}

{{< /tabpane >}}

To view the configurations, click the **View** button for the provider.

To delete a configuration, click **View** and choose **Delete**.
