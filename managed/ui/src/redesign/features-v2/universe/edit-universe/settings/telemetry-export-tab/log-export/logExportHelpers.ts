import * as yup from 'yup';
import { TFunction } from 'i18next';
import { TP_FRIENDLY_NAMES } from '@app/redesign/features/export-telemetry/constants';
import { TelemetryProvider } from '@app/redesign/features/export-telemetry/dtos';
import {
  AuditLogsTelemetrySpec,
  QueryLogsTelemetrySpec,
  TelemetryConfig,
  UniverseLogsExporterConfig,
  UniverseQueryLogsExporterConfig,
  YSQLAuditConfig,
  YSQLAuditConfigLogLevel
} from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';

export type LogExportType = 'query' | 'audit';

export type LogExportOperation = 'create' | 'edit';

export interface LogExportFormValues {
  telemetryConfigUuid: string;
}

const DEFAULT_AUDIT_LOG_LEVEL = YSQLAuditConfigLogLevel.LOG;
const DEFAULT_LOG_PARAMETER_MAX_SIZE = 0;

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

const normalizeYsqlAuditConfig = (ysqlAuditConfig: YSQLAuditConfig): YSQLAuditConfig => ({
  ...ysqlAuditConfig,
  enabled: ysqlAuditConfig.enabled ?? true,
  log_level: ysqlAuditConfig.log_level ?? DEFAULT_AUDIT_LOG_LEVEL,
  log_parameter_max_size:
    ysqlAuditConfig.log_parameter_max_size ?? DEFAULT_LOG_PARAMETER_MAX_SIZE
});

const preserveAuditLogs = (auditLogs: AuditLogsTelemetrySpec): AuditLogsTelemetrySpec => ({
  ...auditLogs,
  ...(auditLogs.ysql_audit_config && {
    ysql_audit_config: normalizeYsqlAuditConfig(auditLogs.ysql_audit_config)
  })
});

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

/**
 * The export-telemetry-configs API fully replaces the telemetry config, so we preserve the
 * existing log capture settings and other telemetry sections and only update exporters for the
 * selected log export type.
 */
export const buildTelemetryConfig = (
  logExportType: LogExportType,
  values: LogExportFormValues,
  currentTelemetryConfig?: TelemetryConfig
): TelemetryConfig => {
  const telemetryConfig: TelemetryConfig = {};

  if (logExportType === 'query') {
    const existingQueryLogs = currentTelemetryConfig?.query_logs;
    const queryLogs: QueryLogsTelemetrySpec = {
      exporters: [
        buildQueryLogsExporter(values.telemetryConfigUuid, existingQueryLogs?.exporters?.[0])
      ]
    };

    if (existingQueryLogs?.ysql_query_log_config) {
      queryLogs.ysql_query_log_config = existingQueryLogs.ysql_query_log_config;
    }

    telemetryConfig.query_logs = queryLogs;

    if (currentTelemetryConfig?.audit_logs) {
      telemetryConfig.audit_logs = preserveAuditLogs(currentTelemetryConfig.audit_logs);
    }
  } else {
    const existingAuditLogs = currentTelemetryConfig?.audit_logs;
    const auditLogs: AuditLogsTelemetrySpec = {
      exporters: [
        buildAuditLogsExporter(values.telemetryConfigUuid, existingAuditLogs?.exporters?.[0])
      ]
    };

    if (existingAuditLogs?.ysql_audit_config) {
      auditLogs.ysql_audit_config = normalizeYsqlAuditConfig(existingAuditLogs.ysql_audit_config);
    }
    if (existingAuditLogs?.ycql_audit_config) {
      auditLogs.ycql_audit_config = existingAuditLogs.ycql_audit_config;
    }

    telemetryConfig.audit_logs = auditLogs;

    if (currentTelemetryConfig?.query_logs) {
      telemetryConfig.query_logs = currentTelemetryConfig.query_logs;
    }
  }

  if (currentTelemetryConfig?.metrics) {
    telemetryConfig.metrics = currentTelemetryConfig.metrics;
  }

  return telemetryConfig;
};

export const getValidationSchema = (t: TFunction) =>
  yup.object({
    telemetryConfigUuid: yup.string().required(t('errors.exportConfigurationRequired'))
  });
