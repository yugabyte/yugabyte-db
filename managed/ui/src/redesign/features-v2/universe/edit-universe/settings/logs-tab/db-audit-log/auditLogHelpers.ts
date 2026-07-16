import {
  TelemetryConfig,
  YSQLAuditConfig,
  YSQLAuditConfigClassesItem,
  YSQLAuditConfigLogLevel
} from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';

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

/**
 * The export-telemetry-configs API fully replaces the telemetry config, so we preserve the
 * existing query_logs and metrics sections and only modify audit_logs. Existing audit-log
 * exporters and YCQL config are preserved as-is (export selection is managed via Telemetry Export).
 *
 * The GET returns audit_logs/metrics as null when those exports are disabled, but the API
 * rejects null sections, so we only carry them over when they are actually configured.
 */
export const buildTelemetryConfig = (
  values: AuditLogFormValues,
  currentTelemetryConfig?: TelemetryConfig
): TelemetryConfig => {
  const telemetryConfig: TelemetryConfig = {
    audit_logs: {
      ysql_audit_config: buildYsqlAuditConfig(
        values,
        currentTelemetryConfig?.audit_logs?.ysql_audit_config
      ),
      ...(currentTelemetryConfig?.audit_logs?.ycql_audit_config && {
        ycql_audit_config: currentTelemetryConfig.audit_logs.ycql_audit_config
      }),
      exporters: currentTelemetryConfig?.audit_logs?.exporters ?? []
    }
  };

  if (currentTelemetryConfig?.query_logs) {
    telemetryConfig.query_logs = currentTelemetryConfig.query_logs;
  }
  if (currentTelemetryConfig?.metrics) {
    telemetryConfig.metrics = currentTelemetryConfig.metrics;
  }

  return telemetryConfig;
};
