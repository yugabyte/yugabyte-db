import * as yup from 'yup';
import { TFunction } from 'i18next';
import {
  TelemetryConfig,
  YSQLQueryLogConfig,
  YSQLQueryLogConfigLogErrorVerbosity,
  YSQLQueryLogConfigLogMinErrorStatement,
  YSQLQueryLogConfigLogStatement
} from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';

import { getPreservedTelemetrySections } from '../../shared/telemetryConfigPreserveUtils';

export const QUERY_LOG_TRANSLATION_KEY_PREFIX = 'editUniverse.logs.queryLogSettings';

export const QUERY_LOG_DOCS_URL =
  'https://www.postgresql.org/docs/current/runtime-config-logging.html#RUNTIME-CONFIG-LOGGING-WHEN';

export const LOG_MIN_DURATION_MIN_VALUE = 0;
// PostgreSQL's log_min_duration_statement is measured in milliseconds and the backend field is
// int32, so the upper bound is the int32 max (-1 is the disabled sentinel, handled separately).
export const LOG_MIN_DURATION_MAX_VALUE = 2_147_483_647;

// When min-duration logging is disabled, the backend expects -1.
const LOG_MIN_DURATION_DISABLED = -1;

export type QueryLogOperation = 'create' | 'edit';

export interface QueryLogFormValues {
  includeLogStatement: boolean;
  logStatement: YSQLQueryLogConfigLogStatement;
  logErrorVerbosity: YSQLQueryLogConfigLogErrorVerbosity;
  logDuration: boolean;
  includeLogMinDuration: boolean;
  logMinDurationStatement: number | null;
  debugPrintPlan: boolean;
  logConnections: boolean;
  logDisconnections: boolean;
}

export const getDefaultFormValues = (
  ysqlQueryLogConfig?: YSQLQueryLogConfig
): QueryLogFormValues => {
  const hasLogStatement =
    !!ysqlQueryLogConfig &&
    ysqlQueryLogConfig.log_statement !== YSQLQueryLogConfigLogStatement.NONE;
  const minDuration = ysqlQueryLogConfig?.log_min_duration_statement ?? LOG_MIN_DURATION_DISABLED;
  const hasMinDuration = minDuration >= 0;

  return {
    includeLogStatement: hasLogStatement,
    logStatement: hasLogStatement
      ? ysqlQueryLogConfig!.log_statement
      : YSQLQueryLogConfigLogStatement.DDL,
    logErrorVerbosity:
      ysqlQueryLogConfig?.log_error_verbosity ?? YSQLQueryLogConfigLogErrorVerbosity.DEFAULT,
    logDuration: ysqlQueryLogConfig?.log_duration ?? false,
    includeLogMinDuration: hasMinDuration,
    logMinDurationStatement: hasMinDuration ? minDuration : null,
    debugPrintPlan: ysqlQueryLogConfig?.debug_print_plan ?? false,
    logConnections: ysqlQueryLogConfig?.log_connections ?? false,
    logDisconnections: ysqlQueryLogConfig?.log_disconnections ?? false
  };
};

const buildEnabledYsqlQueryLogConfig = (
  values: QueryLogFormValues,
  currentYsqlConfig?: YSQLQueryLogConfig
): YSQLQueryLogConfig => ({
  enabled: true,
  log_statement: values.includeLogStatement
    ? values.logStatement
    : YSQLQueryLogConfigLogStatement.NONE,
  log_min_error_statement: YSQLQueryLogConfigLogMinErrorStatement.ERROR,
  log_error_verbosity: values.logErrorVerbosity,
  log_duration: values.logDuration,
  debug_print_plan: values.debugPrintPlan,
  log_connections: values.logConnections,
  log_disconnections: values.logDisconnections,
  log_min_duration_statement:
    values.includeLogMinDuration && values.logMinDurationStatement !== null
      ? values.logMinDurationStatement
      : LOG_MIN_DURATION_DISABLED,
  // `log_line_prefix` is internal and not exposed in the form. Preserve the existing value since
  // the export-telemetry API is full-replace and would otherwise drop it.
  log_line_prefix: currentYsqlConfig?.log_line_prefix
});

export const buildEnabledTelemetryConfig = (
  values: QueryLogFormValues,
  currentTelemetryConfig?: TelemetryConfig
): TelemetryConfig => {
  const preserved = getPreservedTelemetrySections(currentTelemetryConfig);

  return {
    ...preserved,
    query_logs: {
      ysql_query_log_config: buildEnabledYsqlQueryLogConfig(
        values,
        currentTelemetryConfig?.query_logs?.ysql_query_log_config
      ),
      exporters: currentTelemetryConfig?.query_logs?.exporters ?? []
    }
  };
};

export const buildDisableTelemetryConfig = (
  currentTelemetryConfig?: TelemetryConfig
): TelemetryConfig => {
  const preserved = getPreservedTelemetrySections(currentTelemetryConfig);
  delete preserved.query_logs;
  return preserved;
};

export const getValidationSchema = (t: TFunction) =>
  yup.object({
    logMinDurationStatement: yup
      .number()
      .nullable()
      .when('includeLogMinDuration', {
        is: true,
        then: (schema: yup.NumberSchema) =>
          schema
            .typeError(t('errors.minDurationRequired'))
            .required(t('errors.minDurationRequired'))
            .integer(t('errors.minDurationInteger'))
            .min(LOG_MIN_DURATION_MIN_VALUE, t('errors.minDurationRange'))
            .max(LOG_MIN_DURATION_MAX_VALUE, t('errors.minDurationRange'))
      })
  });
