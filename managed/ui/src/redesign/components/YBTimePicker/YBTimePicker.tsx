import { useState, ReactElement, ReactNode } from 'react';
import { Moment } from 'moment';
import TimePicker, { TimePickerProps } from 'rc-time-picker';

import { FieldValues, useController, UseControllerProps } from 'react-hook-form';
import {
  InputLabel,
  makeStyles
} from '@material-ui/core';
import 'rc-time-picker/assets/index.css';

const useStyles = makeStyles((theme) => ({
  inputWithCursorPointer: {
    '& .rc-time-picker-input': {
      cursor: 'pointer',
      height: '42px',
      borderRadius: '8px',
      fontSize: theme.typography.body1.fontSize,
      fontWeight: theme.typography.body2.fontWeight,
      color: 'inherit'
    }
  }
}));

export type YBTimePickerPropsInput = { tooltip?: ReactNode; label?: string } & TimePickerProps;

type YBTimePickerProps<T extends FieldValues> = UseControllerProps<T> & YBTimePickerPropsInput;

export const YBTimePicker = <T extends FieldValues>(props: YBTimePickerProps<T>): ReactElement => {
  const { name, rules, defaultValue, control, label, shouldUnregister } = props;
  const classes = useStyles();
  const {
    field: { ref, value, ...fieldProps }
  } = useController({ name, rules, defaultValue, control, shouldUnregister });
  const [selectedDate, setSelectedDate] = useState<Moment | undefined>((value as unknown) as Moment);
  const handleDateChange = (date: Moment | undefined) => {
    setSelectedDate(date);
    fieldProps.onChange(date);
  };

  return (
    <>
      <InputLabel>{label}</InputLabel>
      <TimePicker
        className={classes.inputWithCursorPointer}
        ref={ref}
        value={selectedDate}
        onChange={handleDateChange}
        {...props}
      />
    </>
  );
};
