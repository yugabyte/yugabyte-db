import React, { ReactElement } from 'react';
import { FieldValues, useController, UseControllerProps } from 'react-hook-form';
import { Box, FormHelperText } from '@material-ui/core';

import { YBRadioGroup, YBRadioGroupProps } from './YBRadio';

type YBRadioGroupFieldProps<T extends FieldValues> = UseControllerProps<T> &
  YBRadioGroupProps & {
    /**
     * Performs additional tasks from the parent when the field element changes.
     */
    onRadioChange?: (event: React.ChangeEvent<HTMLInputElement>, value: string) => void;
    isDisabled?: boolean;
  };

export const YBRadioGroupField = <T extends FieldValues>({
  control,
  defaultValue,
  name,
  onRadioChange,
  rules,
  shouldUnregister,
  isDisabled,
  ...ybRadioGroupProps
}: YBRadioGroupFieldProps<T>): ReactElement => {
  const { field, fieldState } = useController({
    name,
    rules,
    defaultValue,
    control,
    shouldUnregister
  });

  const handleChange = (event: React.ChangeEvent<HTMLInputElement>, value: string): void => {
    field.onChange(event, value);
    if (onRadioChange) {
      onRadioChange(event, value);
    }
  };

  ybRadioGroupProps.options = isDisabled
    ? ybRadioGroupProps.options.map((option) => ({ ...option, disabled: true }))
    : ybRadioGroupProps.options;
  return (
    <Box display="flex" flexDirection="column" justifyContent="center">
      <YBRadioGroup
        {...ybRadioGroupProps}
        name={field.name}
        value={field.value}
        onBlur={field.onBlur}
        onChange={handleChange}
        innerRef={field.ref}
      />
      {fieldState.error?.message && (
        <FormHelperText error={true}>{fieldState.error?.message}</FormHelperText>
      )}
    </Box>
  );
};
