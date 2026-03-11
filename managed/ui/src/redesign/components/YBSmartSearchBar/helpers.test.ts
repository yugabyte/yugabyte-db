import { FieldType, isMatchedBySearchToken, SearchCandidate } from './helpers';
import { SearchToken } from './YBSmartSearchBar';

const makeToken = (value: string, modifier: string | null = null): SearchToken => ({
  id: 'test-id',
  modifier,
  value
});

describe('isMatchedBySearchToken', () => {
  it('matches when there is no modifier and a substring field contains the search value', () => {
    const candidate: SearchCandidate = {
      name: { value: 'my-universe-1', type: FieldType.STRING },
      status: { value: 'Running', type: FieldType.STRING }
    };
    expect(isMatchedBySearchToken(candidate, makeToken('universe'), ['name', 'status'])).toBe(true);
    expect(isMatchedBySearchToken(candidate, makeToken('running'), ['name', 'status'])).toBe(true);
    expect(isMatchedBySearchToken(candidate, makeToken('my-'), ['name', 'status'])).toBe(true);
  });

  it('does not match when there is no modifier and no substring field contains the search value', () => {
    const candidate: SearchCandidate = {
      name: { value: 'my-universe-1', type: FieldType.STRING },
      status: { value: 'Running', type: FieldType.STRING }
    };
    expect(isMatchedBySearchToken(candidate, makeToken('other'), ['name', 'status'])).toBe(false);
    expect(isMatchedBySearchToken(candidate, makeToken('xyz'), ['name', 'status'])).toBe(false);
  });

  it('does not match when there is no modifier and the matching field is not a string type', () => {
    const candidate: SearchCandidate = {
      name: { value: 'my-universe-1', type: FieldType.STRING },
      replicationLagMs: { value: 1000, type: FieldType.NUMBER }
    };
    expect(isMatchedBySearchToken(candidate, makeToken('1000'), ['name'])).toBe(false);
  });

  it('matches on modifier string field when value contains the search token', () => {
    const candidate: SearchCandidate = {
      name: { value: 'prod-universe-us-west', type: FieldType.STRING },
      database: { value: 'prod-database', type: FieldType.STRING },
      replicationLagMs: { value: 1000, type: FieldType.NUMBER }
    };
    expect(isMatchedBySearchToken(candidate, makeToken('prod', 'name'), [])).toBe(true);
    expect(isMatchedBySearchToken(candidate, makeToken('us-west', 'name'), [])).toBe(true);
  });

  it('does not match on modifier string field when value does not contain the search token', () => {
    const candidate: SearchCandidate = {
      name: { value: 'prod-universe', type: FieldType.STRING }
    };
    expect(isMatchedBySearchToken(candidate, makeToken('staging', 'name'), [])).toBe(false);
  });

  it('matches on modifier boolean field for true/false search values', () => {
    const candidateTrue: SearchCandidate = {
      healthy: { value: true, type: FieldType.BOOLEAN }
    };
    const candidateFalse: SearchCandidate = {
      healthy: { value: false, type: FieldType.BOOLEAN }
    };
    expect(isMatchedBySearchToken(candidateTrue, makeToken('true', 'healthy'), [])).toBe(true);
    expect(isMatchedBySearchToken(candidateFalse, makeToken('false', 'healthy'), [])).toBe(true);
  });

  it('does not match on modifier boolean field when search value does not match field value', () => {
    const candidateTrue: SearchCandidate = {
      healthy: { value: true, type: FieldType.BOOLEAN }
    };
    const candidateFalse: SearchCandidate = {
      healthy: { value: false, type: FieldType.BOOLEAN }
    };
    expect(isMatchedBySearchToken(candidateTrue, makeToken('false', 'healthy'), [])).toBe(false);
    expect(isMatchedBySearchToken(candidateFalse, makeToken('true', 'healthy'), [])).toBe(false);
  });

  it('matches on modifier number field with comparison operators', () => {
    // Candidate shape matches xCluster replication table search (ReplicationTables.tsx, ConfigTableSelect.tsx)
    const candidate: SearchCandidate = {
      table: { value: 'users', type: FieldType.STRING },
      database: { value: 'yugabyte', type: FieldType.STRING },
      replicationLagMs: { value: 1500, type: FieldType.NUMBER }
    };
    expect(isMatchedBySearchToken(candidate, makeToken('>1000', 'replicationLagMs'), [])).toBe(
      true
    );
    expect(isMatchedBySearchToken(candidate, makeToken('>=1500', 'replicationLagMs'), [])).toBe(
      true
    );
    expect(isMatchedBySearchToken(candidate, makeToken('<2000', 'replicationLagMs'), [])).toBe(
      true
    );
    expect(isMatchedBySearchToken(candidate, makeToken('<=1500', 'replicationLagMs'), [])).toBe(
      true
    );
    expect(isMatchedBySearchToken(candidate, makeToken('=1500', 'replicationLagMs'), [])).toBe(
      true
    );
    expect(isMatchedBySearchToken(candidate, makeToken('!=5000', 'replicationLagMs'), [])).toBe(
      true
    );
  });

  it('does not match on modifier number field when comparison fails', () => {
    const candidate: SearchCandidate = {
      replicationLagMs: { value: 1500, type: FieldType.NUMBER }
    };
    expect(isMatchedBySearchToken(candidate, makeToken('>5000', 'replicationLagMs'), [])).toBe(
      false
    );
    expect(isMatchedBySearchToken(candidate, makeToken('<=1000', 'replicationLagMs'), [])).toBe(
      false
    );
    expect(isMatchedBySearchToken(candidate, makeToken('=5000', 'replicationLagMs'), [])).toBe(
      false
    );
    expect(isMatchedBySearchToken(candidate, makeToken('!=1500', 'replicationLagMs'), [])).toBe(
      false
    );
  });

  it('does not match on modifier number field when search value does not match comparison regex', () => {
    const candidate: SearchCandidate = {
      sizeBytes: { value: 1024, type: FieldType.NUMBER }
    };
    expect(isMatchedBySearchToken(candidate, makeToken('1024', 'sizeBytes'), [])).toBe(false);
    expect(isMatchedBySearchToken(candidate, makeToken('abc', 'sizeBytes'), [])).toBe(false);
  });

  describe('when candidate has undefined or null field values', () => {
    it('does not throw and returns false when substring field has null value (unqualified token)', () => {
      const candidate: SearchCandidate = {
        name: { value: 'my-universe', type: FieldType.STRING },
        schema: { value: (null as unknown) as string, type: FieldType.STRING }
      };
      expect(() =>
        isMatchedBySearchToken(candidate, makeToken('any'), ['name', 'schema'])
      ).not.toThrow();
      expect(isMatchedBySearchToken(candidate, makeToken('any'), ['name', 'schema'])).toBe(false);
    });

    it('does not throw and returns false when substring field has undefined value (unqualified token)', () => {
      const candidate: SearchCandidate = {
        name: { value: 'my-universe', type: FieldType.STRING },
        database: { value: (undefined as unknown) as string, type: FieldType.STRING }
      };
      expect(() =>
        isMatchedBySearchToken(candidate, makeToken('any'), ['name', 'database'])
      ).not.toThrow();
      expect(isMatchedBySearchToken(candidate, makeToken('any'), ['name', 'database'])).toBe(false);
    });

    it('does not throw when candidate is missing a substring field (unqualified token)', () => {
      const candidate: SearchCandidate = {
        name: { value: 'my-universe', type: FieldType.STRING }
      };
      expect(() =>
        isMatchedBySearchToken(candidate, makeToken('any'), ['name', 'database', 'schema'])
      ).not.toThrow();
      expect(
        isMatchedBySearchToken(candidate, makeToken('universe'), ['name', 'database', 'schema'])
      ).toBe(true);
    });

    it('returns false when modifier field is missing from candidate', () => {
      const candidate: SearchCandidate = {
        name: { value: 'my-universe', type: FieldType.STRING }
      };
      expect(isMatchedBySearchToken(candidate, makeToken('any', 'database'), [])).toBe(false);
    });

    it('returns false when a modifier field is missing for any search value', () => {
      const candidate: SearchCandidate = {
        table: { value: 'users', type: FieldType.STRING },
        database: { value: 'yugabyte', type: FieldType.STRING }
      };
      expect(isMatchedBySearchToken(candidate, makeToken('>0', 'replicationLagMs'), [])).toBe(
        false
      );
      expect(isMatchedBySearchToken(candidate, makeToken('=-1', 'replicationLagMs'), [])).toBe(
        false
      );
    });

    it('returns false when modifier string field has null value', () => {
      const candidate: SearchCandidate = {
        schema: { value: (null as unknown) as string, type: FieldType.STRING }
      };
      expect(isMatchedBySearchToken(candidate, makeToken('public', 'schema'), [])).toBe(false);
    });
  });
});
