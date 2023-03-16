import React, { ReactElement } from 'react';
import { FieldValues, useController, UseControllerProps } from 'react-hook-form';
import { YBRadioGroup, YBRadioGroupProps } from './YBRadio';

type YBRadioGroupFieldProps<T extends FieldValues> = UseControllerProps<T> &
  YBRadioGroupProps & {
    /**
     * Performs additional tasks from the parent when the field element changes.
     */
    onRadioChange?: (event: React.ChangeEvent<HTMLInputElement>, value: string) => void;
  };

export const YBRadioGroupField = <T extends FieldValues>({
  control,
  defaultValue,
  name,
  onRadioChange,
  rules,
  shouldUnregister,
  ...ybRadioGroupProps
}: YBRadioGroupFieldProps<T>): ReactElement => {
  const { field } = useController({ name, rules, defaultValue, control, shouldUnregister });

  const handleChange = (event: React.ChangeEvent<HTMLInputElement>, value: string): void => {
    field.onChange(event, value);
    if (onRadioChange) {
      onRadioChange(event, value);
    }
  };
  return (
    <YBRadioGroup
      {...ybRadioGroupProps}
      name={field.name}
      value={field.value}
      onBlur={field.onBlur}
      onChange={handleChange}
      innerRef={field.ref}
    />
  );
};
