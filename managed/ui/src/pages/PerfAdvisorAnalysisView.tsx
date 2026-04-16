/*
 * Created on Jan 12 2026
 *
 * Copyright 2026 YugabyteDB, Inc. and Contributors
 */

import { URL_TAB_PATH } from '@yugabytedb/perf-advisor-ui';
import { PerfAdvisorAnalysisDashboard } from '../redesign/features/PerfAdvisor/PerfAdvisorAnalysisDashboard';

export const PerfAdvisorAnalysisView = (props: any) => {
  const universeUuid = props.params?.uuid;
  let troubleshootUuid = null;
  if (props.location?.pathname?.includes(`${URL_TAB_PATH.OVERALL_LOAD}/${URL_TAB_PATH.ANOMALIES}`) || props.location?.query?.tab === URL_TAB_PATH.INSIGHTS
  ) {
    troubleshootUuid = props?.params?.id;
  }

  return (
    <PerfAdvisorAnalysisDashboard universeUuid={universeUuid} troubleshootUuid={troubleshootUuid} />
  );
};
