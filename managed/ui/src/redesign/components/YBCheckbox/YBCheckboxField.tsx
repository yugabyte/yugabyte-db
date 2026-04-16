import React, { ReactElement } from 'react';
import { FieldValues, useController, UseControllerProps } from 'react-hook-form';
import { YBCheckbox, YBCheckboxProps } from './YBCheckbox';

type YBCheckboxFieldProps<T extends FieldValues> = UseControllerProps<T> & YBCheckboxProps;

export const YBCheckboxField = <T extends FieldValues>(
  props: YBCheckboxFieldProps<T>
): ReactElement => {
  const { name, rules, defaultValue, control, shouldUnregister, ...ybCheckboxProps } = props;
  const { field } = useController({ name, rules, defaultValue, control, shouldUnregister });

  return (
    <YBCheckbox
      name={field.name}
      inputRef={field.ref}
      checked={!!field.value}
      onBlur={field.onBlur}
      onChange={field.onChange}
      {...ybCheckboxProps}
    />
  );
};
