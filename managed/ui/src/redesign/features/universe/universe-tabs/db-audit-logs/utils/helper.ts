import {
  YSQLAuditStatementClass,
  AuditLogConfig,
  AuditLogFormFields,
  AuditLogPayload
} from './types';

const formDefaultValues: AuditLogFormFields = {
  classes: [YSQLAuditStatementClass.ROLE, YSQLAuditStatementClass.DDL],
  logCatalog: true,
  logClient: false,
  logParameter: false,
  logRelation: false,
  logStatement: true,
  logStatementOnce: false,
  exportActive: false
};

export const disableLogPayload: AuditLogPayload = {
  installOtelCollector: true,
  auditLogConfig: {
    exportActive: false,
    ysqlAuditConfig: {
      enabled: false,
      classes: [],
      logCatalog: false,
      logClient: false,
      logParameter: false,
      logRelation: false,
      logStatement: false,
      logStatementOnce: false
    }
  }
};

export const constructFormPayload = (auditLogConfig: AuditLogConfig | undefined) => {
  if (auditLogConfig) {
    const { ysqlAuditConfig, universeLogsExporterConfig, exportActive } = auditLogConfig;
    const formPayload: AuditLogFormFields = {
      exportActive,
      classes: ysqlAuditConfig?.classes,
      logCatalog: ysqlAuditConfig?.logCatalog,
      logClient: ysqlAuditConfig?.logClient,
      logParameter: ysqlAuditConfig?.logParameter,
      logRelation: ysqlAuditConfig?.logRelation,
      logStatement: ysqlAuditConfig?.logStatement,
      logStatementOnce: ysqlAuditConfig?.logStatementOnce,
      ...(ysqlAuditConfig?.logClient && { logLevel: ysqlAuditConfig?.logLevel }),
      ...(exportActive &&
        universeLogsExporterConfig?.[0]?.exporterUuid && {
          exporterUuid: universeLogsExporterConfig?.[0]?.exporterUuid
        })
    };
    return formPayload;
  } else {
    return formDefaultValues;
  }
};
