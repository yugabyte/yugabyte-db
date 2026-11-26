import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';

import { isTaskCompletedWithinDays, SOFTWARE_UPGRADE_COMPLETED_BANNER_MAX_AGE_DAYS } from './TaskUtils';

describe('isTaskCompletedWithinDays', () => {
  beforeEach(() => {
    vi.useFakeTimers();
    vi.setSystemTime(new Date('2026-10-06T12:00:00Z'));
  });

  afterEach(() => {
    vi.useRealTimers();
  });

  it('returns true when completion is within the window', () => {
    expect(
      isTaskCompletedWithinDays(
        { completionTime: '2026-10-01T12:00:00Z' },
        SOFTWARE_UPGRADE_COMPLETED_BANNER_MAX_AGE_DAYS
      )
    ).toBe(true);
  });

  it('returns false when completion is older than the window', () => {
    expect(
      isTaskCompletedWithinDays(
        { completionTime: '2026-09-28T11:59:59Z' },
        SOFTWARE_UPGRADE_COMPLETED_BANNER_MAX_AGE_DAYS
      )
    ).toBe(false);
  });

  it('returns false when completion time is missing or invalid', () => {
    expect(isTaskCompletedWithinDays({ completionTime: '' }, 7)).toBe(false);
    expect(isTaskCompletedWithinDays({ completionTime: 'not-a-date' }, 7)).toBe(false);
  });
});
