import { Cluster, Universe } from '@app/redesign/helpers/dtos';
import { getPrimaryCluster } from '@app/utils/universeUtilsTyped';
import { TelemetryProviderItem, TelemetryProviderType } from './types';
import { TelemetryProvider } from './dtos';
/**
 * Although we support more than one universe log exporter config from the backend,
 * the designed UI for YBA supports only a single log exporter config per universe.
 */
export const getClusterAuditLogConfig = (cluster: Cluster) =>
  cluster.userIntent.auditLogConfig?.universeLogsExporterConfig?.[0];

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

export const getIsMetricsExportSupported = (telemetryProvider: TelemetryProvider) =>
  telemetryProvider.config.type === TelemetryProviderType.DATA_DOG;

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
        getClusterAuditLogConfig(primaryCluster)?.exporterUuid === exporterUuid
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
