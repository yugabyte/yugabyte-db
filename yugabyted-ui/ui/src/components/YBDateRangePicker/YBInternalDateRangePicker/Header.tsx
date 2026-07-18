import { Grid, makeStyles, IconButton, Select, MenuItem } from '@material-ui/core';
import React, { ChangeEvent } from 'react';
import ChevronLeft from '@material-ui/icons/ChevronLeft';
import ChevronRight from '@material-ui/icons/ChevronRight';
import { setMonth, getMonth, setYear, getYear } from 'date-fns';
import { useTranslation } from 'react-i18next';

const useStyles = makeStyles((theme) => ({
  iconContainer: {
    padding: theme.spacing(0.6)
  },
  icon: {
    padding: theme.spacing(1.25),
    '&:hover': {
      background: 'none'
    }
  }
}));

interface HeaderProps {
  date: Date;
  setDate: (date: Date) => void;
  nextDisabled: boolean;
  prevDisabled: boolean;
  onClickNext: () => void;
  onClickPrevious: () => void;
}

const monthsKey = ['jan', 'feb', 'mar', 'apr', 'may', 'june', 'july', 'aug', 'sept', 'oct', 'nov', 'dec'];

const generateYears = (relativeTo: Date, count: number) => {
  const half = Math.floor(count / 2);
  return Array(count)
    .fill(0)
    .map((_y, i) => relativeTo.getFullYear() - half + i); // TODO: make part of the state
};

export const Header: React.FunctionComponent<HeaderProps> = ({
  date,
  setDate,
  nextDisabled,
  prevDisabled,
  onClickNext,
  onClickPrevious
}: HeaderProps) => {
  const classes = useStyles();
  const { t } = useTranslation();

  const handleMonthChange = (event: ChangeEvent<{ name?: string; value: unknown }>) => {
    setDate(setMonth(date, parseInt(event.target.value as string)));
  };

  const handleYearChange = (event: ChangeEvent<{ name?: string; value: unknown }>) => {
    setDate(setYear(date, parseInt(event.target.value as string)));
  };

  return (
    <Grid container justifyContent="space-between" alignItems="center">
      <Grid item className={classes.iconContainer}>
        <IconButton className={classes.icon} disabled={prevDisabled} onClick={onClickPrevious}>
          <ChevronLeft color={prevDisabled ? 'disabled' : 'action'} />
        </IconButton>
      </Grid>
      <Grid item>
        <Select value={getMonth(date)} onChange={handleMonthChange} MenuProps={{ disablePortal: true }}>
          {monthsKey.map((month, idx) => (
            <MenuItem key={month} value={idx}>
              {t((`common.dateRangePicker.months.${month}` as unknown) as TemplateStringsArray)}
            </MenuItem>
          ))}
        </Select>
      </Grid>

      <Grid item>
        <Select value={getYear(date)} onChange={handleYearChange} MenuProps={{ disablePortal: true }}>
          {generateYears(date, 30).map((year) => (
            <MenuItem key={year} value={year}>
              {year}
            </MenuItem>
          ))}
        </Select>

        {/* <Typography>{format(date, "MMMM YYYY")}</Typography> */}
      </Grid>
      <Grid item className={classes.iconContainer}>
        <IconButton className={classes.icon} disabled={nextDisabled} onClick={onClickNext}>
          <ChevronRight color={nextDisabled ? 'disabled' : 'action'} />
        </IconButton>
      </Grid>
    </Grid>
  );
};
