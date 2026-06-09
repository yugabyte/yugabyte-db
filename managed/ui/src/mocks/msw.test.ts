import { describe, expect, it } from 'vitest';

/**
 * MSW intercepts requests and returns mock responses.
 */
describe('MSW mocking layer', () => {
  it('intercepts GET /api/v2/yba-info and returns YBAInfo mock', async () => {
    const response = await fetch('http://localhost:9000/api/v2/yba-info');
    expect(response.ok).toBe(true);

    const data = await response.json();
    expect(data).toHaveProperty('fips_enabled', false);
    expect(data).toEqual({ fips_enabled: false });
  });
});
