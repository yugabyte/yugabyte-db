import { TelemetryProviderType } from './types';

export const TP_FRIENDLY_NAMES = {
  [TelemetryProviderType.DATA_DOG]: 'Datadog',
  [TelemetryProviderType.SPLUNK]: 'Splunk',
  [TelemetryProviderType.AWS_CLOUDWATCH]: 'AWS CloudWatch',
  [TelemetryProviderType.GCP_CLOUD_MONITORING]: 'GCP Cloud Logging'
};

export const TELEMETRY_PROVIDER_OPTIONS = [
  {
    label: 'Datadog',
    value: TelemetryProviderType.DATA_DOG
  },
  {
    label: 'Splunk',
    value: TelemetryProviderType.SPLUNK
  },
  {
    label: 'AWS CloudWatch',
    value: TelemetryProviderType.AWS_CLOUDWATCH
  },
  {
    label: 'GCP Cloud Logging',
    value: TelemetryProviderType.GCP_CLOUD_MONITORING
  }
];

export const DATADOG_SITES = [
  { name: 'US1', value: 'datadoghq.com' },
  { name: 'US3', value: 'us3.datadoghq.com' },
  { name: 'US5', value: 'us5.datadoghq.com' },
  { name: 'EU1', value: 'datadoghq.eu' },
  { name: 'US1-FED', value: 'ddog-gov.com' },
  { name: 'AP1', value: 'ap1.datadoghq.com' }
];
