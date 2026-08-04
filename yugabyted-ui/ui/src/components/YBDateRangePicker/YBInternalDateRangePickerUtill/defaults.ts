import { addDays, startOfWeek, endOfWeek, addWeeks, startOfMonth, endOfMonth, addMonths } from 'date-fns';
import type { DefinedRange } from './types';
import type { TFunction } from 'i18next';

export const getDefaultRanges = (date: Date, t: TFunction): DefinedRange[] => [
  {
    label: t('common.dateRangePicker.today'),
    startDate: date,
    endDate: date
  },
  {
    label: t('common.dateRangePicker.yesterday'),
    startDate: addDays(date, -1),
    endDate: addDays(date, -1)
  },
  {
    label: t('common.dateRangePicker.thisWeek'),
    startDate: startOfWeek(date),
    endDate: endOfWeek(date)
  },
  {
    label: t('common.dateRangePicker.lastWeek'),
    startDate: startOfWeek(addWeeks(date, -1)),
    endDate: endOfWeek(addWeeks(date, -1))
  },
  {
    label: t('common.dateRangePicker.lastSevenDays'),
    startDate: addWeeks(date, -1),
    endDate: date
  },
  {
    label: t('common.dateRangePicker.thisMonth'),
    startDate: startOfMonth(date),
    endDate: endOfMonth(date)
  },
  {
    label: t('common.dateRangePicker.lastMonth'),
    startDate: startOfMonth(addMonths(date, -1)),
    endDate: endOfMonth(addMonths(date, -1))
  }
];
