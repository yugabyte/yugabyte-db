/*
 * Copyright 2023 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
import { FormHelperText, useTheme } from '@material-ui/core';
import React from 'react';
import { FieldValues, useController, UseControllerProps } from 'react-hook-form';
import { Styles } from 'react-select';

import {
  YBMultiEntryInput,
  YBMultiEntryInputProps
} from '../../../common/forms/fields/YBMultiEntryInput';

// Val is handled by the react-hook-form controller.
interface YBMultiEntryInputFieldProps<T extends FieldValues>
  extends Omit<YBMultiEntryInputProps, 'val'> {
  controllerProps: UseControllerProps<T>;
}

export const YBMultiEntryInputField = <T extends FieldValues>({
  controllerProps,
  ...ybMultiEntryInputProps
}: YBMultiEntryInputFieldProps<T>) => {
  const { field, fieldState } = useController(controllerProps);
  const theme = useTheme();
  const multiSelectStyles: Partial<Styles> = {
    control: (baseStyles) => ({
      ...baseStyles,
      borderRadius: '8px',
      backgroundColor: fieldState.error ? theme.palette.error[100] : baseStyles.backgroundColor,
      borderColor: fieldState.error ? theme.palette.orange[700] : baseStyles.borderColor
    })
  };

  return (
    <div>
      <YBMultiEntryInput
        {...ybMultiEntryInputProps}
        onChange={field.onChange}
        val={field.value}
        styles={multiSelectStyles}
      />
      {fieldState.error?.message && (
        <FormHelperText error={true}>{fieldState.error?.message}</FormHelperText>
      )}
    </div>
  );
};
