/*
 * Copyright 2022 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
import React from 'react';
import { FieldValues, useController, UseControllerProps } from 'react-hook-form';
import { Box, FormHelperText, useTheme } from '@material-ui/core';
import Select, { Styles } from 'react-select';

type YBReactSelectFieldProps<T extends FieldValues> = {
  options: readonly { value: any; label: string; isDisabled?: boolean }[] | undefined;
  isDisabled?: boolean;
} & UseControllerProps<T>;

export const YBReactSelectField = <T extends FieldValues>({
  options,
  isDisabled = false,
  ...useControllerProps
}: YBReactSelectFieldProps<T>) => {
  const { field, fieldState } = useController(useControllerProps);
  const theme = useTheme();

  const reactSelectStyles: Partial<Styles> = {
    control: (baseStyles) => ({
      ...baseStyles,
      height: 42,
      borderRadius: 8,
      border: `1px solid ${
        fieldState.error ? theme.palette.orange[700] : theme.palette.ybacolors.ybGray
      }`,
      backgroundColor: fieldState.error ? theme.palette.error[100] : baseStyles.backgroundColor
    })
  };
  return (
    <Box width="100%">
      <div data-testid={`YBReactSelectField-${field.name}`}>
        <Select
          styles={reactSelectStyles}
          name={field.name}
          onChange={field.onChange}
          onBlur={field.onBlur}
          value={field.value}
          options={options}
          isDisabled={isDisabled}
        />
      </div>
      {fieldState.error?.message && (
        <FormHelperText error={true}>{fieldState.error?.message}</FormHelperText>
      )}
    </Box>
  );
};
