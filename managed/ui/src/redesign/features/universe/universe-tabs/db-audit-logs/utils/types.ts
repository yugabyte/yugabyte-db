export enum YSQLAuditStatementClass {
  READ = 'READ',
  WRITE = 'WRITE',
  FUNCTION = 'FUNCTION',
  ROLE = 'ROLE',
  DDL = 'DDL',
  MISC = 'MISC',
  MISC_SET = 'MISC_SET'
}

export enum YSQLAuditLogLevel {
  DEBUG1 = 'DEBUG1',
  DEBUG2 = 'DEBUG2',
  DEBUG3 = 'DEBUG3',
  DEBUG4 = 'DEBUG4',
  DEBUG5 = 'DEBUG5',
  INFO = 'INFO',
  NOTICE = 'NOTICE',
  WARNING = 'WARNING',
  LOG = 'LOG'
}

export interface AuditLogFormFields {
  classes: YSQLAuditStatementClass[];
  logCatalog: boolean;
  logClient: boolean;
  logParameter: boolean;
  logRelation: boolean;
  logStatement: boolean;
  logStatementOnce: boolean;
  logLevel?: YSQLAuditLogLevel;
  exportActive: boolean;
  exporterUuid?: string;
}

export interface ExporterObject {
  exporterUuid: string;
}

export interface AuditLogConfig {
  exportActive: boolean;
  ysqlAuditConfig: {
    enabled: boolean;
    logCatalog: boolean;
    logClient: boolean;
    logParameter: boolean;
    logRelation: boolean;
    logStatement: boolean;
    logStatementOnce: boolean;
    logLevel?: YSQLAuditLogLevel;
    classes: YSQLAuditStatementClass[];
  };
  universeLogsExporterConfig?: ExporterObject[];
}

export interface AuditLogPayload {
  installOtelCollector: boolean;
  auditLogConfig: AuditLogConfig;
}
