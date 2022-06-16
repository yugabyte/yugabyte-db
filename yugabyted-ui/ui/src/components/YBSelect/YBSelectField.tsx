import React, { ReactElement } from 'react';
import { useController, UseControllerProps } from 'react-hook-form';
import { YBSelect, YBSelectProps } from '@app/components';

type YBSelectFieldProps<T> = UseControllerProps<T> & YBSelectProps;

export const YBSelectField = <T,>(props: YBSelectFieldProps<T>): ReactElement => {
  const { name, rules, defaultValue, control, shouldUnregister, children, ...ybSelectProps } = props;
  const { field, fieldState } = useController({ name, rules, defaultValue, control, shouldUnregister });
  return (
    <YBSelect
      {...ybSelectProps}
      name={field.name}
      inputRef={field.ref}
      onBlur={field.onBlur}
      onChange={field.onChange}
      value={field.value}
      error={!!fieldState.error}
      helperText={fieldState.error?.message ?? ybSelectProps.helperText}
    >
      {children}
    </YBSelect>
  );
};
