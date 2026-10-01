import { describe, expect, it } from 'vitest';

import { TimeUnitType } from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';

import {
  convertBackupFrequencyBetweenFormUnits,
  convertFrequencyToFormInterval,
  getBackupIntervalDisplay,
  getFrequencyInMilliseconds,
  normalizeFrequencyInMilliseconds
} from './utils';

describe('getFrequencyInMilliseconds', () => {
  it('converts hours to milliseconds using the backend scale', () => {
    expect(getFrequencyInMilliseconds(2, TimeUnitType.HOURS)).toBe(2 * 60 * 60 * 1000);
  });

  it('converts one million nanoseconds to one millisecond', () => {
    expect(getFrequencyInMilliseconds(1_000_000, TimeUnitType.NANOSECONDS)).toBe(1);
  });

  it('converts one thousand microseconds to one millisecond', () => {
    expect(getFrequencyInMilliseconds(1_000, TimeUnitType.MICROSECONDS)).toBe(1);
  });
});

describe('normalizeFrequencyInMilliseconds', () => {
  it('rounds sub-second totals up to 1 second', () => {
    expect(normalizeFrequencyInMilliseconds(500, TimeUnitType.MILLISECONDS)).toBe(1000);
  });

  it('rounds fractional seconds within a longer interval up to the nearest second', () => {
    const fiveMinutesOneMillisecondInMs = 5 * 60 * 1000 + 1;
    expect(normalizeFrequencyInMilliseconds(5 * 60 * 1000 + 1, TimeUnitType.MILLISECONDS)).toBe(
      fiveMinutesOneMillisecondInMs + 999
    );
  });
});

describe('convertFrequencyToFormInterval', () => {
  it('converts 2 hours to 120 minutes', () => {
    expect(convertFrequencyToFormInterval(2, TimeUnitType.HOURS)).toEqual({
      frequency: 120,
      unit: TimeUnitType.MINUTES
    });
  });

  it('keeps 125 seconds as seconds', () => {
    expect(convertFrequencyToFormInterval(125, TimeUnitType.SECONDS)).toEqual({
      frequency: 125,
      unit: TimeUnitType.SECONDS
    });
  });

  it('converts 5 minutes and 1 millisecond to 301 seconds', () => {
    expect(
      convertFrequencyToFormInterval(5 * 60 * 1000 + 1, TimeUnitType.MILLISECONDS)
    ).toEqual({
      frequency: 301,
      unit: TimeUnitType.SECONDS
    });
  });
});

describe('getBackupIntervalDisplay', () => {
  it('preserves stored hours on the card', () => {
    expect(getBackupIntervalDisplay(2, TimeUnitType.HOURS)).toEqual({
      backupFrequency: 2,
      durationI18nKey: 'hours'
    });
  });

  it('shows 2 months as 60 days using the largest divisible unit', () => {
    expect(getBackupIntervalDisplay(2, TimeUnitType.MONTHS)).toEqual({
      backupFrequency: 60,
      durationI18nKey: 'days'
    });
  });

  it('rounds sub-second values up to 1 second for display', () => {
    expect(getBackupIntervalDisplay(500, TimeUnitType.MILLISECONDS)).toEqual({
      backupFrequency: 1,
      durationI18nKey: 'seconds'
    });
  });
});

describe('convertBackupFrequencyBetweenFormUnits', () => {
  it('converts 5 minutes to 300 seconds when switching units', () => {
    expect(convertBackupFrequencyBetweenFormUnits(5, 'MINUTES', 'SECONDS')).toBe(
      300
    );
  });
});

