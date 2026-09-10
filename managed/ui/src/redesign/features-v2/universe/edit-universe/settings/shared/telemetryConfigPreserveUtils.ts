import { TelemetryConfig } from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';

/**
 * Carry sibling sections through a full-replace export-telemetry POST.
 * Pass sections through as-is (do not normalize) so backend diff does not treat
 * unchanged siblings as modified.
 */
export const getPreservedTelemetrySections = (
  currentTelemetryConfig?: TelemetryConfig
): TelemetryConfig => {
  const preserved: TelemetryConfig = {};

  if (currentTelemetryConfig?.audit_logs) {
    preserved.audit_logs = currentTelemetryConfig.audit_logs;
  }
  if (currentTelemetryConfig?.query_logs) {
    preserved.query_logs = currentTelemetryConfig.query_logs;
  }
  if (currentTelemetryConfig?.metrics) {
    preserved.metrics = currentTelemetryConfig.metrics;
  }
  if (currentTelemetryConfig?.master_logs) {
    preserved.master_logs = currentTelemetryConfig.master_logs;
  }

  return preserved;
};
