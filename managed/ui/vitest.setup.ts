import '@testing-library/jest-dom';
import { vi } from 'vitest';

// @yugabytedb/perf-advisor-ui depends on vis-timeline/standalone which has broken ESM
// (directory imports without file extensions). Mock it globally to avoid the chain.
vi.mock('@yugabytedb/perf-advisor-ui', () => ({}));
