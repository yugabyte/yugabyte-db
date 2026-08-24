import * as yup from 'yup';
import { TFunction } from 'i18next';
import { TP_FRIENDLY_NAMES } from '@app/redesign/features/export-telemetry/constants';
import { TelemetryProvider } from '@app/redesign/features/export-telemetry/dtos';
import {
  AuditLogsTelemetrySpec,
  QueryLogsTelemetrySpec,
  TelemetryConfig,
  UniverseLogsExporterConfig,
  UniverseQueryLogsExporterConfig
} from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';

import { getPreservedTelemetrySections } from '../../shared/telemetryConfigPreserveUtils';

export type LogExportType = 'query' | 'audit';

export type LogExportOperation = 'create' | 'edit';

export interface LogExportFormValues {
  telemetryConfigUuid: string;
}

export const getLogExportTranslationKeyPrefix = (logExportType: LogExportType): string =>
  logExportType === 'query'
    ? 'editUniverse.telemetryExport.queryLogExportSettings'
    : 'editUniverse.telemetryExport.auditLogExportSettings';

export const getLogExportCardTranslationKeyPrefix = (logExportType: LogExportType): string =>
  logExportType === 'query'
    ? 'editUniverse.telemetryExport.queryLogExport'
    : 'editUniverse.telemetryExport.auditLogExport';

export const isLogExportEnabled = (
  logExportType: LogExportType,
  telemetryConfig?: TelemetryConfig
): boolean =>
  logExportType === 'query'
    ? !!telemetryConfig?.query_logs?.exporters?.length
    : !!telemetryConfig?.audit_logs?.exporters?.length;

export const getLogExportDisplayInfo = (
  logExportType: LogExportType,
  telemetryConfig?: TelemetryConfig,
  telemetryProviders?: TelemetryProvider[]
): { exportConfigurationName: string; exportingTo: string } | undefined => {
  const exporterUuid =
    logExportType === 'query'
      ? telemetryConfig?.query_logs?.exporters?.[0]?.exporter_uuid
      : telemetryConfig?.audit_logs?.exporters?.[0]?.exporter_uuid;

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
  logExportType: LogExportType,
  telemetryConfig?: TelemetryConfig
): LogExportFormValues => {
  const existingExporterUuid =
    logExportType === 'query'
      ? telemetryConfig?.query_logs?.exporters?.[0]?.exporter_uuid
      : telemetryConfig?.audit_logs?.exporters?.[0]?.exporter_uuid;

  return {
    telemetryConfigUuid: existingExporterUuid ?? ''
  };
};

const buildQueryLogsExporter = (
  telemetryConfigUuid: string,
  existingExporter?: UniverseQueryLogsExporterConfig
): UniverseQueryLogsExporterConfig => ({
  ...existingExporter,
  exporter_uuid: telemetryConfigUuid
});

const buildAuditLogsExporter = (
  telemetryConfigUuid: string,
  existingExporter?: UniverseLogsExporterConfig
): UniverseLogsExporterConfig => ({
  ...existingExporter,
  exporter_uuid: telemetryConfigUuid
});

export const buildTelemetryConfig = (
  logExportType: LogExportType,
  values: LogExportFormValues,
  currentTelemetryConfig?: TelemetryConfig
): TelemetryConfig => {
  const preserved = getPreservedTelemetrySections(currentTelemetryConfig);

  if (logExportType === 'query') {
    const existingQueryLogs = preserved.query_logs;
    const queryLogs: QueryLogsTelemetrySpec = {
      exporters: [
        buildQueryLogsExporter(values.telemetryConfigUuid, existingQueryLogs?.exporters?.[0])
      ]
    };

    if (existingQueryLogs?.ysql_query_log_config) {
      queryLogs.ysql_query_log_config = existingQueryLogs.ysql_query_log_config;
    }

    return {
      ...preserved,
      query_logs: queryLogs
    };
  }

  const existingAuditLogs = preserved.audit_logs;
  const auditLogs: AuditLogsTelemetrySpec = {
    exporters: [
      buildAuditLogsExporter(values.telemetryConfigUuid, existingAuditLogs?.exporters?.[0])
    ]
  };

  if (existingAuditLogs?.ysql_audit_config) {
    auditLogs.ysql_audit_config = existingAuditLogs.ysql_audit_config;
  }
  if (existingAuditLogs?.ycql_audit_config) {
    auditLogs.ycql_audit_config = existingAuditLogs.ycql_audit_config;
  }

  return {
    ...preserved,
    audit_logs: auditLogs
  };
};

export const buildDisableTelemetryConfig = (
  logExportType: LogExportType,
  currentTelemetryConfig?: TelemetryConfig
): TelemetryConfig => {
  const preserved = getPreservedTelemetrySections(currentTelemetryConfig);

  if (logExportType === 'query') {
    const existingQueryLogs = preserved.query_logs;
    const queryLogs: QueryLogsTelemetrySpec = {
      exporters: []
    };

    if (existingQueryLogs?.ysql_query_log_config) {
      queryLogs.ysql_query_log_config = existingQueryLogs.ysql_query_log_config;
    }

    return {
      ...preserved,
      query_logs: queryLogs
    };
  }

  const existingAuditLogs = preserved.audit_logs;
  const auditLogs: AuditLogsTelemetrySpec = {
    exporters: []
  };

  if (existingAuditLogs?.ysql_audit_config) {
    auditLogs.ysql_audit_config = existingAuditLogs.ysql_audit_config;
  }
  if (existingAuditLogs?.ycql_audit_config) {
    auditLogs.ycql_audit_config = existingAuditLogs.ycql_audit_config;
  }

  return {
    ...preserved,
    audit_logs: auditLogs
  };
};

export const getValidationSchema = (t: TFunction) =>
  yup.object({
    telemetryConfigUuid: yup.string().required(t('errors.exportConfigurationRequired'))
  });
