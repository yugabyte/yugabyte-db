import '@testing-library/jest-dom';
import { afterAll, afterEach, beforeAll, vi } from 'vitest';

import { server } from './src/mocks/server';

// @yugabytedb/perf-advisor-ui depends on vis-timeline/standalone which has broken ESM
// (directory imports without file extensions). Mock it globally to avoid the chain.
// Vitest requires every named import used by YBA modules to be present on this mock.
vi.mock('@yugabytedb/perf-advisor-ui', () => ({
  // perfAdvisorHeaders.ts registers a CSRF header hook at module load time.
  setPerfAdvisorCustomHeaders: () => {},
  // Used at module load by perfAdvisorUrlHelpers.ts.
  URL_TAB_PATH: {
    ANOMALIES: 'anomalies',
    QUERIES: 'queries',
    METRICS: 'metricsNew',
    INSIGHTS: 'insights',
    CLUSTER_LOAD: 'clusterLoad',
    CLUSTER_LOAD_DB: 'dbLoad',
    CLUSTER_LOAD_DB_CPU: 'dbCpu',
    OVERALL_LOAD: 'overallLoad'
  },
  isNonEmptyString: (str: string) => typeof str === 'string' && str.length > 0,
  isDefinedNotNull: (obj: unknown) => obj !== undefined && obj !== null,
  PerfAdvisorEntry: () => null,
  MetricsAnalysisEntry: () => null,
  buildQueryDrilldownUrl: () => ''
}));

beforeAll(() => server.listen({ onUnhandledRequest: 'warn' }));
afterEach(() => server.resetHandlers());
afterAll(() => server.close());
