---
title: Integrate with third-party tools in YugabyteDB Anywhere
headerTitle: Manage export configurations
linkTitle: Export configuration
description: Set up links to third-party tools in YugabyteDB Anywhere.
headcontent: Set up links to third-party tools
badges: ea
menu:
  preview_yugabyte-platform:
    identifier: anywhere-export-configurations
    parent: alerts-monitoring
    weight: 80
type: docs
---

You can export universe logs to third-party tools for analysis and customization. Exporting data is a two-stage process:

1. Create an export configuration. A configuration defines the sign in credentials and settings for the tool that you want to export to.
1. Use the configuration to export data from a universe. While the connection is active, metrics or logs are automatically streamed to the tool.

Currently, you can export data to the following tools:

- [Datadog](https://docs.datadoghq.com/)
- [Splunk](https://www.splunk.com/en_us/solutions/opentelemetry.html)
- [AWS CloudWatch](https://docs.aws.amazon.com/AmazonCloudWatch/latest/monitoring/WhatIsCloudWatch.html)
- [GCP Cloud Logging](https://cloud.google.com/logging/)

For information on how to export logs from a universe using an export configuration, refer to [Export logs](../universe-logging/).

## Prerequisites

Export configuration is {{<badge/ea>}}. To enable export configuration management, set the **Enable DB Audit Logging** Global Configuration option (config key `yb.universe.audit_logging_enabled`) to true. Refer to [Manage runtime configuration settings](../../../administer-yugabyte-platform/manage-runtime-config/). Note that only a Super Admin user can modify Global configuration settings. The flag can't be turned off if audit logging is enabled on a universe.

## Configure integrations

Create and manage export configurations on the **Integrations > Logs** page.

![Export configurations](/images/yp/export-configurations.png)

The page lists the configured and available third-party integrations.

### Manage integrations

You can add and delete export configurations for the following tools. You can't delete a configuration that is in use by a universe.

{{< tabpane text=true >}}

  {{% tab header="Datadog" lang="datadog" %}}

The Datadog export configuration requires the following:

- Datadog account
- Datadog [API key](https://docs.datadoghq.com/account_management/api-app-keys/)

To create an export configuration, do the following:

1. On the **Integrations** page, on the **Log** tab, click **Create Export Configuration**, and choose **Datadog**.
1. Enter a name for the configuration.
1. Enter your Datadog [API key](https://docs.datadoghq.com/account_management/api-app-keys/).
1. Choose the Datadog site to connect to, or choose Self-hosted and enter your URL.
1. Click **Create Configuration**.

  {{% /tab %}}

  {{% tab header="Splunk" lang="splunk" %}}

The Splunk export configuration requires the following:

- Splunk access token
- Endpoint URL

To create an export configuration, do the following:

1. On the **Integrations** page, on the **Log** tab, click **Create Export Configuration**, and choose **Splunk**.
1. Enter a name for the configuration.
1. Enter your Splunk [Access token](https://docs.splunk.com/observability/en/admin/authentication/authentication-tokens/org-tokens.html).
1. Enter the Endpoint URL.
1. Optionally, enter the Source, Source Type, and Index.
1. Click **Validate and Create Configuration**.

  {{% /tab %}}

  {{% tab header="AWS" lang="aws" %}}

The AWS CloudWatch export configuration requires the following:

- Access Key ID and Secret Access Key for the AWS Service Account

To create an export configuration, do the following:

1. On the **Integrations** page, on the **Log** tab, click **Create Export Configuration**, and choose **AWS CloudWatch**.
1. Enter a name for the configuration.
1. Enter your access key and secret access key.
1. Enter the Log group and Log stream.
1. Enter the region.
1. Optionally, provide the ARN for the role.
1. Optionally, provide an endpoint URL.
1. Click **Validate and Create Configuration**.

  {{% /tab %}}

  {{% tab header="GCP" lang="gcp" %}}

The GCP Cloud Logging export configuration requires the following:

- Google Service Account credentials JSON

To create an export configuration, do the following:

1. On the **Integrations** page, on the **Log** tab, click **Create Export Configuration**, and choose **GCP Cloud Logging**.
1. Enter a name for the configuration.
1. Optionally, provide the project name.
1. Upload the JSON file containing your Google Cloud credentials.
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
