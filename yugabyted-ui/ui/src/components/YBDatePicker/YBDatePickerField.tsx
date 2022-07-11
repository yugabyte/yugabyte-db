import React, { useState, ReactElement, ReactNode } from 'react';
import { useController, UseControllerProps } from 'react-hook-form';
import type { DateType } from '@date-io/type';
import { YBDatePicker } from './YBDatePicker';
import { InputAdornment, IconButton, StandardTextFieldProps, makeStyles } from '@material-ui/core';
import { CalendarToday } from '@material-ui/icons';

import { YBInput } from '@app/components';

const useStyles = makeStyles((theme) => ({
  inputWithCursorPointer: {
    '& .MuiInputBase-input': {
      cursor: 'pointer'
    },
    '& .MuiInputAdornment-root': {
      marginLeft: theme.spacing(0.5)
    }
  }
}));

export type YBDatePickerPropsInput = {
  tooltip?: ReactNode;
  format?: string;
  disablePast?: boolean;
  disableFuture?: boolean;
  disabled?: boolean;
  maxDate?: Date;
  minDate?: Date;
  helperText?: string;
} & Omit<
  StandardTextFieldProps,
  'variant' | 'color' | 'classes' | 'size' | 'select' | 'FormHelperTextProps' | 'SelectProps'
>;

type YBDatePickerProps<T> = UseControllerProps<T> & YBDatePickerPropsInput;

export const YBDatePickerField = <T,>(props: YBDatePickerProps<T>): ReactElement => {
  const {
    name,
    rules,
    defaultValue,
    control,
    label,
    shouldUnregister,
    fullWidth,
    disablePast,
    disableFuture,
    minDate,
    maxDate,
    disabled,
    format
  } = props;
  const classes = useStyles();
  const {
    field: { ref, value, ...fieldProps },
    fieldState
  } = useController({ name, rules, defaultValue, control, shouldUnregister });
  const [selectedDate, setSelectedDate] = useState<DateType | null>((value as unknown) as DateType);
  const handleDateChange = (date: DateType | null) => {
    setSelectedDate(date);
    fieldProps.onChange(date);
  };

  return (
    <YBDatePicker
      inputRef={ref}
      date={selectedDate}
      value={selectedDate}
      onChange={handleDateChange}
      format={format}
      disablePast={disablePast}
      disableFuture={disableFuture}
      TextFieldComponent={YBInput}
      minDate={minDate ?? null}
      maxDate={maxDate ?? null}
      InputProps={{
        ...fieldProps,
        fullWidth,
        className: classes.inputWithCursorPointer,
        startAdornment: (
          <InputAdornment position="end">
            <IconButton>
              <CalendarToday fontSize="small" />
            </IconButton>
          </InputAdornment>
        ),
        label,
        inputRef: ref,
        error: !!fieldState.error,
        disabled: disabled ?? false
      }}
    />
  );
};
