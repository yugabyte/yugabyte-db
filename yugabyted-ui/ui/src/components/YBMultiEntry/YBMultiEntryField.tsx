import React, { ReactElement } from 'react';
import { useController, UseControllerProps } from 'react-hook-form';
import { YBMultiEntry, YBMultiEntryProps } from './YBMultiEntry';

type YBMultiEntryFieldProps<T> = UseControllerProps<T> & YBMultiEntryProps;

export const YBMultiEntryField = <T,>(props: YBMultiEntryFieldProps<T>): ReactElement => {
  const { name, rules, defaultValue, control, shouldUnregister, placeholderText, value } = props;
  const { field, fieldState } = useController({ name, rules, defaultValue, control, shouldUnregister });

  return (
    <YBMultiEntry
      label={props?.label}
      onChange={field.onChange}
      value={value}
      error={fieldState.error}
      helperText={fieldState.error?.message ?? ''}
      placeholderText={placeholderText}
    />
  );
};
