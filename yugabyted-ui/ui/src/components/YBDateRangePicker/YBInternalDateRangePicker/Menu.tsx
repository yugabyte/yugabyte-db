import React from 'react';
import { Paper, Grid, Typography, Divider, makeStyles, Theme } from '@material-ui/core';
import { format as formatDateFnS, differenceInCalendarMonths } from 'date-fns';
import ArrowRightAlt from '@material-ui/icons/ArrowRightAlt';
import { Month } from './Month';
import { DefinedRanges } from './DefinedRanges';
import type {
  DateRange,
  DefinedRange,
  Setter,
  NavigationAction
} from '@app/components/YBDateRangePicker/YBInternalDateRangePickerUtill/types';
import { MARKERS } from './DateRangePicker';

const useStyles = makeStyles((theme: Theme) => ({
  header: {
    padding: `${theme.spacing(2.5)}px ${theme.spacing(11.2)}px`,
    borderTopLeftRadius: theme.shape.borderRadius,
    borderTopRightRadius: theme.shape.borderRadius
  },
  headerItem: {
    flex: 1,
    textAlign: 'center'
  },
  divider: {
    borderLeft: `1px solid ${theme.palette.action.hover}`,
    marginBottom: theme.spacing(2.5)
  },
  startMonth: {
    borderBottomLeftRadius: theme.shape.borderRadius
  },
  endMonth: {
    borderBottomRightRadius: theme.shape.borderRadius
  }
}));

interface MenuProps {
  showDefinedRange?: boolean;
  format?: string;
  dateRange: DateRange;
  ranges: DefinedRange[];
  minDate: Date;
  maxDate: Date;
  firstMonth: Date;
  secondMonth: Date;
  setFirstMonth: Setter<Date>;
  setSecondMonth: Setter<Date>;
  setDateRange: Setter<DateRange>;
  helpers: {
    inHoverRange: (day: Date) => boolean;
  };
  handlers: {
    onDayClick: (day: Date) => void;
    onDayHover: (day: Date) => void;
    onMonthNavigate: (marker: symbol, action: NavigationAction) => void;
  };
}

export const Menu: React.FunctionComponent<MenuProps> = (props: MenuProps) => {
  const classes = useStyles();

  const {
    ranges,
    dateRange,
    minDate,
    maxDate,
    firstMonth,
    setFirstMonth,
    secondMonth,
    setSecondMonth,
    setDateRange,
    helpers,
    handlers,
    format,
    showDefinedRange
  } = props;

  const { startDate, endDate } = dateRange;
  const canNavigateCloser = differenceInCalendarMonths(secondMonth, firstMonth) >= 2;
  const commonProps = {
    dateRange,
    minDate,
    maxDate,
    helpers,
    handlers
  };
  return (
    <Paper elevation={5}>
      <Grid container direction="row" wrap="nowrap">
        <Grid>
          <Grid container className={classes.header} alignItems="center">
            <Grid item className={classes.headerItem}>
              <Typography variant="subtitle1">
                {startDate ? formatDateFnS(startDate, format ?? 'MMM dd, yyyy') : 'Start Date'}
              </Typography>
            </Grid>
            <Grid item className={classes.headerItem}>
              <ArrowRightAlt color="action" />
            </Grid>
            <Grid item className={classes.headerItem}>
              <Typography variant="subtitle1">
                {endDate ? formatDateFnS(endDate, format ?? 'MMM dd, yyyy') : 'End Date'}
              </Typography>
            </Grid>
          </Grid>
          <Divider />
          <Grid container direction="row" justifyContent="center" wrap="nowrap">
            <Month
              {...commonProps}
              className={classes.startMonth}
              value={firstMonth}
              format={format}
              setValue={setFirstMonth}
              navState={[true, canNavigateCloser]}
              marker={MARKERS.FIRST_MONTH}
            />
            <div className={classes.divider} />
            <Month
              {...commonProps}
              className={classes.endMonth}
              value={secondMonth}
              setValue={setSecondMonth}
              navState={[canNavigateCloser, true]}
              marker={MARKERS.SECOND_MONTH}
            />
          </Grid>
        </Grid>
        {showDefinedRange && (
          <Grid>
            <DefinedRanges selectedRange={dateRange} ranges={ranges} setRange={setDateRange} />
          </Grid>
        )}
      </Grid>
    </Paper>
  );
};
