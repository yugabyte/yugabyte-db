/*
 * Created on Jan 16 2024
 *
 * Copyright 2024 YugaByte, Inc. and Contributors
 */

import { PerfAdvisorAnalysisDashboard } from '../redesign/features/PerfAdvisor/PerfAdvisorAnalysisDashboard';

export const PerfAdvisorAnalysisView = (props: any) => {
  const universeUuid = props.params?.uuid;
  const troubleshootUuid = props.location?.query?.anomalyUuid;

  return (
    <PerfAdvisorAnalysisDashboard universeUuid={universeUuid} troubleshootUuid={troubleshootUuid} />
  );
};
