import '@testing-library/jest-dom';
import { afterAll, afterEach, beforeAll, vi } from 'vitest';

import { server } from './src/mocks/server';

// @yugabytedb/perf-advisor-ui depends on vis-timeline/standalone which has broken ESM
// (directory imports without file extensions). Mock it globally to avoid the chain.
vi.mock('@yugabytedb/perf-advisor-ui', () => ({}));

beforeAll(() => server.listen({ onUnhandledRequest: 'warn' }));
afterEach(() => server.resetHandlers());
afterAll(() => server.close());
