// Copyright (c) YugabyteDB, Inc.

import { compareUniversesForDashboardDisplay } from './universeDisplaySort';

function sortUniverses<T extends { creationDate: string; name: string }>(universes: T[]): T[] {
  return [...universes].sort(compareUniversesForDashboardDisplay);
}

describe('compareUniversesForDashboardDisplay', () => {
  it('sorts by creationDate ascending when both dates are valid', () => {
    const ordered = sortUniverses([
      { creationDate: '2024-06-02T00:00:00.000Z', name: 'gamma' },
      { creationDate: '2024-01-01T00:00:00.000Z', name: 'alpha' },
      { creationDate: '2024-03-15T00:00:00.000Z', name: 'beta' }
    ]);
    expect(ordered.map((u) => u.name)).toEqual(['alpha', 'beta', 'gamma']);
  });

  it('uses name as tie-breaker when creationDate is equal', () => {
    const sameTime = '2024-01-01T12:00:00.000Z';
    const ordered = sortUniverses([
      { creationDate: sameTime, name: 'yugabyte-db-universe' },
      { creationDate: sameTime, name: 'yugabyte-db' },
      { creationDate: sameTime, name: 'yugabyte-platform' }
    ]);
    expect(ordered.map((u) => u.name)).toEqual([
      'yugabyte-db',
      'yugabyte-db-universe',
      'yugabyte-platform'
    ]);
  });

  it('places universes with invalid creationDate after those with valid dates', () => {
    const ordered = sortUniverses([
      { creationDate: '', name: 'first-invalid' },
      { creationDate: '2020-01-01T00:00:00.000Z', name: 'valid' },
      { creationDate: '2019-01-01T00:00:00.000Z', name: 'valid-2' },
      { creationDate: 'not-a-date', name: 'second-invalid' }
    ]);
    expect(ordered.map((u) => u.name)).toEqual([
      'valid-2',
      'valid',
      'first-invalid',
      'second-invalid'
    ]);
  });

  it('sorts by name when both creation dates are invalid', () => {
    const ordered = sortUniverses([
      { creationDate: 'invalid', name: 'zebra' },
      { creationDate: '', name: 'apple' }
    ]);
    expect(ordered.map((u) => u.name)).toEqual(['apple', 'zebra']);
  });
});
