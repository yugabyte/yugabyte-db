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

You can export cluster metrics and logs to third-party tools for analysis and customization.

To export either metrics or logs from a cluster:

1. [Create an export configuration](#configure-integrations) for the integration you want to use. A configuration defines the sign in credentials and settings for the tool that you want to export to.

1. Using the configuration you created, connect your cluster.

    - [Export metrics](../metrics-export/)
    - [Export logs](../logging-export/)

    While the connection is active, metrics or logs are automatically streamed to the tool.

## Available integrations

Currently, you can export data to the following tools.

| Integration | Log export | Metric export |
| :---------- | :--------- | :------------ |
| [Datadog](#datadog) | Database query logs<br>Database audit logs | [Yes](https://docs.datadoghq.com/integrations/yugabytedb_managed/#data-collected) |
| [Grafana Cloud](#grafana-cloud) | | [Yes](https://grafana.com/grafana/dashboards/12620-yugabytedb/) |
| [Sumo Logic](#sumo-logic) | | Yes |
| [Prometheus](#prometheus) | | Yes |
| [VictoriaMetrics](#victoriametrics) | | Yes |
| [Google Cloud Storage](#google-cloud-storage) (GCS) | Database audit logs | |
<!--| [Dynatrace](#dynatrace) | | Yes |-->

## Manage integrations

Create and manage export configurations on the **Integrations** page.

![Integrations](/images/yb-cloud/managed-integrations.png)

The page lists the configured and available third-party integrations.

To view the configured integrations, click the **View** button for the integration.

To delete a configuration, click **View**, click the three dots, and choose **Delete configuration**. You can't delete a configuration that is assigned to a cluster.

Note that you can't modify an existing integration configuration. If you need to change an integration (for example, to replace or update an API key) for a particular tool, do the following:

1. Create a new configuration for the integration with the updated information.
1. Assign the new configuration to your clusters.
1. Unassign the old configuration from clusters.
1. Delete the old configuration.

Exporting cluster metrics and logs counts against your data transfer allowance. This may incur additional costs for network transfer, especially for cross-region and internet-based transfers, if usage exceeds your cluster allowance. Refer to [Data transfer costs](../../cloud-admin/cloud-billing-costs/#data-transfer-costs).

## Configure integrations

You can add and delete export configurations for the following tools. You can't delete a configuration that is assigned to a cluster.

### Datadog

The [Datadog](https://docs.datadoghq.com/) integration requires the following:

- Datadog account
- Datadog [API key](https://docs.datadoghq.com/account_management/api-app-keys/)

To create an export configuration, do the following:

1. On the **Integrations** page, click **Configure** for the **Datadog** integration or, if a configuration is already available, **Add Configuration**.
1. Enter a name for the configuration.
1. Enter your Datadog [API key](https://docs.datadoghq.com/account_management/api-app-keys/).
1. Choose the Datadog site to connect to, or choose Self-hosted and enter your URL.
1. Optionally, click **Download** to download the Datadog [dashboard](https://docs.datadoghq.com/dashboards/) template. You can import this JSON format template into your Datadog account and use it as a starting point for visualizing your cluster data in Datadog.
1. Click **Test Configuration** to make sure your connection is working.
1. Click **Create Configuration**.

### Grafana Cloud

The [Grafana Cloud](https://grafana.com/docs/grafana-cloud/) integration requires the following:

- Grafana Cloud account and stack. For best performance and lower data transfer costs, deploy your stack in the region closest to your cluster. For a list of supported regions, see the [Grafana Cloud documentation](https://grafana.com/docs/grafana-cloud/account-management/regional-availability/).
- Access policy token. You need to create an Access policy with metrics:write scope, and then add a token. For more information, see [Grafana Cloud Access Policies](https://grafana.com/docs/grafana-cloud/account-management/authentication-and-permissions/access-policies/authorize-services/) in the Grafana documentation.

To create an export configuration, do the following:

1. On the **Integrations** page, click **Configure** for the **Grafana Cloud** integration or, if a configuration is already available, **Add Configuration**.
1. Enter a name for the configuration.
1. Enter your organization name. This is displayed in the URL when you connect to your Grafana Cloud Portal (for example, `https://grafana.com/orgs/<organization-name>`).
1. Enter your Grafana Cloud Access policy token.
1. Enter your Grafana Cloud instance ID and zone. Obtain these by navigating to the Grafana Cloud portal, selecting your stack, and on the Grafana tile, clicking **Details**. They are displayed under **Instance Details**.
1. Optionally, click **Download** to download the Grafana Cloud dashboard template. You can [import this JSON format template](https://grafana.com/docs/grafana-cloud/visualizations/dashboards/manage-dashboards/#export-and-import-dashboards) into your Grafana account and use it as a starting point for visualizing your cluster data in Grafana. The dashboard is also available from the [Grafana Dashboards](https://grafana.com/grafana/dashboards/19887-yugabytedb-managed-clusters/) page.
1. Click **Test Configuration** to make sure your connection is working.
1. Click **Create Configuration**.

### Sumo Logic

The [Sumo Logic](https://www.sumologic.com) integration requires the following:

- Create an [access ID and access key](https://help.sumologic.com/docs/manage/security/access-keys/) on the **Preferences** page under your profile name.
- [Installation token](https://help.sumologic.com/docs/manage/security/installation-tokens/). These are available under **Administration > Security > Installation Tokens**.
- To use the dashboard template, [install the YugabyteDB app](https://help.sumologic.com/docs/get-started/apps-integrations/) (coming soon) in your Sumo Logic account.

To create an export configuration, do the following:

1. On the **Integrations** page, click **Configure** for the **Sumo Logic** integration or, if a configuration is already available, **Add Configuration**.
1. Enter a name for the configuration.
1. Enter your installation token, access ID, and access key.
1. Optionally, click **Download** to download the Sumo Logic dashboard template. After you install the [YugabyteDB app](https://help.sumologic.com/docs/get-started/apps-integrations/) (coming soon) in your Sumo Logic account, you can import this JSON format template and use it as a starting point for visualizing your cluster data.
1. Click **Test Configuration** to make sure your connection is working.
1. Click **Create Configuration**.

### Prometheus

Prometheus integration is only available for clusters deployed on AWS or GCP.

The [Prometheus](https://prometheus.io/docs/introduction/overview/) integration requires the following:

- Prometheus instance
  - Deployed in a VPC on AWS or GCP.
  - [OTLP Receiver](https://prometheus.io/docs/prometheus/latest/querying/api/#otlp-receiver) feature flag enabled.
  {{< note title="Note" >}}
How you enable the OTLP Receiver feature flag differs between Prometheus versions. Be sure to check the appropriate documentation for your version of Prometheus.
  {{< /note >}}
  - Publicly-accessible endpoint URL that resolves to the private IP of the Prometheus instance.

    The DNS for the endpoint must be in a publicly accessible DNS record, allowing it to resolve globally. This typically involves adding the URL to a public DNS zone. For example, in AWS, this would mean adding the URL to a Public Hosted Zone in Route 53. To confirm that the address is publicly resolvable, you can use a tool like nslookup.

  - VPC hosting the Prometheus instance has the following Inbound Security Group rules:
    - Allow HTTP inbound traffic on port 80 for Prometheus endpoint URL (HTTP).
    - Allow HTTPS inbound traffic on port 443 for Prometheus endpoint URL (HTTPS).

    See [Control traffic to your AWS resources using security groups](https://docs.aws.amazon.com/vpc/latest/userguide/vpc-security-groups.html) in the AWS documentation, or [VPC firewall rules](https://cloud.google.com/firewall/docs/firewalls) in the Google Cloud documentation.

- YugabyteDB Aeon cluster from which you want to export metrics
  - Cluster deployed in VPCs on AWS, or a VPC in GCP. See [VPCs](../../cloud-basics/cloud-vpcs/cloud-add-vpc/).
  - VPCs are peered with the VPC hosting Prometheus. See [Peer VPCs](../../cloud-basics/cloud-vpcs/cloud-add-vpc-aws/).

    As each region of a cluster deployed in AWS has its own VPC, make sure that _all_ the VPCs are peered and allow inbound access from Prometheus; this also applies to regions you add or change after deployment, and to read replicas. In GCP, clusters are deployed in a single VPC.

  For information on VPC networking in YugabyteDB Aeon, see [VPC network overview](../../cloud-basics/cloud-vpcs/cloud-vpc-intro/).

To create an export configuration, do the following:

1. On the **Integrations** page, click **Configure** for the **Prometheus** integration or, if a configuration is already available, **Add Configuration**.
1. Enter a name for the configuration.
1. Enter the endpoint URL of the Prometheus instance.

    The URL must be in the form

    ```sh
    http://<prometheus-endpoint-host-address>/api/v1/otlp
    ```

1. Click **Create Configuration**.

### VictoriaMetrics

VictoriaMetrics integration is only available for clusters deployed on AWS or GCP.

The [VictoriaMetrics](https://docs.victoriametrics.com/) integration requires the following:

- VictoriaMetrics self-hosted instance (Enterprise and Cloud are not currently supported)
  - Deployed in a VPC on AWS or GCP.
  - Publically-accessible endpoint URL that resolves to the private IP of the VictoriaMetrics instance.

    The DNS for the endpoint must be in a publicly accessible DNS record, allowing it to resolve globally. This typically involves adding the URL to a public DNS zone. To confirm that the address is publicly resolvable, you can use a tool like nslookup.

    The URL must be in the form as described in [How to use OpenTelemetry metrics with VictoriaMetrics](https://docs.victoriametrics.com/guides/getting-started-with-opentelemetry/).

  - VPC hosting the VictoriaMetrics instance has the following Inbound Security Group rules:
    - Allow HTTP inbound traffic on port 80 for VictoriaMetrics endpoint URL (HTTP)
    - Allow HTTPS inbound traffic on port 443 for VictoriaMetrics endpoint URL (HTTPS)

    See [Control traffic to your AWS resources using security groups](https://docs.aws.amazon.com/vpc/latest/userguide/vpc-security-groups.html) in the AWS documentation, or [VPC firewall rules](https://cloud.google.com/firewall/docs/firewalls) in the Google Cloud documentation.

- YugabyteDB Aeon cluster from which you want to export metrics
  - Cluster deployed in VPCs on AWS, or a VPC in GCP. See [VPCs](../../cloud-basics/cloud-vpcs/cloud-add-vpc/).
  - VPCs are peered with the VPC hosting VictoriaMetrics. See [Peer VPCs](../../cloud-basics/cloud-vpcs/cloud-add-vpc-aws/).

  As each region of a cluster deployed in AWS has its own VPC, make sure that _all_ the VPCs are peered and allow inbound access from VictoriaMetrics; this also applies to regions you add or change after deployment, and to read replicas. In GCP, clusters are deployed in a single VPC.

  For information on VPC networking in YugabyteDB Aeon, see [VPC network overview](../../cloud-basics/cloud-vpcs/cloud-vpc-intro/).

To create an export configuration, do the following:

1. On the **Integrations** page, click **Configure** for the **VictoriaMetrics** integration or, if a configuration is already available, **Add Configuration**.
1. Enter a name for the configuration.
1. Enter the endpoint URL of the VictoriaMetrics instance.

    The URL must be in the form

    ```sh
    http://<victoria-metrics-endpoint-host-address>/opentelemetry
    ```

1. Click **Create Configuration**.

### Google Cloud Storage

The [Google Cloud Storage](https://cloud.google.com/storage) (GCS) integration requires the following:

- A service account that has been granted the `logging.logWriter` permission.
- Service account credentials. These credentials are used to authorize your use of the storage. This is the key file (JSON) that you downloaded when creating credentials for the service account. For more information, refer to [Create credentials for a service account](https://developers.google.com/workspace/guides/create-credentials#create_credentials_for_a_service_account) in the Google documentation.

To create an export configuration, do the following:

1. On the **Integrations** page, click **Configure** for the **Google Cloud Storage** integration or, if a configuration is already available, **Add Configuration**.
1. Enter a name for the configuration.
1. Upload the JSON key file.
1. Click **Test Configuration** to make sure your connection is working.
1. Click **Create Configuration**.

<!--### Dynatrace

The [Dynatrace](https://www.dynatrace.com) integration requires the following:

- Publically-accessible endpoint URL of your Dynatrace instance. The endpoint URL is the URL of your Dynatrace instance.
- [Dynatrace Access Token](https://docs.dynatrace.com/docs/manage/identity-access-management/access-tokens-and-oauth-clients/access-tokens#create-api-token). The access token needs to have ingest metrics, ingest logs, ingest OpenTelemetry traces, and read API tokens [scope](https://docs.dynatrace.com/docs/manage/identity-access-management/access-tokens-and-oauth-clients/access-tokens#scopes).

To create an export configuration, do the following:

1. On the **Integrations** page, click **Configure** for the Dynatrace provider or, if a configuration is already available, **Add Configuration**.
1. Enter a name for the configuration.
1. Enter the Dynatrace Endpoint URL.
1. Enter your Dynatrace Access Token.
1. Optionally, click **Download** to download the Dynatrace dashboard template. You can import this template in your Dynatrace account and use it as a starting point for visualizing your cluster data.
1. Click **Test Configuration** to make sure your connection is working.
1. Click **Create Configuration**.-->

## Next steps

- [Export metrics from a cluster](../metrics-export/)
- [Export logs from a cluster](../logging-export/)
