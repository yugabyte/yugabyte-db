---
title: Integrate with third-party tools in YugabyteDB Anywhere
headerTitle: Manage export configurations
linkTitle: Log and metrics export
description: Set up links to third-party tools in YugabyteDB Anywhere.
headcontent: Set up links to third-party tools
menu:
  stable_yugabyte-platform:
    identifier: anywhere-export-configurations
    parent: alerts-monitoring
    weight: 80
type: docs
---

You can export universe metrics and logs to third-party tools for analysis and customization.

To export either metrics or logs from a universe:

1. [Create an export configuration](#configure-integrations) for the integration you want to use. A configuration defines the sign in credentials and settings for the tool that you want to export to.

1. Using the configuration you created, connect your cluster.

    - [Export metrics](../anywhere-metrics-export/)
    - [Export logs](../universe-logging/)

    While the connection is active, metrics or logs are automatically streamed to the tool.

## Available integrations

Currently, you can export data to the following tools:

| Integration | Log export | Metric export |
| :---------- | :--------- | :------------ |
| [Datadog](https://docs.datadoghq.com/) | Database audit logs | Yes |
| [Splunk](https://www.splunk.com/en_us/solutions/opentelemetry.html) | Database audit logs | |
| [AWS CloudWatch](https://docs.aws.amazon.com/AmazonCloudWatch/latest/monitoring/WhatIsCloudWatch.html) | Database audit logs | |
| [Google Cloud Logging](https://cloud.google.com/logging/) | Database audit logs | |
| [Dynatrace](#dynatrace) | | Yes |

## Best practices

To limit performance impact and control costs, locate export configurations in a region close to your universe(s).

## Configure integrations

Create and manage export configurations on the **Integrations > Log** page.

<!--![Export configurations](/images/yp/export-configurations.png)-->

The page lists the configured and available third-party integrations.

### Manage integrations

You can add and delete export configurations for the following tools. You can't delete a configuration that is in use by a universe.

{{< tabpane text=true >}}

  {{% tab header="Datadog" lang="datadog" %}}

The Datadog export configuration requires the following:

- Datadog account
- Datadog [API key](https://docs.datadoghq.com/account_management/api-app-keys/)

To create an export configuration, do the following:

1. On the **Integrations** page, on the **Log & Metrics Export** tab, click **Add Configuration**.
1. Enter a name for the configuration.
1. Choose **Datadog**.
1. Enter your Datadog [API key](https://docs.datadoghq.com/account_management/api-app-keys/).
1. Choose the Datadog site to connect to, or choose Self-hosted and enter your URL.
1. Click **Create Configuration**.

  {{% /tab %}}

  {{% tab header="Splunk" lang="splunk" %}}

The Splunk export configuration requires the following:

- Splunk access token
- Endpoint URL

To create an export configuration, do the following:

1. On the **Integrations** page, on the **Log & Metrics Export** tab, click **Add Configuration**.
1. Enter a name for the configuration.
1. Choose **Splunk**.
1. Enter your Splunk [Access token](https://docs.splunk.com/observability/en/admin/authentication/authentication-tokens/org-tokens.html).
1. Enter the Endpoint URL.
1. Optionally, enter the Source, Source Type, and Index.
1. Click **Validate and Create Configuration**.

  {{% /tab %}}

  {{% tab header="AWS" lang="aws" %}}

The AWS CloudWatch export configuration requires the following:

- Access Key ID and Secret Access Key for the AWS Service Account
- The [log group and log stream](https://docs.aws.amazon.com/AmazonCloudWatch/latest/logs/Working-with-log-groups-and-streams.html)
- The AWS region where the stream is located

To create an export configuration, do the following:

1. On the **Integrations** page, on the **Log & Metrics Export** tab, click **Add Configuration**.
1. Enter a name for the configuration.
1. Choose **AWS CloudWatch**.
1. Enter your access key and secret access key.
1. Enter the Log group and Log stream.
1. Enter the region.
1. Optionally, provide the ARN for the role.
1. Optionally, provide an endpoint URL.
1. Click **Validate and Create Configuration**.

  {{% /tab %}}

  {{% tab header="GCP" lang="gcp" %}}

The Google Cloud Logging export configuration requires the following:

- Google Service Account with the `roles/logging.logWriter` role.
- The Service Account credentials JSON key. The credentials should be scoped to the project where the log group is located.

To create an export configuration, do the following:

1. On the **Integrations** page, on the **Log & Metrics Export** tab, click **Add Configuration**.
1. Enter a name for the configuration.
1. Choose **GCP Cloud Logging**.
1. Optionally, provide the project name.
1. Upload the JSON file containing your Google Cloud credentials.
1. Click **Validate and Create Configuration**.

  {{% /tab %}}

  {{% tab header="Dynatrace" lang="dynatrace" %}}

The [Dynatrace](https://www.dynatrace.com) integration requires the following:

- Publically-accessible endpoint URL of your Dynatrace instance. The endpoint URL is the URL of your Dynatrace instance.
- [Dynatrace Access Token](https://docs.dynatrace.com/docs/manage/identity-access-management/access-tokens-and-oauth-clients/access-tokens#create-api-token). The access token needs to have ingest metrics, ingest logs, ingest OpenTelemetry traces, and read API tokens [scope](https://docs.dynatrace.com/docs/manage/identity-access-management/access-tokens-and-oauth-clients/access-tokens#scopes).

To create an export configuration, do the following:

1. On the **Integrations** page, on the **Log & Metrics Export** tab, click **Add Configuration**.
1. Enter a name for the configuration.
1. Choose **Dynatrace**.
1. Enter the Dynatrace Endpoint URL.
1. Enter your Dynatrace Access Token.
1. Click **Validate and Create Configuration**.

  {{% /tab %}}

{{< /tabpane >}}

To view configuration details, select the configuration.

To delete a configuration, click **Actions** and choose **Delete**.

You can't modify an existing configuration. If you need to change the configuration (for example, to replace or update an API key) for a particular tool, do the following:

1. Create a new configuration for the provider with the updated information.
1. Assign the new configuration to your universes.
1. Unassign the old configuration from universes.
1. Delete the old configuration.

## Next steps

- [Export metrics from a universe](../anywhere-metrics-export/)
- [Export logs from a universe](../universe-logging/)
