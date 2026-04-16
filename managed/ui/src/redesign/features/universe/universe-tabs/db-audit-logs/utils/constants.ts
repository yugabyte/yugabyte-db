import { YSQLAuditStatementClass, YSQLAuditLogLevel } from './types';

export const MODIFY_AUDITLOG_TASK_TYPE = 'ModifyAuditLoggingConfig';

export const YSQL_AUDIT_CLASSES = [
  {
    title: 'Read',
    value: YSQLAuditStatementClass.READ,
    tooltip: 'Log SELECT and COPY when the source is a relation or a query.'
  },
  {
    title: 'Write',
    value: YSQLAuditStatementClass.WRITE,
    tooltip: 'Log INSERT, UPDATE, DELETE, TRUNCATE, and COPY when the destination is a relation.'
  },
  {
    title: 'Function',
    value: YSQLAuditStatementClass.FUNCTION,
    tooltip: 'Log function calls and DO blocks.'
  },
  {
    title: 'Role',
    value: YSQLAuditStatementClass.ROLE,
    tooltip:
      'Log statements related to roles and privileges: GRANT, REVOKE, CREATE/ALTER/DROP ROLE.'
  },
  {
    title: 'DDL',
    value: YSQLAuditStatementClass.DDL,
    tooltip: 'Log all DDL that is not included in the ROLE class.'
  },
  {
    title: 'Misc',
    value: YSQLAuditStatementClass.MISC,
    tooltip: 'Log miscellaneous commands, such as DISCARD, FETCH, CHECKPOINT, VACUUM, SET.'
  }
];

export const YSQL_LOG_LEVEL_OPTIONS = [
  {
    label: 'Debug1',
    value: YSQLAuditLogLevel.DEBUG1
  },
  {
    label: 'Debug2',
    value: YSQLAuditLogLevel.DEBUG2
  },
  {
    label: 'Debug3',
    value: YSQLAuditLogLevel.DEBUG3
  },
  {
    label: 'Debug4',
    value: YSQLAuditLogLevel.DEBUG4
  },
  {
    label: 'Debug5',
    value: YSQLAuditLogLevel.DEBUG5
  },
  {
    label: 'Log',
    value: YSQLAuditLogLevel.LOG
  },
  {
    label: 'Info',
    value: YSQLAuditLogLevel.INFO
  },
  {
    label: 'Notice',
    value: YSQLAuditLogLevel.NOTICE
  },
  {
    label: 'Warning',
    value: YSQLAuditLogLevel.WARNING
  }
];
