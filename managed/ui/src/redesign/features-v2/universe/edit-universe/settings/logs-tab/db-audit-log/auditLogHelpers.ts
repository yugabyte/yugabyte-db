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

export interface AuditLogFormValues {
  classes: YSQLAuditConfigClassesItem[];
  logCatalog: boolean;
  logClient: boolean;
  logLevel: YSQLAuditConfigLogLevel;
  logParameter: boolean;
  logRelation: boolean;
  logStatement: boolean;
  logStatementOnce: boolean;
}

const createDefaultFormValues = (): AuditLogFormValues => ({
  classes: [YSQLAuditConfigClassesItem.ROLE, YSQLAuditConfigClassesItem.DDL],
  logCatalog: true,
  logClient: false,
  logLevel: YSQLAuditConfigLogLevel.LOG,
  logParameter: false,
  logRelation: false,
  logStatement: true,
  logStatementOnce: false
});

export const getDefaultFormValues = (ysqlAuditConfig?: YSQLAuditConfig): AuditLogFormValues => {
  if (!ysqlAuditConfig) {
    return createDefaultFormValues();
  }

  return {
    classes: ysqlAuditConfig.classes ?? [],
    logCatalog: ysqlAuditConfig.log_catalog,
    logClient: ysqlAuditConfig.log_client,
    logLevel: ysqlAuditConfig.log_level ?? YSQLAuditConfigLogLevel.LOG,
    logParameter: ysqlAuditConfig.log_parameter,
    logRelation: ysqlAuditConfig.log_relation,
    logStatement: ysqlAuditConfig.log_statement,
    logStatementOnce: ysqlAuditConfig.log_statement_once
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
    ...(currentYsqlConfig?.log_retention_days !== undefined && {
      log_retention_days: currentYsqlConfig.log_retention_days
    })
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
