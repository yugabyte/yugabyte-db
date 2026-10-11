import {
  YSQLAuditConfigClassesItem,
  YSQLAuditConfigLogLevel
} from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';

export const YSQL_AUDIT_CLASSES = [
  {
    title: 'Read',
    value: YSQLAuditConfigClassesItem.READ,
    tooltipKey: 'tooltips.read'
  },
  {
    title: 'Write',
    value: YSQLAuditConfigClassesItem.WRITE,
    tooltipKey: 'tooltips.write'
  },
  {
    title: 'Function',
    value: YSQLAuditConfigClassesItem.FUNCTION,
    tooltipKey: 'tooltips.function'
  },
  {
    title: 'Role',
    value: YSQLAuditConfigClassesItem.ROLE,
    tooltipKey: 'tooltips.role'
  },
  {
    title: 'DDL',
    value: YSQLAuditConfigClassesItem.DDL,
    tooltipKey: 'tooltips.ddl'
  },
  {
    title: 'Misc',
    value: YSQLAuditConfigClassesItem.MISC,
    tooltipKey: 'tooltips.misc'
  },
  {
    title: 'Misc SET',
    value: YSQLAuditConfigClassesItem.MISC_SET,
    tooltipKey: 'tooltips.miscSet'
  }
];

export const YSQL_LOG_LEVEL_OPTIONS = [
  { label: 'Debug1', value: YSQLAuditConfigLogLevel.DEBUG1 },
  { label: 'Debug2', value: YSQLAuditConfigLogLevel.DEBUG2 },
  { label: 'Debug3', value: YSQLAuditConfigLogLevel.DEBUG3 },
  { label: 'Debug4', value: YSQLAuditConfigLogLevel.DEBUG4 },
  { label: 'Debug5', value: YSQLAuditConfigLogLevel.DEBUG5 },
  { label: 'Log', value: YSQLAuditConfigLogLevel.LOG },
  { label: 'Info', value: YSQLAuditConfigLogLevel.INFO },
  { label: 'Notice', value: YSQLAuditConfigLogLevel.NOTICE },
  { label: 'Warning', value: YSQLAuditConfigLogLevel.WARNING }
];

export const PGAUDIT_DOCS_URL = 'https://www.pgaudit.org/';
export const AUDIT_LOG_DOCS_URL =
  'https://docs.yugabyte.com/preview/secure/audit-logging/audit-logging-ysql/';
