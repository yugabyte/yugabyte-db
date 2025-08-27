import { Universe } from '@app/redesign/helpers/dtos';
import { getPrimaryCluster } from '@app/utils/universeUtilsTyped';
import { TelemetryProviderType } from '../export-log/utils/types';
import { TelemetryProvider } from './dtos';

/**
 * Although we support more than one universe metrics exporter config from the backend,
 * the designed UI for YBA supports only a single metrics exporter config per universe.
 */
export const getUniverseMetricsExportConfig = (universe: Universe) =>
  getPrimaryCluster(universe.universeDetails.clusters)?.userIntent?.metricsExportConfig
    ?.universeMetricsExporterConfig[0];

export const getIsMetricsExportSupported = (telemetryProvider: TelemetryProvider) =>
  telemetryProvider.config.type === TelemetryProviderType.DATA_DOG;
