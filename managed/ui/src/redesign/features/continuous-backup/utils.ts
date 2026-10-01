import moment from 'moment';

import { ContinuousBackup, TimeUnitType } from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';
import { CustomerConfig, CustomerConfigType, StorageConfig } from '../../../components/backupv2';
import { BackupFrequencyFormUnit } from './constants';

// Classify the customer config so typescript can infer the correct type.
export const isStorageConfig = (
  customerConfig: CustomerConfig
): customerConfig is StorageConfig => {
  return customerConfig.type === CustomerConfigType.STORAGE;
};

// This type assertion can be removed after updating to Typescript 5.5 as the
// type checker will be able to infer the correct type post version 5.5:
// https://devblogs.microsoft.com/typescript/announcing-typescript-5-5/#inferred-type-predicates
export const getStorageConfigs = (customerConfigs: CustomerConfig[]): StorageConfig[] =>
  customerConfigs.filter((customerConfig) => isStorageConfig(customerConfig)) as StorageConfig[];

const RECENT_BACKUP_THRESHOLD_HOURS = 24;

export const getIsLastPlatformBackupOld = (continuousBackupConfig: ContinuousBackup) => {
  const currentTime = moment();
  const lastBackupTime = continuousBackupConfig.info?.last_backup;

  return (
    !!lastBackupTime && currentTime.diff(lastBackupTime, 'hours') > RECENT_BACKUP_THRESHOLD_HOURS
  );
};

const NANOSECONDS_IN_MICROSECOND = 1000;
const MICROSECONDS_IN_MILLISECOND = 1000;
const MILLISECONDS_IN_SECOND = 1000;
const SECONDS_IN_MINUTE = 60;
const MINUTES_IN_HOUR = 60;
const HOURS_IN_DAY = 24;
const DAYS_IN_MONTH = 30;
const DAYS_IN_YEAR = 365;

export type BackupIntervalDurationI18nKey = 'seconds' | 'minutes' | 'hours' | 'days';

export type BackupIntervalDisplay = {
  backupFrequency: number;
  durationI18nKey: BackupIntervalDurationI18nKey;
};

/**
 * Convert (frequency, unit) to milliseconds using the same scale as
 * ContinuousBackupConfig.getFrequencyInMilliseconds on the backend.
 */
export const getFrequencyInMilliseconds = (
  frequency: number,
  frequencyTimeUnit: TimeUnitType | undefined
): number => {
  switch (frequencyTimeUnit) {
    case TimeUnitType.NANOSECONDS:
      return frequency / (NANOSECONDS_IN_MICROSECOND * MICROSECONDS_IN_MILLISECOND);
    case TimeUnitType.MICROSECONDS:
      return frequency / MICROSECONDS_IN_MILLISECOND;
    case TimeUnitType.MILLISECONDS:
      return frequency;
    case TimeUnitType.SECONDS:
      return frequency * MILLISECONDS_IN_SECOND;
    case TimeUnitType.MINUTES:
      return frequency * MILLISECONDS_IN_SECOND * SECONDS_IN_MINUTE;
    case TimeUnitType.HOURS:
      return frequency * MILLISECONDS_IN_SECOND * SECONDS_IN_MINUTE * MINUTES_IN_HOUR;
    case TimeUnitType.DAYS:
      return (
        frequency * MILLISECONDS_IN_SECOND * SECONDS_IN_MINUTE * MINUTES_IN_HOUR * HOURS_IN_DAY
      );
    case TimeUnitType.MONTHS:
      return (
        frequency *
        MILLISECONDS_IN_SECOND *
        SECONDS_IN_MINUTE *
        MINUTES_IN_HOUR *
        HOURS_IN_DAY *
        DAYS_IN_MONTH
      );
    case TimeUnitType.YEARS:
      return (
        frequency *
        MILLISECONDS_IN_SECOND *
        SECONDS_IN_MINUTE *
        MINUTES_IN_HOUR *
        HOURS_IN_DAY *
        DAYS_IN_YEAR
      );
    default:
      return frequency * MILLISECONDS_IN_SECOND * SECONDS_IN_MINUTE;
  }
};

/**
 * Round frequency up to the nearest whole second (e.g. 5 minutes 1 millisecond → 5
 * minutes 1 second). Zero or negative values are returned unchanged.
 */
export const normalizeFrequencyInMilliseconds = (
  frequency: number,
  frequencyTimeUnit: TimeUnitType | undefined
): number => {
  const frequencyInMilliseconds = getFrequencyInMilliseconds(frequency, frequencyTimeUnit);
  if (frequencyInMilliseconds <= 0) {
    return frequencyInMilliseconds;
  }
  return Math.ceil(frequencyInMilliseconds / MILLISECONDS_IN_SECOND) * MILLISECONDS_IN_SECOND;
};

export type BackupFrequencyFormInterval = {
  frequency: number;
  unit: BackupFrequencyFormUnit;
};

/**
 * Edit form supports seconds and minutes only. Prefer minutes when the interval
 * is an integer number of minutes; otherwise use seconds (rounded up).
 */
