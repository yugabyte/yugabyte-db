import React, { ReactElement } from 'react';
import { useController, UseControllerProps } from 'react-hook-form';
import { YBRadioGroup, YBRadioGroupProps } from './YBRadio';

type YBRadioGroupFieldProps<T> = UseControllerProps<T> & YBRadioGroupProps;

export const YBRadioGroupField = <T,>(props: YBRadioGroupFieldProps<T>): ReactElement => {
  const { name, rules, defaultValue, control, shouldUnregister, ...ybRadioGroupProps } = props;
  const { field } = useController({ name, rules, defaultValue, control, shouldUnregister });

  return (
    <YBRadioGroup
      name={field.name}
      value={field.value}
      onBlur={field.onBlur}
      onChange={field.onChange}
      innerRef={field.ref}
      {...ybRadioGroupProps}
    />
  );
};
