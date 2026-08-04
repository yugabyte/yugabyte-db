import React, { KeyboardEvent } from 'react';
import classNames from 'classnames';
import { makeStyles, Theme } from '@material-ui/core';
import { DateRangePicker } from './DateRangePicker';
import type { DateRange, DefinedRange } from '@app/components/YBDateRangePicker/YBInternalDateRangePickerUtill/types';

const useStyles = makeStyles((theme: Theme) => ({
  dateRangePickerContainer: {
    position: 'relative',
    borderRadius: theme.shape.borderRadius
  },
  dateRangePicker: {
    position: 'relative',
    borderRadius: theme.shape.borderRadius,
    zIndex: 1
  },
  dateRangeBackdrop: {
    position: 'fixed',
    height: '100vh',
    width: '100vw',
    bottom: 0,
    zIndex: 0,
    right: 0,
    left: 0,
    top: 0
  }
}));

export interface DateRangePickerWrapperProps {
  open: boolean;
  format?: string;
  toggle: () => void;
  definedRanges?: DefinedRange[];
  showDefinedRange?: boolean;
  sameDateSelectable?: boolean;
  minDate?: Date | string;
  maxDate?: Date | string;
  onChange: (dateRange: DateRange) => void;
  onChangePartial?: (dateRange: DateRange) => void;
  closeOnClickOutside?: boolean;
  wrapperClassName?: string;
  dateRange: DateRange;
  setDateRange: (dateRange: DateRange) => void;
}

export const DateRangePickerWrapper: React.FunctionComponent<DateRangePickerWrapperProps> = (
  props: DateRangePickerWrapperProps
) => {
  const classes = useStyles();

  const { closeOnClickOutside, wrapperClassName, toggle, open } = props;

  const handleToggle = () => {
    if (closeOnClickOutside === false) {
      return;
    }

    toggle();
  };

  const handleKeyPress = (event: KeyboardEvent) => event?.key === 'Escape' && handleToggle();

  const wrapperClasses = classNames(classes.dateRangePicker, wrapperClassName);

  return (
    <div className={classes.dateRangePickerContainer}>
      {open && <div className={classes.dateRangeBackdrop} onKeyPress={handleKeyPress} onClick={handleToggle} />}

      <div className={wrapperClasses}>
        <DateRangePicker {...props} />
      </div>
    </div>
  );
};
