import React, { ReactElement } from 'react';
import { useController, UseControllerProps } from 'react-hook-form';
import type { FieldValues } from 'react-hook-form';
import { YBInput, YBInputProps } from './YBInput';

export type YBInputFieldProps<T extends FieldValues> = UseControllerProps<T> & YBInputProps;

export const YBInputField =
  <T extends FieldValues,>(props: YBInputFieldProps<T>): ReactElement => {
  const { name, rules, defaultValue, control, shouldUnregister, ...ybInputProps } = props;
  const {
    field: { ref, ...fieldProps },
    fieldState
  } = useController({ name, rules, defaultValue, control, shouldUnregister });

  return (
    <YBInput
      {...fieldProps}
      {...ybInputProps}
      inputRef={ref}
      error={!!fieldState.error}
      helperText={fieldState.error?.message ?? ybInputProps.helperText}
    />
  );
};
