import React from 'react';
import { useTranslation } from 'react-i18next';
import { addMonths, isSameDay, isAfter, isBefore, isSameMonth, addYears, max, min } from 'date-fns';
import type {
  DateRange,
  NavigationAction,
  DefinedRange
} from '@app/components/YBDateRangePicker/YBInternalDateRangePickerUtill/types';
import {
  getValidatedMonths,
  inDateRange,
  parseOptionalDate
} from '@app/components/YBDateRangePicker/YBInternalDateRangePickerUtill/utils';
import { getDefaultRanges } from '@app/components/YBDateRangePicker/YBInternalDateRangePickerUtill/defaults';
import { Menu } from './Menu';

type Marker = symbol;

export const MARKERS: { [key: string]: Marker } = {
  FIRST_MONTH: Symbol('firstMonth'),
  SECOND_MONTH: Symbol('secondMonth')
};

interface DateRangePickerProps {
  open: boolean;
  format?: string;
  definedRanges?: DefinedRange[];
  showDefinedRange?: boolean;
  sameDateSelectable?: boolean;
  minDate?: Date | string;
  maxDate?: Date | string;
  onChange: (dateRange: DateRange) => void;
  onChangePartial?: (dateRange: DateRange) => void;
  dateRange: DateRange;
  setDateRange: (dateRange: DateRange) => void;
}

export const DateRangePicker: React.FunctionComponent<DateRangePickerProps> = (props: DateRangePickerProps) => {
  const today = new Date();
  const { t } = useTranslation();
  const {
    open,
    onChange,
    onChangePartial,
    dateRange,
    setDateRange,
    minDate,
    maxDate,
    showDefinedRange,
    sameDateSelectable,
    format,
    definedRanges = getDefaultRanges(new Date(), t)
  } = props;

  const minDateValid = parseOptionalDate(minDate, addYears(today, -10));
  const maxDateValid = parseOptionalDate(maxDate, addYears(today, 10));
  const [initialFirstMonth, initialSecondMonth] = getValidatedMonths(dateRange ?? {}, minDateValid, maxDateValid);

  const [hoverDay, setHoverDay] = React.useState<Date>();
  const [firstMonth, setFirstMonth] = React.useState<Date>(initialFirstMonth ?? today);
  const [secondMonth, setSecondMonth] = React.useState<Date>(initialSecondMonth ?? addMonths(firstMonth, 1));

  const { startDate, endDate } = dateRange ?? {};

  // handlers
  const setFirstMonthValidated = (date: Date) => {
    if (isBefore(date, secondMonth)) {
      setFirstMonth(date);
    }
  };

  const setSecondMonthValidated = (date: Date) => {
    if (isAfter(date, firstMonth)) {
      setSecondMonth(date);
    }
  };

  const setDateRangeValidated = (range: DateRange) => {
    let { startDate: newStart, endDate: newEnd } = range;

    if (newStart && newEnd) {
      range.startDate = newStart = max([newStart, minDateValid]);
      range.endDate = newEnd = min([newEnd, maxDateValid]);

      setDateRange(range);
      onChange(range);

      setFirstMonth(newStart);
      setSecondMonth(isSameMonth(newStart, newEnd) ? addMonths(newStart, 1) : newEnd);
    } else {
      const emptyRange = {};

      setDateRange(emptyRange);
      onChange(emptyRange);

      setFirstMonth(today);
      setSecondMonth(addMonths(firstMonth, 1));
    }
    if (onChangePartial) {
      if (newStart || newEnd) {
        onChangePartial(range);
      } else {
        onChangePartial({});
      }
    }
  };
  const onDayClick = (day: Date) => {
    const dontCare = () => {
      const newRange = { startDate: day, endDate: undefined };
      setDateRange(newRange);
      if (onChangePartial) {
        onChangePartial(newRange);
      }
    };
    if (!sameDateSelectable && startDate && !endDate && isSameDay(day, startDate)) {
      dontCare();
    } else if (startDate && !endDate && !isBefore(day, startDate)) {
      const newRange = { startDate, endDate: day };
      onChange(newRange);
      setDateRange(newRange);
      if (onChangePartial) {
        onChangePartial(newRange);
      }
    } else {
      dontCare();
    }

    setHoverDay(day);
  };

  const onMonthNavigate = (marker: Marker, action: NavigationAction) => {
    if (marker === MARKERS.FIRST_MONTH) {
      const firstNew = addMonths(firstMonth, action);
      if (isBefore(firstNew, secondMonth)) setFirstMonth(firstNew);
    } else {
      const secondNew = addMonths(secondMonth, action);
      if (isBefore(firstMonth, secondNew)) setSecondMonth(secondNew);
    }
  };

  const onDayHover = (date: Date) => {
    if (startDate && !endDate) {
      if (!hoverDay || !isSameDay(date, hoverDay)) {
        setHoverDay(date);
      }
    }
  };

  // helpers
  const inHoverRange = (day: Date) => {
    return !!(
      startDate &&
      !endDate &&
      hoverDay &&
      isAfter(hoverDay, startDate) &&
      inDateRange({ startDate, endDate: hoverDay }, day)!
    );
  };

  const helpers = {
    inHoverRange
  };

  const handlers = {
    onDayClick,
    onDayHover,
    onMonthNavigate
  };

  return open ? (
    <Menu
      format={format}
      showDefinedRange={showDefinedRange}
      dateRange={dateRange}
      minDate={minDateValid}
      maxDate={maxDateValid}
      ranges={definedRanges}
      firstMonth={firstMonth}
      secondMonth={secondMonth}
      setFirstMonth={setFirstMonthValidated}
      setSecondMonth={setSecondMonthValidated}
      setDateRange={setDateRangeValidated}
      helpers={helpers}
      handlers={handlers}
    />
  ) : null;
};
