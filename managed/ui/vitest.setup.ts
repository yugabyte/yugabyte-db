import '@testing-library/jest-dom';
import { afterAll, afterEach, beforeAll, vi } from 'vitest';

import { server } from './src/mocks/server';

// @yugabytedb/perf-advisor-ui depends on vis-timeline/standalone which has broken ESM
// (directory imports without file extensions). Mock it globally to avoid the chain.
// Stub any named exports that YBA modules invoke at import time (side-effect calls)
// so the mock doesn't blow up before the test body runs. Passive named imports
// (types, constants) can still be undefined and pass through vitest untouched.
vi.mock('@yugabytedb/perf-advisor-ui', () => ({
  // perfAdvisorHeaders.ts registers a CSRF header hook at module load time.
  setPerfAdvisorCustomHeaders: () => {}
}));

beforeAll(() => server.listen({ onUnhandledRequest: 'warn' }));
afterEach(() => server.resetHandlers());
afterAll(() => server.close());
