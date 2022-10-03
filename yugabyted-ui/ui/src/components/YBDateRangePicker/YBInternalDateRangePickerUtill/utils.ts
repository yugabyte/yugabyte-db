import {
  startOfWeek,
  startOfMonth,
  endOfWeek,
  endOfMonth,
  isBefore,
  addDays,
  isSameDay,
  isWithinInterval,
  isSameMonth,
  addMonths,
  isValid,
  min,
  max
} from 'date-fns';

import type { DateRange } from './types';

export const identity = <T>(x: T): T => x;

export const chunks = <T>(array: readonly T[], size: number): T[][] => {
  return Array.from({ length: Math.ceil(array.length / size) }, (_v, i) => array.slice(i * size, i * size + size));
};

// Date
export const getDaysInMonth = (date: Date): Date[] => {
  const startWeek = startOfWeek(startOfMonth(date));
  const endWeek = endOfWeek(endOfMonth(date));
  const days = [];
  for (let curr = startWeek; isBefore(curr, endWeek); ) {
    days.push(curr);
    curr = addDays(curr, 1);
  }
  return days;
};

export const isStartOfRange = ({ startDate }: DateRange, day: Date): boolean | undefined => {
  return startDate && isSameDay(day, startDate)!;
};

export const isEndOfRange = ({ endDate }: DateRange, day: Date): boolean | undefined => {
  return endDate && isSameDay(day, endDate)!;
};

export const inDateRange = ({ startDate, endDate }: DateRange, day: Date): boolean | undefined => {
  return (
    startDate &&
    endDate &&
    (isWithinInterval(day, { start: startDate, end: endDate }) || isSameDay(day, startDate) || isSameDay(day, endDate))!
  );
};

export const isRangeSameDay = ({ startDate, endDate }: DateRange): boolean => {
  if (startDate && endDate) {
    return isSameDay(startDate, endDate);
  }
  return false;
};

type Falsy = false | null | undefined | 0 | '';

export const parseOptionalDate = (date: Date | string | Falsy, defaultValue: Date): Date => {
  if (date) {
    const parsed = new Date(Date.parse(date.toString()));
    if (isValid(parsed)) return parsed;
  }
  return defaultValue;
};

export const getValidatedMonths = (range: DateRange, minDate: Date, maxDate: Date): (Date | undefined)[] => {
  const { startDate, endDate } = range;
  if (startDate && endDate) {
    const newStart = max([startDate, minDate]);
    const newEnd = min([endDate, maxDate]);

    return [newStart, isSameMonth(newStart, newEnd) ? addMonths(newStart, 1) : newEnd];
  }
  return [startDate, endDate];
};
