import React, { ReactElement } from 'react';
import { useController, UseControllerProps } from 'react-hook-form';
import type { FieldValues } from 'react-hook-form';
import { YBToggle, YBToggleProps } from './YBToggle';

type YBInputFieldProps<T extends FieldValues> = UseControllerProps<T> & YBToggleProps;

export const YBToggleField =
  <T extends FieldValues,>(props: YBInputFieldProps<T>): ReactElement => {
  const { name, rules, defaultValue, control, shouldUnregister, ...ybToggleProps } = props;
  const {
    field: { ref, value, ...fieldProps }
  } = useController({ name, rules, defaultValue, control, shouldUnregister });

  return <YBToggle {...fieldProps} {...ybToggleProps} checked={!!value} />;
};