export const convertFrequencyToFormInterval = (
  frequency: number,
  frequencyTimeUnit: TimeUnitType | undefined
): BackupFrequencyFormInterval => {
  const frequencyInMilliseconds = normalizeFrequencyInMilliseconds(
    frequency,
    frequencyTimeUnit
  );

  const millisecondsInMinute = MILLISECONDS_IN_SECOND * SECONDS_IN_MINUTE;
  if (frequencyInMilliseconds % millisecondsInMinute === 0) {
    return {
      frequency: frequencyInMilliseconds / millisecondsInMinute,
      unit: 'MINUTES'
    };
  }

  return {
    frequency: Math.ceil(frequencyInMilliseconds / MILLISECONDS_IN_SECOND),
    unit: 'SECONDS'
  };
};

const getLargestDivisibleDisplayInterval = (
  frequencyInMilliseconds: number
): BackupIntervalDisplay => {
  const millisecondsInMinute = MILLISECONDS_IN_SECOND * SECONDS_IN_MINUTE;
  const millisecondsInHour = millisecondsInMinute * MINUTES_IN_HOUR;
  const millisecondsInDay = millisecondsInHour * HOURS_IN_DAY;

  if (frequencyInMilliseconds % millisecondsInDay === 0) {
    return {
      backupFrequency: frequencyInMilliseconds / millisecondsInDay,
      durationI18nKey: 'days'
    };
  }
  if (frequencyInMilliseconds % millisecondsInHour === 0) {
    return {
      backupFrequency: frequencyInMilliseconds / millisecondsInHour,
      durationI18nKey: 'hours'
    };
  }
  if (frequencyInMilliseconds % millisecondsInMinute === 0) {
    return {
      backupFrequency: frequencyInMilliseconds / millisecondsInMinute,
      durationI18nKey: 'minutes'
    };
  }
  return {
    backupFrequency: Math.ceil(frequencyInMilliseconds / MILLISECONDS_IN_SECOND),
    durationI18nKey: 'seconds'
  };
};

/**
 * Format a backup interval from the API for display. Preserves the stored unit
 * when it is seconds or coarser and no sub-second normalization was applied;
 * otherwise picks the largest unit (days → hours → minutes → seconds) that
 * divides evenly (e.g. 2 MONTHS → 60 days).
 */
export const getBackupIntervalDisplay = (
  frequency: number,
  frequencyTimeUnit: TimeUnitType | undefined
): BackupIntervalDisplay => {
  const rawFrequencyInMilliseconds = getFrequencyInMilliseconds(frequency, frequencyTimeUnit);
  const frequencyInMilliseconds = normalizeFrequencyInMilliseconds(
    frequency,
    frequencyTimeUnit
  );

  const preservedDisplayUnits: Array<{
    apiUnit: TimeUnitType;
    durationI18nKey: BackupIntervalDurationI18nKey;
  }> = [
    { apiUnit: TimeUnitType.DAYS, durationI18nKey: 'days' },
    { apiUnit: TimeUnitType.HOURS, durationI18nKey: 'hours' },
    { apiUnit: TimeUnitType.MINUTES, durationI18nKey: 'minutes' },
    { apiUnit: TimeUnitType.SECONDS, durationI18nKey: 'seconds' }
  ];

  if (rawFrequencyInMilliseconds === frequencyInMilliseconds) {
    for (const { apiUnit, durationI18nKey } of preservedDisplayUnits) {
      if (frequencyTimeUnit === apiUnit) {
        return { backupFrequency: frequency, durationI18nKey };
      }
    }
  }

  return getLargestDivisibleDisplayInterval(frequencyInMilliseconds);
};

/** Backend: at least 2 minutes, at most 1 day. */
export const getBackupFrequencyMinValue = (unit: BackupFrequencyFormUnit | undefined): number =>
  unit === TimeUnitType.SECONDS ? 120 : 2;

export const getBackupFrequencyMaxValue = (unit: BackupFrequencyFormUnit | undefined): number =>
  unit === TimeUnitType.SECONDS ? 86400 : 1440;

/**
 * Convert a form interval into another form unit, clamping to the target
 * unit's min/max so switching units does not produce an invalid value.
 */
export const convertBackupFrequencyBetweenFormUnits = (
  frequency: number,
  fromUnit: BackupFrequencyFormUnit,
  toUnit: BackupFrequencyFormUnit
): number => {
  if (fromUnit === toUnit) {
    return frequency;
  }

  const frequencyInMilliseconds = getFrequencyInMilliseconds(frequency, fromUnit);
  const millisecondsInMinute = MILLISECONDS_IN_SECOND * SECONDS_IN_MINUTE;
  const convertedFrequency =
    toUnit === TimeUnitType.SECONDS
      ? frequencyInMilliseconds / MILLISECONDS_IN_SECOND
      : frequencyInMilliseconds / millisecondsInMinute;

  const minValue = getBackupFrequencyMinValue(toUnit);
  const maxValue = getBackupFrequencyMaxValue(toUnit);
  return Math.min(maxValue, Math.max(minValue, Math.round(convertedFrequency)));
};

