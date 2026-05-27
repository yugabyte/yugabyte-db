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
rightNav:
  hideH4: true
type: docs
---

You can export universe metrics and logs to third-party tools for analysis and customization.

To export either metrics or logs from a universe:

1. [Create an export configuration](#configure-integrations) for the integration you want to use. A configuration defines the sign in credentials and settings for the tool that you want to export to.

1. Using the configuration you created, connect your cluster.

    - [Export metrics](../anywhere-metrics-export/)
    - [Export logs](../universe-logging/)

    While the connection is active, metrics or logs are automatically streamed to the tool.

To be able to export logs from Kubernetes universes, ensure the OpenTelemetry Operator is installed. Refer to [OpenTelemetry Operator for Kubernetes](https://opentelemetry.io/docs/platforms/kubernetes/operator/#getting-started) in the OpenTelemetry documentation. Metrics export is not supported on Kubernetes.

## Available integrations

Currently, you can export data to the following tools:

| Integration | Log export | Metric export |
| :---------- | :--------- | :------------ |
| [Datadog](https://docs.datadoghq.com/) | Database audit logs | Yes |
| [Splunk](https://www.splunk.com/en_us/solutions/opentelemetry.html) | Database audit logs | |
| [AWS CloudWatch](https://docs.aws.amazon.com/AmazonCloudWatch/latest/monitoring/WhatIsCloudWatch.html) | Database audit logs | |
| [Google Cloud Logging](https://cloud.google.com/logging/) | Database audit logs | |
| [Dynatrace](#dynatrace) | | Yes |
| [Loki](#loki) | Database audit logs | |
| [OTLP](#otlp) | Database audit logs | Yes |

## Best practices

To limit performance impact and control costs, locate export configurations in a region close to your universe(s).

## Manage integrations

Create and manage export configurations on the **Integrations > Log & Metrics Export** page.

The page lists the configured third-party integrations.

To view details for a configuration, select it in the list.

To delete a configuration, click the three dots, and choose **Delete configuration**. You can't delete a configuration that is assigned to a universe.

Note that you can't modify an existing configuration. If you need to change an configuration (for example, to replace or update an API key) for a particular tool, do the following:

1. Create a new configuration for the integration with the updated information.
1. Assign the new configuration to your universes.
1. Unassign the old configuration from universes.
1. Delete the old configuration.

## Configure integrations

You can add and delete export configurations for the following tools. You can't delete a configuration that is in use by a universe.

### Datadog

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

### Splunk

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

### AWS

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

### Google Cloud Logging

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

### Dynatrace

The [Dynatrace](https://www.dynatrace.com) integration requires the following:

- Publically-accessible [OTLP endpoint URL](https://docs.dynatrace.com/docs/ingest-from/opentelemetry/otlp-api#export-to-dynatrace) of your Dynatrace instance. The endpoint URL is the URL of your Dynatrace instance. For example:

  `https://{your-environment-id}.live.dynatrace.com/api/v2/otlp`

  Note that if you copy your Dynatrace environment ID from the browser address bar, make sure to remove `.apps`.

- [Dynatrace Access Token](https://docs.dynatrace.com/docs/manage/identity-access-management/access-tokens-and-oauth-clients/access-tokens#create-api-token). The access token needs to have ingest metrics, ingest logs, ingest OpenTelemetry traces, and read API tokens [scope](https://docs.dynatrace.com/docs/manage/identity-access-management/access-tokens-and-oauth-clients/access-tokens#scopes).

To create an export configuration, do the following:

1. On the **Integrations** page, on the **Log & Metrics Export** tab, click **Add Configuration**.
1. Enter a name for the configuration.
1. Choose **Dynatrace**.
1. Enter the Dynatrace Endpoint URL.
1. Enter your Dynatrace Access Token.
1. Click **Validate and Create Configuration**.

### Loki

[Grafana Loki](https://grafana.com/docs/loki/latest/) is a log aggregation system designed to store and query logs. YugabyteDB Anywhere can export database audit logs to a Loki-compatible endpoint (including self-hosted Loki and [Grafana Cloud](https://grafana.com/docs/grafana-cloud/send-data/logs/logs-with-loki/)).

#### Prerequisites

- Enable the Loki sink by setting the **Allow Loki Exporter in Telemetry Provider** Global Configuration option (config key `yb.telemetry.allow_loki`) to `true`.

    Refer to [Manage runtime configuration settings](../../administer-yugabyte-platform/manage-runtime-config/).

    The flag is enforced when you create or delete a Loki telemetry provider.

- A reachable Loki endpoint.

#### Create a Loki export configuration

To create an export configuration, do the following:

1. On the **Integrations** page, on the **Log & Metrics Export** tab, click **Add Configuration**.
1. Enter a name for the configuration.
1. Choose **Loki**.
1. Fill in the Loki-specific fields:
   - Enter the **Loki endpoint** URL. For example, `http://<loki-url>:<loki-port>`.
   - **Organization / Tenant ID** (optional) — Organization ID is required when multi-tenancy is set up in Loki. Optional for Grafana Cloud as the authentication reroutes requests according to scope.
   - **Authentication Type**: **Basic Auth** or **No Auth**. If you choose **Basic Auth**, enter a **Username** and **Password**.
1. Click **Validate and Create Configuration**.

{{< note title="Loki configurations are immutable" >}}

After you create a configuration, you cannot edit it. To change settings, create a new configuration, reassign universes, and delete the old configuration.

{{< /note >}}

After the configuration is created, attach it to a universe using the [database log export](../universe-logging/) workflow.

For log export on Kubernetes universes, ensure the [OpenTelemetry Operator](https://opentelemetry.io/docs/platforms/kubernetes/operator/#getting-started) is installed on the cluster.

### OTLP

YugabyteDB Anywhere supports [OTLP](https://opentelemetry.io/docs/) (OpenTelemetry Protocol) as a generic telemetry provider sink. An OTLP telemetry provider lets a universe stream database audit logs, and database metrics to any OTLP-compatible receiver using the standard OpenTelemetry wire format.

The OTLP sink is vendor-agnostic and works with any backend that speaks OTLP, including (but not limited to) [Cribl](https://cribl.io/), [Grafana Cloud](https://grafana.com/docs/grafana-cloud/), [New Relic](https://docs.newrelic.com/docs/opentelemetry/opentelemetry-introduction/), [Prometheus](https://prometheus.io/docs/guides/opentelemetry/) (3.0+), [VictoriaMetrics](https://docs.victoriametrics.com/guides/getting-started-with-opentelemetry/), and [Sumo Logic](https://help.sumologic.com/docs/send-data/opentelemetry-for-logs/).

The same OTLP telemetry provider can be reused for [database audit logging](../universe-logging/), [database metrics export](../anywhere-metrics-export/), or both. OTLP uses the same OpenTelemetry Collector and universe export workflows as other telemetry providers.

{{< note title="Enable OTLP before configuring" >}}

Set the **OTLP Exporter for Telemetry Provider** Global Configuration option (config key `yb.telemetry.allow_otlp`), which defaults to `false`. Refer to [Manage runtime configuration settings](../../administer-yugabyte-platform/manage-runtime-config/).

- Note that till the flag is set to true, any REST API create and delete requests for OTLP telemetry providers return HTTP 400 with: `OTLP Exporter for Telemetry Provider is not enabled. Please set the runtime flag 'yb.telemetry.allow_otlp' to true.`

The flag is enforced when you create or delete an OTLP telemetry provider.

{{< /note >}}

#### Prerequisites

- Enable the OTLP feature as described in the preceding note.
- A reachable OTLP-compatible receiver and credentials if required (Basic Auth username and password, or a bearer token).

#### Create an OTLP export configuration

To create an export configuration, do the following:

1. On the **Integrations** page, on the **Log & Metrics Export** tab, click **Add Configuration**.
1. Enter a name for the configuration.
1. Choose **OTLP**.
1. Fill in the OTLP-specific fields:
   - Enter the OTLP receiver **Endpoint URL**.
   - **Protocol**: gRPC (default) or HTTP.
   - **Authentication Type**:
     - **No Auth** (default): no credentials.
     - **Basic Auth**: username and password.
     - **Bearer Token**
   - **Logs Endpoint** (HTTP only, optional): override URL for logs (for example, `https://example.com:4318/v1/logs`).
   - **Metrics Endpoint** (HTTP only, optional): override URL for metrics (for example, `https://example.com:4318/v1/metrics`).
   - **Timeout (seconds)**: default is `5`.
1. Click **Validate and Create Configuration**.

{{< note title="OTLP configurations are immutable" >}}

After you create a configuration, you cannot edit it. To change settings, create a new configuration, reassign universes, and delete the old configuration.

{{< /note >}}

After the configuration is created, attach it to a universe using the universe [database log export](../universe-logging/) workflow (for audit and PostgreSQL query logs) or [database metrics export](../anywhere-metrics-export/) workflow (for metrics).

Concrete endpoint URLs and auth schemes vary by OTLP backend; consult your receiver vendor's documentation or [Integrations in YugabyteDB Aeon](../../../yugabyte-cloud/cloud-monitor/managed-integrations/) for the correct OTLP address.

#### Unsupported scenarios

- Per-signal endpoint overrides (`logsEndpoint`, `metricsEndpoint`) are allowed only when **Protocol** is **HTTP**. A gRPC provider that sets either field is rejected.
- For the **HTTP** protocol, when log export is enabled the OpenTelemetry Collector appends `/v1/logs` to the configured endpoint. Configure the endpoint without that suffix unless you use the explicit **Logs Endpoint** override.
- Kubernetes support follows the same rules as the rest of YugabyteDB Anywhere OpenTelemetry export: [metrics export](../anywhere-metrics-export/#limitations) is not supported on Kubernetes; [log export](../universe-logging/#prerequisites) on Kubernetes requires the [OpenTelemetry Operator](https://opentelemetry.io/docs/platforms/kubernetes/operator/#getting-started) on the cluster.

#### REST API configuration

The full OTLP request body for the REST API is documented in the YugabyteDB Anywhere OpenAPI specification under the `TelemetryProvider` model and the `OTLPConfig` schema (`POST /api/v1/customers/{cUUID}/telemetry_provider` with `config.type` = `"OTLP"`). Refer to the [YugabyteDB Anywhere REST API](../../anywhere-automation/anywhere-api/).

Be careful when using `collection_level=ALL` in the API, as it can lead to performance overhead on the database nodes.

The following `OTLPConfig` fields are accepted by the REST API but are not exposed in the UI:

| Field | Description |
| :---- | :---------- |
| `headers` | Map of extra HTTP headers attached to every export request. |
| `compression` | Exporter compression (`gzip`, `none`, `snappy`, `zstd`). UI create requests always send `gzip`. |
| `retryOnFailure` | Retry settings: `{ "initial_interval": "...", "max_interval": "...", "max_elapsed_time": "..." }` with duration strings such as `30s`, `1m`, `60m`. |

#### Additional configuration

| Runtime flag | Scope | Description |
| :----------- | :---- | :---------- |
| `yb.telemetry.skip_connectivity_validations` | Global | Skips connectivity and permission validations on create if your receiver is not reachable from YugabyteDB Anywhere at configuration time. |
| `yb.universe.otel_collector_max_memory` | Universe | Hard memory limit on the OpenTelemetry Collector process (kills the process if exceeded). To apply a change on an existing universe, re-run any OpenTelemetry configure API (configure metrics export, modify audit logging, or modify query logging). |

## Next steps

- [Export metrics from a universe](../anywhere-metrics-export/)
- [Export logs from a universe](../universe-logging/)
