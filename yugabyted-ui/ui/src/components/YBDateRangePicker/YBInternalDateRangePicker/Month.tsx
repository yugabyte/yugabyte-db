import * as React from 'react';
import { Paper, Grid, Typography, makeStyles, Theme } from '@material-ui/core';
import { getDate, isSameMonth, isToday, format as formatDateFnS } from 'date-fns';
import clsx from 'clsx';
import { useTranslation } from 'react-i18next';
import { Header } from './Header';
import { Day } from './Day';
import { NavigationAction, DateRange } from '@app/components/YBDateRangePicker/YBInternalDateRangePickerUtill/types';
import {
  chunks,
  getDaysInMonth,
  isStartOfRange,
  isEndOfRange,
  inDateRange,
  isRangeSameDay
} from '@app/components/YBDateRangePicker/YBInternalDateRangePickerUtill/utils';

const WEEK_DAYS_KEYS = ['su', 'mo', 'tu', 'we', 'th', 'fr', 'sa'];

const useStyles = makeStyles((theme: Theme) => ({
  root: {
    width: theme.spacing(36.25)
  },
  weekDaysContainer: {
    marginTop: theme.spacing(1.25),
    paddingLeft: theme.spacing(3.75),
    paddingRight: theme.spacing(3.75)
  },
  daysContainer: {
    paddingLeft: theme.spacing(2),
    paddingRight: theme.spacing(2),
    marginTop: theme.spacing(2),
    marginBottom: theme.spacing(2.5)
  }
}));

interface MonthProps {
  value: Date;
  marker: symbol;
  format?: string;
  dateRange: DateRange;
  minDate: Date;
  maxDate: Date;
  navState: [boolean, boolean];
  className?: string;
  setValue: (date: Date) => void;
  helpers: {
    inHoverRange: (day: Date) => boolean;
  };
  handlers: {
    onDayClick: (day: Date) => void;
    onDayHover: (day: Date) => void;
    onMonthNavigate: (marker: symbol, action: NavigationAction) => void;
  };
}

export const Month: React.FunctionComponent<MonthProps> = (props: MonthProps) => {
  const classes = useStyles();
  const { t } = useTranslation();

  const {
    helpers,
    handlers,
    value: date,
    dateRange,
    marker,
    setValue: setDate,
    minDate,
    maxDate,
    format,
    className
  } = props;

  const [back, forward] = props.navState;

  const getIsFilled = (isStart: boolean, isEnd: boolean): boolean => {
    if (isStart || isEnd) {
      return true;
    }
    return false;
  };

  return (
    <Paper square elevation={0} className={clsx(classes.root, className)}>
      <Grid container>
        <Header
          date={date}
          setDate={setDate}
          nextDisabled={!forward}
          prevDisabled={!back}
          onClickPrevious={() => handlers.onMonthNavigate(marker, NavigationAction.Previous)}
          onClickNext={() => handlers.onMonthNavigate(marker, NavigationAction.Next)}
        />

        <Grid item container direction="row" justifyContent="space-between" className={classes.weekDaysContainer}>
          {WEEK_DAYS_KEYS.map((day) => (
            <Typography color="textSecondary" key={day} variant="caption">
              {t((`weekday.twoDigit.${day}` as unknown) as TemplateStringsArray)}
            </Typography>
          ))}
        </Grid>

        <Grid item container direction="column" justifyContent="space-between" className={classes.daysContainer}>
          {chunks(getDaysInMonth(date), 7).map((week, idx) => (
            // eslint-disable-next-line react/no-array-index-key
            <Grid key={idx} container direction="row" justifyContent="center">
              {week.map((day) => {
                const isStart = isStartOfRange(dateRange, day);
                const isEnd = isEndOfRange(dateRange, day);
                const isRangeOneDay = isRangeSameDay(dateRange);
                const highlighted = inDateRange(dateRange, day) ?? helpers.inHoverRange(day);
                return (
                  <Day
                    key={formatDateFnS(day, format ?? 'MMM dd, yyyy')}
                    filled={getIsFilled(isStart ?? false, isEnd ?? false)}
                    outlined={isToday(day)}
                    highlighted={highlighted && !isRangeOneDay}
                    disabled={!isSameMonth(date, day) || !inDateRange({ startDate: minDate, endDate: maxDate }, day)}
                    startOfRange={isStart && !isRangeOneDay}
                    endOfRange={isEnd && !isRangeOneDay}
                    onClick={() => handlers.onDayClick(day)}
                    onHover={() => handlers.onDayHover(day)}
                    value={getDate(day)}
                  />
                );
              })}
            </Grid>
          ))}
        </Grid>
      </Grid>
    </Paper>
  );
};
