import * as yup from 'yup';
import { TFunction } from 'i18next';
import { TP_FRIENDLY_NAMES } from '@app/redesign/features/export-telemetry/constants';
import { TelemetryProvider } from '@app/redesign/features/export-telemetry/dtos';
import {
  MetricsExportConfigBaseCollectionLevel,
  MetricsTelemetrySpec,
  ScrapeConfigTargetType,
  TelemetryConfig,
  UniverseMetricsExporterConfig
} from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';

export const METRICS_EXPORT_TRANSLATION_KEY_PREFIX =
  'editUniverse.telemetryExport.metricsExportSettings';

export const METRICS_EXPORT_DOCS_URL =
  'https://docs.yugabyte.com/stable/yugabyte-platform/alerts-monitoring/anywhere-metrics-export/';

export const DEFAULT_SCRAPE_INTERVAL_SECONDS = 30;
export const DEFAULT_SCRAPE_TIMEOUT_SECONDS = 20;

export const ALL_SCRAPE_CONFIG_TARGETS = Object.values(ScrapeConfigTargetType);

export type MetricsExportOperation = 'create' | 'edit';

export interface CollectionLevelOption {
  value: MetricsExportConfigBaseCollectionLevel;
  labelKey: string;
}

export const COLLECTION_LEVEL_OPTIONS: CollectionLevelOption[] = [
  { value: MetricsExportConfigBaseCollectionLevel.ALL, labelKey: 'collectionLevelOptions.all' },
  {
    value: MetricsExportConfigBaseCollectionLevel.NORMAL,
    labelKey: 'collectionLevelOptions.normal'
  },
  {
    value: MetricsExportConfigBaseCollectionLevel.TABLE_OFF,
    labelKey: 'collectionLevelOptions.tableOff'
  },
  {
    value: MetricsExportConfigBaseCollectionLevel.MINIMAL,
    labelKey: 'collectionLevelOptions.minimal'
  }
];

export interface ScrapeConfigTargetOption {
  value: ScrapeConfigTargetType;
  label: string;
}

export const SCRAPE_CONFIG_TARGET_OPTIONS: ScrapeConfigTargetOption[] =
  ALL_SCRAPE_CONFIG_TARGETS.map((target) => ({
    value: target,
    label: target
  }));

export interface MetricsExportFormValues {
  telemetryConfigUuid: string;
  scrapeIntervalSeconds: number;
  scrapeTimeoutSeconds: number;
  collectionLevel: MetricsExportConfigBaseCollectionLevel;
  scrapeConfigTargets: ScrapeConfigTargetType[];
}

export const isMetricsExportEnabled = (telemetryConfig?: TelemetryConfig): boolean =>
  !!telemetryConfig?.metrics?.exporters?.length;

export const getMetricsExportDisplayInfo = (
  telemetryConfig?: TelemetryConfig,
  telemetryProviders?: TelemetryProvider[]
): { exportConfigurationName: string; exportingTo: string } | undefined => {
  const exporterUuid = telemetryConfig?.metrics?.exporters?.[0]?.exporter_uuid;
  if (!exporterUuid || !telemetryProviders?.length) {
    return undefined;
  }

  const telemetryProvider = telemetryProviders.find((provider) => provider.uuid === exporterUuid);
  if (!telemetryProvider) {
    return undefined;
  }

  return {
    exportConfigurationName: telemetryProvider.name,
    exportingTo: TP_FRIENDLY_NAMES[telemetryProvider.config.type]
  };
};

export const getDefaultFormValues = (
  metrics?: MetricsTelemetrySpec | null
): MetricsExportFormValues => {
  const existingExporter = metrics?.exporters?.[0];

  return {
    telemetryConfigUuid: existingExporter?.exporter_uuid ?? '',
    scrapeIntervalSeconds: metrics?.scrape_interval_seconds ?? DEFAULT_SCRAPE_INTERVAL_SECONDS,
    scrapeTimeoutSeconds: metrics?.scrape_timeout_seconds ?? DEFAULT_SCRAPE_TIMEOUT_SECONDS,
    collectionLevel: metrics?.collection_level ?? MetricsExportConfigBaseCollectionLevel.NORMAL,
    scrapeConfigTargets:
      metrics?.scrape_config_targets && metrics.scrape_config_targets.length > 0
        ? metrics.scrape_config_targets
        : [...ALL_SCRAPE_CONFIG_TARGETS]
  };
};

const buildMetricsExporter = (
  telemetryConfigUuid: string,
  existingExporter?: UniverseMetricsExporterConfig
): UniverseMetricsExporterConfig => ({
  ...existingExporter,
  exporter_uuid: telemetryConfigUuid
});

/**
 * The export-telemetry-configs API fully replaces the telemetry config, so we preserve the
 * existing audit_logs and query_logs sections and only modify metrics.
 */
export const buildTelemetryConfig = (
  values: MetricsExportFormValues,
  currentTelemetryConfig?: TelemetryConfig
): TelemetryConfig => {
  const existingExporter = currentTelemetryConfig?.metrics?.exporters?.[0];

  const telemetryConfig: TelemetryConfig = {
    metrics: {
      scrape_interval_seconds: values.scrapeIntervalSeconds,
      scrape_timeout_seconds: values.scrapeTimeoutSeconds,
      collection_level: values.collectionLevel,
      scrape_config_targets: values.scrapeConfigTargets,
      exporters: [buildMetricsExporter(values.telemetryConfigUuid, existingExporter)]
    }
  };

  if (currentTelemetryConfig?.audit_logs) {
    telemetryConfig.audit_logs = currentTelemetryConfig.audit_logs;
  }
  if (currentTelemetryConfig?.query_logs) {
    telemetryConfig.query_logs = currentTelemetryConfig.query_logs;
  }

  return telemetryConfig;
};

export const getValidationSchema = (t: TFunction) =>
  yup.object({
    telemetryConfigUuid: yup.string().required(t('errors.exportConfigurationRequired')),
    scrapeIntervalSeconds: yup
      .number()
      .typeError(t('errors.intervalRequired'))
      .required(t('errors.intervalRequired'))
      .integer(t('errors.intervalInteger'))
      .min(1, t('errors.intervalMin')),
    scrapeTimeoutSeconds: yup
      .number()
      .typeError(t('errors.timeoutRequired'))
      .required(t('errors.timeoutRequired'))
      .integer(t('errors.timeoutInteger'))
      .min(1, t('errors.timeoutMin'))
      .test('timeout-less-than-interval', t('errors.timeoutLessThanInterval'), function (value) {
        const interval = this.parent.scrapeIntervalSeconds as number | undefined;
        if (value === undefined || interval === undefined) {
          return true;
        }
        return value < interval;
      }),
    collectionLevel: yup
      .mixed<MetricsExportConfigBaseCollectionLevel>()
      .required(t('errors.collectionLevelRequired')),
    scrapeConfigTargets: yup
      .array()
      .of(yup.mixed<ScrapeConfigTargetType>().required())
      .min(1, t('errors.metricSourcesRequired'))
      .required(t('errors.metricSourcesRequired'))
  });
