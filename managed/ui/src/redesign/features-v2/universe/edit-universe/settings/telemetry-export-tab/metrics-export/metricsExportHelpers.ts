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

import { getPreservedTelemetrySections } from '../../shared/telemetryConfigPreserveUtils';
import { K8S_SUPPORTED_SCRAPE_CONFIG_TARGETS } from '../k8sTelemetrySupport';

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

export const getScrapeConfigTargetOptions = (
  isKubernetes: boolean
): ScrapeConfigTargetOption[] => {
  const targets = isKubernetes
    ? K8S_SUPPORTED_SCRAPE_CONFIG_TARGETS
    : ALL_SCRAPE_CONFIG_TARGETS;
  return targets.map((target) => ({
    value: target,
    label: target
  }));
};

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
  metrics?: MetricsTelemetrySpec | null,
  options?: { isKubernetes?: boolean }
): MetricsExportFormValues => {
  const existingExporter = metrics?.exporters?.[0];
  const defaultTargets = options?.isKubernetes
    ? [...K8S_SUPPORTED_SCRAPE_CONFIG_TARGETS]
    : [...ALL_SCRAPE_CONFIG_TARGETS];
  const existingTargets = metrics?.scrape_config_targets ?? [];
  const filteredExistingTargets = options?.isKubernetes
    ? existingTargets.filter((target) =>
        K8S_SUPPORTED_SCRAPE_CONFIG_TARGETS.includes(target)
      )
    : existingTargets;

  return {
    telemetryConfigUuid: existingExporter?.exporter_uuid ?? '',
    scrapeIntervalSeconds: metrics?.scrape_interval_seconds ?? DEFAULT_SCRAPE_INTERVAL_SECONDS,
    scrapeTimeoutSeconds: metrics?.scrape_timeout_seconds ?? DEFAULT_SCRAPE_TIMEOUT_SECONDS,
    collectionLevel: metrics?.collection_level ?? MetricsExportConfigBaseCollectionLevel.NORMAL,
    scrapeConfigTargets:
      filteredExistingTargets.length > 0 ? filteredExistingTargets : defaultTargets
  };
};

const buildMetricsExporter = (
  telemetryConfigUuid: string,
  existingExporter?: UniverseMetricsExporterConfig
): UniverseMetricsExporterConfig => ({
  ...existingExporter,
  exporter_uuid: telemetryConfigUuid
});

export const buildTelemetryConfig = (
  values: MetricsExportFormValues,
  currentTelemetryConfig?: TelemetryConfig
): TelemetryConfig => {
  const existingExporter = currentTelemetryConfig?.metrics?.exporters?.[0];
  const preserved = getPreservedTelemetrySections(currentTelemetryConfig);

  return {
    ...preserved,
    metrics: {
      scrape_interval_seconds: values.scrapeIntervalSeconds,
      scrape_timeout_seconds: values.scrapeTimeoutSeconds,
      collection_level: values.collectionLevel,
      scrape_config_targets: values.scrapeConfigTargets,
      exporters: [buildMetricsExporter(values.telemetryConfigUuid, existingExporter)]
    }
  };
};

export const buildDisableTelemetryConfig = (
  currentTelemetryConfig?: TelemetryConfig
): TelemetryConfig => {
  const preserved = getPreservedTelemetrySections(currentTelemetryConfig);
  delete preserved.metrics;
  return preserved;
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
        if (value == null || interval === undefined) {
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
