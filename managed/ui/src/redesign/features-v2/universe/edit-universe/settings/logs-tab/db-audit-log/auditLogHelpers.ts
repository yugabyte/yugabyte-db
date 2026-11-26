import {
  TelemetryConfig,
  YSQLAuditConfig,
  YSQLAuditConfigClassesItem,
  YSQLAuditConfigLogLevel
} from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';

import { getPreservedTelemetrySections } from '../../shared/telemetryConfigPreserveUtils';

export const AUDIT_LOG_TRANSLATION_KEY_PREFIX = 'editUniverse.logs.auditLogSettings';

export type AuditLogOperation = 'create' | 'edit';

const DEFAULT_LOG_PARAMETER_MAX_SIZE = 0;
export const DEFAULT_LOG_RETENTION_DAYS = 1;

export interface AuditLogFormValues {
  classes: YSQLAuditConfigClassesItem[];
  logCatalog: boolean;
  logClient: boolean;
  logLevel: YSQLAuditConfigLogLevel;
  logParameter: boolean;
  logRelation: boolean;
  logStatement: boolean;
  logStatementOnce: boolean;
  retainAuditLogArchive: boolean;
  logRetentionDays: number;
}

const createDefaultFormValues = (): AuditLogFormValues => ({
  classes: [YSQLAuditConfigClassesItem.ROLE, YSQLAuditConfigClassesItem.DDL],
  logCatalog: true,
  logClient: false,
  logLevel: YSQLAuditConfigLogLevel.LOG,
  logParameter: false,
  logRelation: false,
  logStatement: true,
  logStatementOnce: false,
  retainAuditLogArchive: false,
  logRetentionDays: DEFAULT_LOG_RETENTION_DAYS
});

export const getDefaultFormValues = (ysqlAuditConfig?: YSQLAuditConfig): AuditLogFormValues => {
  if (!ysqlAuditConfig) {
    return createDefaultFormValues();
  }

  const logRetentionDays = ysqlAuditConfig.log_retention_days;
  const retainAuditLogArchive =
    logRetentionDays !== undefined && logRetentionDays !== null && logRetentionDays > 0;

  return {
    classes: ysqlAuditConfig.classes ?? [],
    logCatalog: ysqlAuditConfig.log_catalog,
    logClient: ysqlAuditConfig.log_client,
    logLevel: ysqlAuditConfig.log_level ?? YSQLAuditConfigLogLevel.LOG,
    logParameter: ysqlAuditConfig.log_parameter,
    logRelation: ysqlAuditConfig.log_relation,
    logStatement: ysqlAuditConfig.log_statement,
    logStatementOnce: ysqlAuditConfig.log_statement_once,
    retainAuditLogArchive,
    logRetentionDays: retainAuditLogArchive ? logRetentionDays : DEFAULT_LOG_RETENTION_DAYS
  };
};

const buildYsqlAuditConfig = (
  values: AuditLogFormValues,
  currentYsqlConfig?: YSQLAuditConfig
): YSQLAuditConfig => {
  const ysqlConfig = {
    enabled: true,
    classes: values.classes,
    log_catalog: values.logCatalog,
    log_client: values.logClient,
    log_level: values.logLevel,
    log_parameter: values.logParameter,
    log_parameter_max_size:
      currentYsqlConfig?.log_parameter_max_size ?? DEFAULT_LOG_PARAMETER_MAX_SIZE,
    log_relation: values.logRelation,
    log_rows: currentYsqlConfig?.log_rows ?? false,
    log_statement: values.logStatement,
    log_statement_once: values.logStatementOnce,
    // 0 disables dedicated audit-log retention (see YSQLAuditConfig.yaml log_retention_days).
    log_retention_days: values.retainAuditLogArchive
      ? Math.max(DEFAULT_LOG_RETENTION_DAYS, Number(values.logRetentionDays))
      : 0
  };

  return ysqlConfig as YSQLAuditConfig;
};

export const buildTelemetryConfig = (
  values: AuditLogFormValues,
  currentTelemetryConfig?: TelemetryConfig
): TelemetryConfig => {
  const preserved = getPreservedTelemetrySections(currentTelemetryConfig);
  const existingAuditLogs = preserved.audit_logs;

  return {
    ...preserved,
    audit_logs: {
      ysql_audit_config: buildYsqlAuditConfig(
        values,
        currentTelemetryConfig?.audit_logs?.ysql_audit_config
      ),
      ...(existingAuditLogs?.ycql_audit_config && {
        ycql_audit_config: existingAuditLogs.ycql_audit_config
      }),
      exporters: currentTelemetryConfig?.audit_logs?.exporters ?? []
    }
  };
};

export const buildDisableTelemetryConfig = (
  currentTelemetryConfig?: TelemetryConfig
): TelemetryConfig => {
  const preserved = getPreservedTelemetrySections(currentTelemetryConfig);
  delete preserved.audit_logs;
  return preserved;
};
