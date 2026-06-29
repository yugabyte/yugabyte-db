import { describe, expect, it } from 'vitest';

import { formatDuration } from './Formatters';

describe('formatDuration', () => {
  it('formats zero durations in compact and full-label modes', () => {
    expect(formatDuration(0)).toBe('0ms');
    expect(formatDuration(0, true)).toBe('0 milliseconds');
  });

  it('formats multi-unit durations in compact mode', () => {
    const durationInMilliseconds =
      (((1 * 24 + 2) * 60 + 3) * 60 + 4) * 1000 + 5;

    expect(formatDuration(durationInMilliseconds)).toBe('1d 2h 3m 4s 5ms');
  });

  it('formats multi-unit durations in full-label mode', () => {
    const durationInMilliseconds =
      (((((400 * 24 + 2) * 60 + 3) * 60 + 4) * 1000) + 5);

    expect(formatDuration(durationInMilliseconds, true)).toBe(
      '400 days 2 hours 3 minutes 4 seconds 5 milliseconds'
    );
  });

  it('preserves the sign for negative durations', () => {
    const durationInMilliseconds = (65 * 1000) + 1;

    expect(formatDuration(-durationInMilliseconds)).toBe('-1m 5s 1ms');
    expect(formatDuration(-durationInMilliseconds, true)).toBe(
      '-1 minute 5 seconds 1 millisecond'
    );
  });

  it('returns an empty string for undefined values', () => {
    expect(formatDuration(undefined as unknown as number)).toBe('');
  });

  it('returns non-finite values as strings', () => {
    expect(formatDuration(Infinity)).toBe('Infinity');
    expect(formatDuration(-Infinity)).toBe('-Infinity');
  });
});
