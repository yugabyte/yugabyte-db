import type { SupportBundleStatus } from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';

/**
 * Flat shape expected by the existing Support Bundle UI (ThirdStep table).
 * Matches the v1 SupportBundle JSON fields consumed by the modal.
 */
export interface UiSupportBundle {
  bundleUUID: string;
  creationDate?: string | null;
  expirationDate?: string | null;
  status: SupportBundleStatus | string;
  sizeInBytes?: number;
}

/**
 * CamelCase create payload produced by SecondStep `updateOptions`.
 * Only the fields the UI currently emits are listed; extras are ignored by the mapper.
 */
export interface UiSupportBundleCreatePayload {
  startDate?: string;
  endDate?: string;
  components?: string[];
  filterPgAuditLogs?: boolean;
  maxNumRecentCores?: number;
  maxCoreFileSize?: number;
  paDumpStartDate?: string;
  paDumpEndDate?: string;
  paMetricsFormat?: string;
  promDumpStartDate?: string;
  promDumpEndDate?: string;
  promExportType?: string;
  promMetricsFormat?: string;
  promDumpDownSample?: boolean;
  promQueries?: Record<string, string>;
  prometheusMetricsTypes?: string[];
  batchDurationPromDumpMins?: number;
  stepPromDumpSecs?: number | null;
  [key: string]: unknown;
}
