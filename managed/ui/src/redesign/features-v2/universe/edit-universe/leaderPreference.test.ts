import { describe, expect, it } from 'vitest';
import { getAzRankGetter, getMapRankGetter } from './leaderPreference';

describe('getAzRankGetter', () => {
  it('shows nothing preferred when every AZ is affinitized and unranked', () => {
    const rankOf = getAzRankGetter([{ leader_affinity: true }, {}]);
    expect([rankOf({ leader_affinity: true }), rankOf({})]).toEqual([0, 0]);
  });

  it('uses ranks only once any AZ has one', () => {
    const azs = [
      { leader_affinity: true, leader_preference: 1 },
      { leader_affinity: true, leader_preference: 0 }
    ];
    const rankOf = getAzRankGetter(azs);
    expect(azs.map(rankOf)).toEqual([1, 0]);
  });
});

describe('getMapRankGetter', () => {
  it('never marks read replica zones preferred', () => {
    const regions = [
      { zones: [{ leader_affinity: false }, { leader_affinity: true }] },
      { clusterType: 'ASYNC', zones: [{ leader_affinity: true, leader_preference: 1 }] }
    ];
    const rankOf = getMapRankGetter(regions);
    expect(rankOf(regions[0], regions[0].zones[1])).toBe(1);
    expect(rankOf(regions[1], regions[1].zones[0])).toBe(0);
  });
});
