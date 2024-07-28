import { XCLUSTER_UNDEFINED_LAG_NUMERIC_REPRESENTATION } from '../components/xcluster/constants';
import { TableType } from '../redesign/helpers/dtos';
import { isDefinedNotNull } from './ObjectUtils';

/**
 * Format the duration into _d _h _m _s _ms format.
 */
export const formatDuration = (milliseconds: number) => {
  const isNegative = milliseconds < 0;
  const absoluteMilliseconds = Math.abs(milliseconds);

  const MILLISECONDS_IN_SECOND = 1000;
  const SECONDS_IN_MINUTE = 60;
  const MINUTES_IN_HOUR = 60;
  const HOURS_IN_DAY = 24;

  // d - day, h - hour, m - minutes, s - seconds, ms - milliseconds).
  // Units from greatest to least. Base unit should always be last in the array.
  const durationUnits = [
    {
      value: 0,
      unit: 'd',
      baseUnitFactor: MILLISECONDS_IN_SECOND * SECONDS_IN_MINUTE * MINUTES_IN_HOUR * HOURS_IN_DAY
    },
    {
      value: 0,
      unit: 'h',
      baseUnitFactor: MILLISECONDS_IN_SECOND * SECONDS_IN_MINUTE * MINUTES_IN_HOUR
    },
    {
      value: 0,
      unit: 'm',
      baseUnitFactor: MILLISECONDS_IN_SECOND * SECONDS_IN_MINUTE
    },
    {
      value: 0,
      unit: 's',
      baseUnitFactor: MILLISECONDS_IN_SECOND
    },
    {
      value: 0,
      unit: 'ms',
      baseUnitFactor: 1
    }
  ];

  if (!isDefinedNotNull(milliseconds)) return '';

  if (milliseconds && !isFinite(milliseconds)) {
    return milliseconds.toString();
  }

  if (milliseconds === 0) {
    return `0${durationUnits[durationUnits.length - 1].unit}`;
  }

  let allocatedDuration = 0;
  durationUnits.forEach((durationUnit, index) => {
    durationUnit.value =
      index === durationUnits.length - 1
        ? Math.ceil((absoluteMilliseconds - allocatedDuration) / durationUnit.baseUnitFactor)
        : Math.floor((absoluteMilliseconds - allocatedDuration) / durationUnit.baseUnitFactor);
    allocatedDuration += durationUnit.value * durationUnit.baseUnitFactor;
  });

  return `${isNegative ? '-' : ''} ${durationUnits
    .map((durationUnit) =>
      durationUnit.value > 0 ? `${durationUnit.value}${durationUnit.unit}` : ''
    )
    .join(' ')}`;
};

/**
 * Wraps {@link formatDuration} with special formatting for lag metrics
 */
export const formatLagMetric = (milliseconds: number | undefined) => {
  if (
    milliseconds === undefined ||
    milliseconds === XCLUSTER_UNDEFINED_LAG_NUMERIC_REPRESENTATION
  ) {
    return 'Not Reported';
  }
  if (!isFinite(milliseconds)) {
    return 'Unreachable';
  }

  return formatDuration(milliseconds);
};
export const formatSchemaName = (tableType: TableType, schemaName: string) =>
  tableType === TableType.PGSQL_TABLE_TYPE ? schemaName : '-';

/**
 *
 * @param inputNumber number to convert
 * @returns number in text form
 */
export const formatNumberToText = (inputNumber: number) => {
  if (inputNumber < 0) return '';
  const single_digit = ['', 'One', 'Two', 'Three', 'Four', 'Five', 'Six', 'Seven', 'Eight', 'Nine'];
  const double_digit = [
    'Ten',
    'Eleven',
    'Twelve',
    'Thirteen',
    'Fourteen',
    'Fifteen',
    'Sixteen',
    'Seventeen',
    'Eighteen',
    'Nineteen'
  ];
  const below_hundred = [
    'Twenty',
    'Thirty',
    'Forty',
    'Fifty',
    'Sixty',
    'Seventy',
    'Eighty',
    'Ninety'
  ];

  if (inputNumber === 0) return 'Zero';

  function translate(num: number) {
    let word = '';
    if (num < 10) {
      word = single_digit[num] + ' ';
    } else if (num < 20) {
      word = double_digit[num - 10] + ' ';
    } else if (num < 100) {
      const remainder = translate(num % 10);
      word = below_hundred[(num - (num % 10)) / 10 - 2] + ' ' + remainder;
    } else if (num < 1000) {
      word = single_digit[Math.trunc(num / 100)] + ' Hundred ' + translate(num % 100);
    } else if (num < 1000000) {
      word = translate(Math.trunc(num / 1000)).trim() + ' Thousand ' + translate(num % 1000);
    } else if (num < 1000000000) {
      word = translate(Math.trunc(num / 1000000)).trim() + ' Million ' + translate(num % 1000000);
    } else {
      word =
        translate(Math.trunc(num / 1000000000)).trim() + ' Billion ' + translate(num % 1000000000);
    }
    return word;
  }
  const result = translate(inputNumber);
  return result;
};
