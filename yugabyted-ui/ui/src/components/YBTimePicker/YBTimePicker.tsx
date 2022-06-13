import React, { useState, ReactElement, ReactNode } from 'react';
import { useController, UseControllerProps } from 'react-hook-form';
import { MuiPickersUtilsProvider, TimePicker } from 'material-ui-pickers-v4';
import type { DateType } from '@date-io/type';
import DateFnsUtils from '@date-io/date-fns';
import { InputAdornment, IconButton, StandardTextFieldProps, InputLabel, makeStyles } from '@material-ui/core';
import { AccessTime } from '@material-ui/icons';

import { YBInput } from '@app/components';

const useStyles = makeStyles(() => ({
  inputWithCursorPointer: {
    '& .MuiInputBase-input': {
      cursor: 'pointer'
    }
  }
}));

export type YBTimePickerPropsInput = { tooltip?: ReactNode } & Omit<
  StandardTextFieldProps,
  'variant' | 'color' | 'classes' | 'size' | 'select' | 'FormHelperTextProps' | 'SelectProps'
>;

type YBTimePickerProps<T> = UseControllerProps<T> & YBTimePickerPropsInput;

export const YBTimePicker = <T,>(props: YBTimePickerProps<T>): ReactElement => {
  const { name, rules, defaultValue, control, label, shouldUnregister } = props;
  const classes = useStyles();
  const {
    field: { ref, value, ...fieldProps }
  } = useController({ name, rules, defaultValue, control, shouldUnregister });
  const [selectedDate, setSelectedDate] = useState<DateType | null>((value as unknown) as DateType);
  const handleDateChange = (date: DateType | null) => {
    setSelectedDate(date);
    fieldProps.onChange(date);
  };

  return (
    <MuiPickersUtilsProvider utils={DateFnsUtils}>
      <InputLabel>{label}</InputLabel>
      <TimePicker
        TextFieldComponent={YBInput}
        InputProps={{
          ...fieldProps,
          className: classes.inputWithCursorPointer,
          endAdornment: (
            <InputAdornment position="end">
              <IconButton>
                <AccessTime fontSize="small" />
              </IconButton>
            </InputAdornment>
          ),
          label
        }}
        inputRef={ref}
        date={selectedDate}
        value={selectedDate}
        ampm
        onChange={handleDateChange}
      />
    </MuiPickersUtilsProvider>
  );
};
