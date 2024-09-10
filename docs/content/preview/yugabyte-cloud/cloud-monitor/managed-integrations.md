---
title: Integrate with third-party tools in YugabyteDB Aeon
headerTitle: Integrations
linkTitle: Integrations
description: Set up links to third-party tools in YugabyteDB Aeon.
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
- [Prometheus](https://prometheus.io/docs/introduction/overview/) {{<badge/tp>}}
- [VictoriaMetrics](https://docs.victoriametrics.com/) {{<badge/tp>}}

Exporting cluster metrics and logs counts against your data transfer allowance. This may incur additional costs for network transfer, especially for cross-region and internet-based transfers, if usage exceeds your cluster allowance. Refer to [Data transfer costs](../../cloud-admin/cloud-billing-costs/#data-transfer-costs).

For information on how to export metrics and logs from a cluster using an integration, refer to [Export metrics](../metrics-export/) and [Export logs](../logging-export/).

## Configure integrations

Create and manage export configurations on the **Integrations** page.

![Integrations](/images/yb-cloud/managed-integrations.png)

The page lists the configured and available third-party integrations.

### Manage integrations

You can add and delete export configurations for the following tools. You can't delete a configuration that is assigned to a cluster.

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

  {{% tab header="Prometheus" lang="prometheus" %}}

Prometheus integration is {{<badge/tp>}} and only available for clusters deployed on AWS.

The Prometheus integration requires the following:

- Prometheus instance
  - deployed in a VPC on AWS
  - [OLTP Receiver](https://prometheus.io/docs/prometheus/latest/querying/api/#otlp-receiver) feature flag enabled
  - publically-accessible endpoint URL that resolves to the private IP of the Prometheus instance; the DNS for the endpoint must be in a public hosted zone in AWS.
  - VPC hosting the Prometheus instance has the following Inbound Security Group rules:
    - Allow HTTP inbound traffic on port 80 for Prometheus endpoint URL (HTTP)
    - Allow HTTPS inbound traffic on port 443 for Prometheus endpoint URL (HTTPS)

    See [Control traffic to your AWS resources using security groups](https://docs.aws.amazon.com/vpc/latest/userguide/vpc-security-groups.html) in the AWS documentation.

- YugabyteDB Aeon cluster from which you want to export metrics
  - the cluster is [deployed in VPCs](../../cloud-basics/cloud-vpcs/cloud-add-vpc/) on AWS
  - each region VPC is peered with the VPC hosting Prometheus. See [Peer VPCs](../../cloud-basics/cloud-vpcs/cloud-add-vpc-aws/).

  As each region of a cluster deployed in AWS has its own VPC, make sure that all the VPCs are peered and allow inbound access from Prometheus; this also applies to regions you add or change after deployment, and to read replicas. For information on VPC networking in YugabyteDB Aeon, see [VPC network overview](../../cloud-basics/cloud-vpcs/cloud-vpc-intro/).

To create an export configuration, do the following:

1. On the **Integrations** page, click **Configure** for the Prometheus provider or, if a configuration is already available, **Add Configuration**.
1. Enter a name for the configuration.
1. Enter the endpoint URL of the Prometheus instance.

    The URL must be in the form

    ```sh
    http://<prometheus-endpoint-host-address>/api/v1/otlp
    ```

1. Click **Create Configuration**.

  {{% /tab %}}

  {{% tab header="VictoriaMetrics" lang="victoria" %}}

VictoriaMetrics integration is {{<badge/tp>}} and only available for clusters deployed on AWS.

The VictoriaMetrics integration requires the following:

- VictoriaMetrics instance
  - deployed in a VPC on AWS
  - publically-accessible endpoint URL that resolves to the private IP of the VictoriaMetrics instance; the DNS for the endpoint must be in a public hosted zone in AWS. The URL must be in the form as described in [How to use OpenTelemetry metrics with VictoriaMetrics](https://docs.victoriametrics.com/guides/getting-started-with-opentelemetry/).
  - VPC hosting the VictoriaMetrics instance has the following Inbound Security Group rules:
    - Allow HTTP inbound traffic on port 80 for VictoriaMetrics endpoint URL (HTTP)
    - Allow HTTPS inbound traffic on port 443 for VictoriaMetrics endpoint URL (HTTPS)

    See [Control traffic to your AWS resources using security groups](https://docs.aws.amazon.com/vpc/latest/userguide/vpc-security-groups.html) in the AWS documentation.

- YugabyteDB Aeon cluster from which you want to export metrics
  - the cluster is [deployed in VPCs](../../cloud-basics/cloud-vpcs/cloud-add-vpc/) on AWS
  - each region VPC is peered with the VPC hosting VictoriaMetrics. See [Peer VPCs](../../cloud-basics/cloud-vpcs/cloud-add-vpc-aws/).

  As each region of a cluster deployed in AWS has its own VPC, make sure that all the VPCs are peered and allow inbound access from VictoriaMetrics; this also applies to regions you add or change after deployment, and to read replicas. For information on VPC networking in YugabyteDB Aeon, see [VPC network overview](../../cloud-basics/cloud-vpcs/cloud-vpc-intro/).

To create an export configuration, do the following:

1. On the **Integrations** page, click **Configure** for the VictoriaMetrics provider or, if a configuration is already available, **Add Configuration**.
1. Enter a name for the configuration.
1. Enter the endpoint URL of the VictoriaMetrics instance.

    The URL must be in the form

    ```sh
    http://<victoria-metrics-endpoint-host-address>/api/v1/otlp
    ```

1. Click **Create Configuration**.

  {{% /tab %}}

{{< /tabpane >}}

To view the configurations, click the **View** button for the provider.

To delete a configuration, click **View** and choose **Delete**.

Note that you can't modify an existing integration configuration. If you need to change an integration (for example, to replace or update an API key) for a particular tool, do the following:

1. Create a new configuration for the provider with the updated information.
1. Assign the new configuration to your clusters.
1. Unassign the old configuration from clusters.
1. Delete the old configuration.
