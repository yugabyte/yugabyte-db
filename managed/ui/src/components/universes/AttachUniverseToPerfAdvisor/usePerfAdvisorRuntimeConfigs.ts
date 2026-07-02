import type { PaFeatureRuntimeConfigs } from '@yugabytedb/perf-advisor-ui';

/**
 * Builds the `PaFeatureRuntimeConfigs` object passed to the embedded Perf
 * Advisor entries (`PerfAdvisorEntry` and `MetricsAnalysisEntry`).
 *
 * Values are hardcoded for now until the backend `yb.ui.*` capability keys are
 * registered.
 */
export const usePerfAdvisorRuntimeConfigs = (): PaFeatureRuntimeConfigs => ({
  showDbLoadView: true,
  showDbCpuView: false,
  showBgTasksView: true,
  showSystemQueriesView: false,
  showTimezoneSelector: false,
  showAnomalyHighlighting: false,
  showExplainPlan: true,
  showPgssRpcStats: true,
  showQueriesExport: true
});
