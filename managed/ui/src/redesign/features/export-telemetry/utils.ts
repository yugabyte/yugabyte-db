import { Cluster, Universe } from '@app/redesign/helpers/dtos';
import { getPrimaryCluster } from '@app/utils/universeUtilsTyped';
import { TelemetryProviderItem, TelemetryProviderType } from './types';
import { TelemetryProvider } from './dtos';
import { assertUnreachableCase } from '@app/utils/errorHandlingUtils';
/**
 * Although we support more than one universe log exporter config from the backend,
 * the designed UI for YBA supports only a single log exporter config per universe.
 */
export const getClusterAuditLogConfig = (cluster: Cluster) =>
  cluster.userIntent.auditLogConfig?.universeLogsExporterConfig?.[0];

/**
 * Although we support more than one universe query log exporter config from the backend,
 * the designed UI for YBA supports only a single query log exporter config per universe.
 */
export const getClusterQueryLogConfig = (cluster: Cluster) =>
  cluster.userIntent.queryLogConfig?.universeLogsExporterConfig?.[0];

/**
 * Although we support more than one universe metrics exporter config from the backend,
 * the designed UI for YBA supports only a single metrics exporter config per universe.
 */
export const getClusterMetricsExportConfig = (cluster: Cluster) =>
  cluster.userIntent?.metricsExportConfig?.universeMetricsExporterConfig?.[0];

export const getUniverseMetricsExportConfig = (universe: Universe) => {
  const primaryCluster = getPrimaryCluster(universe.universeDetails.clusters);
  return primaryCluster ? getClusterMetricsExportConfig(primaryCluster) : undefined;
};

export const getIsMetricsExportSupported = (telemetryProvider: TelemetryProvider) => {
  switch (telemetryProvider.config.type) {
    case TelemetryProviderType.DATA_DOG:
    case TelemetryProviderType.DYNATRACE:
    case TelemetryProviderType.OTLP:
      return true;
    case TelemetryProviderType.AWS_CLOUDWATCH:
    case TelemetryProviderType.GCP_CLOUD_MONITORING:
    case TelemetryProviderType.LOKI:
    case TelemetryProviderType.SPLUNK:
    case TelemetryProviderType.S3:
      return false;
    default:
      return assertUnreachableCase(telemetryProvider.config.type);
  }
};

export const getIsLogsExportSupported = (telemetryProvider: TelemetryProvider) => {
  switch (telemetryProvider.config.type) {
    case TelemetryProviderType.DATA_DOG:
    case TelemetryProviderType.AWS_CLOUDWATCH:
    case TelemetryProviderType.GCP_CLOUD_MONITORING:
    case TelemetryProviderType.LOKI:
    case TelemetryProviderType.SPLUNK:
    case TelemetryProviderType.S3:
    case TelemetryProviderType.OTLP:
      return true;
    case TelemetryProviderType.DYNATRACE:
      return false;
    default:
      return assertUnreachableCase(telemetryProvider.config.type);
  }
};

export const getIsTelemetryProviderConfigInUse = (telemetryProviderItem: TelemetryProviderItem) => {
  return (
    telemetryProviderItem.linkedUniverses.universesWithLogExporter.length > 0 ||
    telemetryProviderItem.linkedUniverses.universesWithMetricsExporter.length > 0
  );
};

export const getLinkedUniverses = (
  exporterUuid: string,
  universes: Universe[]
): { universesWithLogExporter: Universe[]; universesWithMetricsExporter: Universe[] } =>
  universes.reduce(
    (
      linkedUniverses: {
        universesWithLogExporter: Universe[];
        universesWithMetricsExporter: Universe[];
      },
      universe
    ) => {
      const primaryCluster = getPrimaryCluster(universe.universeDetails.clusters);

      if (
        primaryCluster &&
        (getClusterAuditLogConfig(primaryCluster)?.exporterUuid === exporterUuid ||
          getClusterQueryLogConfig(primaryCluster)?.exporterUuid === exporterUuid)
      ) {
        linkedUniverses.universesWithLogExporter.push(universe);
      }

      if (
        primaryCluster &&
        getClusterMetricsExportConfig(primaryCluster)?.exporterUuid === exporterUuid
      ) {
        linkedUniverses.universesWithMetricsExporter.push(universe);
      }
      return linkedUniverses;
    },
    { universesWithLogExporter: [], universesWithMetricsExporter: [] }
  );
