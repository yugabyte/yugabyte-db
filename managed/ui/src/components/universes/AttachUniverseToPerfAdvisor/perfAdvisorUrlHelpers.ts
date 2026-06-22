import { URL_TAB_PATH, isNonEmptyString } from '@yugabytedb/perf-advisor-ui';
import { PERF_ADVISOR_PATH } from '@app/redesign/helpers/constants';

/**
 * Shared helpers for reading Perf Advisor drilldown state from the browser URL.
 */

const anomalyUuidPattern = new RegExp(`/${URL_TAB_PATH.ANOMALIES}/([^/]+)`);
const perfAdvisorDrilldownPattern = new RegExp(`/${PERF_ADVISOR_PATH}/([^/?#]+)`);

const uuidPattern = /^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/i;

/** queryId from the search string — set when a query drilldown is opened. */
export const getQueryIdFromUrl = (): string | null =>
  new URLSearchParams(window.location.search).get('queryId');

/** Anomaly uuid from the drilldown URL handles YBA URL shapes. */
export const getTroubleshootUuidFromUrl = (): string | null => {
  const path = window.location.pathname;
  const candidate =
    path.match(anomalyUuidPattern)?.[1] ?? path.match(perfAdvisorDrilldownPattern)?.[1] ?? null;
  return candidate && uuidPattern.test(candidate) ? candidate : null;
};

/** True when the current URL is a query or anomaly drilldown view. */
export const isPaDrilldownUrl = (): boolean =>
  isNonEmptyString(getQueryIdFromUrl()!) || isNonEmptyString(getTroubleshootUuidFromUrl()!);
