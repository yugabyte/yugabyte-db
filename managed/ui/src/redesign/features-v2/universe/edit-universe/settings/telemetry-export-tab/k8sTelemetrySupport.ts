import { compareYBSoftwareVersionsWithReleaseTrack } from '@app/utils/universeUtilsTyped';
import { ScrapeConfigTargetType } from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';

/** Matches KubernetesUtil.MIN_VERSION_OTEL_SUPPORT_* — audit log export on K8s. */
export const K8S_AUDIT_LOG_EXPORT_STABLE_VERSION = '2025.1.0.0-b0';
export const K8S_AUDIT_LOG_EXPORT_PREVIEW_VERSION = '2.25.1.0-b133';

/**
 * Matches OtelCollectorUtil.OTEL_HELM_CONFIG_PASSTHROUGH_* —
 * query / metrics / master / tserver export on K8s (spec.config passthrough chart).
 */
export const K8S_PASSTHROUGH_TELEMETRY_EXPORT_STABLE_VERSION = '2026.1.2.0';
export const K8S_PASSTHROUGH_TELEMETRY_EXPORT_PREVIEW_VERSION = '2.31.0.0';

/** Pod-local scrape targets only; NODE_EXPORT / NODE_AGENT_EXPORT are rejected by the API on K8s. */
export const K8S_SUPPORTED_SCRAPE_CONFIG_TARGETS: ScrapeConfigTargetType[] = [
  ScrapeConfigTargetType.MASTER_EXPORT,
  ScrapeConfigTargetType.TSERVER_EXPORT,
  ScrapeConfigTargetType.YSQL_EXPORT,
  ScrapeConfigTargetType.CQL_EXPORT,
  ScrapeConfigTargetType.OTEL_EXPORT
];

const isAtOrAboveVersionGate = (
  dbVersion: string | undefined | null,
  stableVersion: string,
  previewVersion: string
): boolean => {
  if (!dbVersion) {
    return false;
  }
  return (
    compareYBSoftwareVersionsWithReleaseTrack({
      version: dbVersion,
      stableVersion,
      previewVersion,
      options: { suppressFormatError: true }
    }) >= 0
  );
};

export const isK8sAuditLogExportSupported = (dbVersion: string | undefined | null): boolean =>
  isAtOrAboveVersionGate(
    dbVersion,
    K8S_AUDIT_LOG_EXPORT_STABLE_VERSION,
    K8S_AUDIT_LOG_EXPORT_PREVIEW_VERSION
  );

/** K8s query / metrics export (requires OTel Helm config passthrough chart). */
export const isK8sPassthroughTelemetryExportSupported = (
  dbVersion: string | undefined | null
): boolean =>
  isAtOrAboveVersionGate(
    dbVersion,
    K8S_PASSTHROUGH_TELEMETRY_EXPORT_STABLE_VERSION,
    K8S_PASSTHROUGH_TELEMETRY_EXPORT_PREVIEW_VERSION
  );
