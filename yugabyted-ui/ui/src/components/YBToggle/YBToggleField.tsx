import React, { ReactElement } from 'react';
import { useController, UseControllerProps } from 'react-hook-form';
import { YBToggle, YBToggleProps } from './YBToggle';

type YBInputFieldProps<T> = UseControllerProps<T> & YBToggleProps;

export const YBToggleField = <T,>(props: YBInputFieldProps<T>): ReactElement => {
  const { name, rules, defaultValue, control, shouldUnregister, ...ybToggleProps } = props;
  const {
    field: { ref, value, ...fieldProps }
  } = useController({ name, rules, defaultValue, control, shouldUnregister });

  return <YBToggle {...fieldProps} {...ybToggleProps} checked={!!value} />;
};
