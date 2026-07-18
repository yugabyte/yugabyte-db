import { setupServer } from 'msw/node';
import { handlers } from './handlers';

/**
 * MSW server for Node (Vitest). Use in vitest.setup.ts with listen/resetHandlers/close.
 */
export const server = setupServer(...handlers);
