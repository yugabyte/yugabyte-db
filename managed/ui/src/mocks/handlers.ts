import { http, HttpResponse } from 'msw';

/**
 * Base URL for API requests in development and in tests.
 */
const API_BASE = 'http://localhost:9000';

/**
 * Shared MSW request handlers.
 */
export const handlers = [
  http.get(`${API_BASE}/api/v2/yba-info`, () => {
    return HttpResponse.json({
      fips_enabled: false
    });
  })
];
