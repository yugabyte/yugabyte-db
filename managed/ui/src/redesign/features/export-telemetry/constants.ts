import { TelemetryProviderType } from './types';

// TODO: move this to en.json
export const TP_FRIENDLY_NAMES = {
  [TelemetryProviderType.DATA_DOG]: 'Datadog',
  [TelemetryProviderType.SPLUNK]: 'Splunk',
  [TelemetryProviderType.AWS_CLOUDWATCH]: 'AWS CloudWatch',
  [TelemetryProviderType.GCP_CLOUD_MONITORING]: 'GCP Cloud Logging',
  [TelemetryProviderType.LOKI]: 'Loki',
  [TelemetryProviderType.DYNATRACE]: 'Dynatrace',
  [TelemetryProviderType.S3]: 'S3',
  [TelemetryProviderType.OTLP]: 'OTLP'
};

export const DATADOG_SITES = [
  { name: 'US1', value: 'datadoghq.com' },
  { name: 'US3', value: 'us3.datadoghq.com' },
  { name: 'US5', value: 'us5.datadoghq.com' },
  { name: 'EU1', value: 'datadoghq.eu' },
  { name: 'US1-FED', value: 'ddog-gov.com' },
  { name: 'AP1', value: 'ap1.datadoghq.com' }
];

export const LOKI_AUTH_TYPES = [
  {
    label: 'Basic Auth',
    value: 'BasicAuth'
  },
  {
    label: 'No Auth',
    value: 'NoAuth'
  }
];

export const TelemetryType = {
  LOGS: 'logs',
  METRICS: 'metrics'
};
export type TelemetryType = typeof TelemetryType[keyof typeof TelemetryType];
