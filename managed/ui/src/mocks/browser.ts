import { setupWorker } from 'msw/browser';
import { handlers } from './handlers';

/**
 * MSW worker for browser (Storybook or dev). Call worker.start() in a decorator or app bootstrap.
 */
export const worker = setupWorker(...handlers);
