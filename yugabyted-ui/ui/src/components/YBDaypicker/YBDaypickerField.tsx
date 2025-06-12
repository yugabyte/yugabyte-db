import React, { ReactElement } from 'react';
import { useController, UseControllerProps } from 'react-hook-form';
import type { FieldValues } from 'react-hook-form';
import { YBDaypicker, YBDaypickerProps } from './YBDaypicker';

type YBInputFieldProps<T extends FieldValues> = UseControllerProps<T> & YBDaypickerProps;

export const YBDayPickerField =
  <T extends FieldValues,>(props: YBInputFieldProps<T>): ReactElement => {
  const { name, rules, defaultValue, control, shouldUnregister } = props;
  const { field, fieldState } = useController({ name, rules, defaultValue, control, shouldUnregister });

  const handleChange = (days: number[]) => {
    field.onChange(days);
  };

  return (
    <YBDaypicker
      label={props?.label}
      onChange={handleChange}
      value={field.value as number[]}
      error={!!fieldState.error}
      helperText={fieldState.error?.message ?? ''}
    />
  );
};
