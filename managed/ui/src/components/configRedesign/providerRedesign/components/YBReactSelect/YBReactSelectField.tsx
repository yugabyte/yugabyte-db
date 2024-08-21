/*
 * Copyright 2022 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
import Select, { components, OptionProps, Styles } from 'react-select';
import { Box, FormHelperText, Typography, useTheme } from '@material-ui/core';
import { FieldValues, useController, UseControllerProps } from 'react-hook-form';

import { SelectComponents } from 'react-select/src/components';
import { YBTooltip } from '../../../../../redesign/components';

export type ReactSelectOption = { value: any; label: string; isDisabled?: boolean };
export type ReactSelectGroupedOption = { label: string; options: ReactSelectOption[] };
export type YBReactSelectFieldProps<TFieldValues extends FieldValues> = {
  options: readonly ReactSelectGroupedOption[] | readonly ReactSelectOption[] | undefined;

  components?: Partial<SelectComponents<ReactSelectOption>>;
  isDisabled?: boolean;
  onChange?: (value: ReactSelectOption) => void;
  placeholder?: string;
  stylesOverride?: Partial<Styles>;
  width?: string; // Will override dynamic width.
  autoSizeMinWidth?: number; // If specified, will grow the field width from a given minimum.
  accessoryContainerWidthPx?: number;
  maxWidth?: string;
} & UseControllerProps<TFieldValues>;

export const YBReactSelectField = <T extends FieldValues>({
  options,
  onChange,
  components,
  isDisabled = false,
  placeholder,
  stylesOverride,
  width,
  autoSizeMinWidth,
  accessoryContainerWidthPx = 0,
  maxWidth,
  ...useControllerProps
}: YBReactSelectFieldProps<T>) => {
  const { field, fieldState } = useController(useControllerProps);
  const theme = useTheme();

  const reactSelectStyles: Partial<Styles> = {
    ...stylesOverride,
    control: (baseStyles) => ({
      ...baseStyles,
      height: 42,
      borderRadius: 8,
      border: `1px solid ${
        fieldState.error ? theme.palette.orange[700] : theme.palette.ybacolors.ybGray
      }`,
      backgroundColor: fieldState.error ? theme.palette.error[100] : baseStyles.backgroundColor,
      ...stylesOverride?.control
    }),
    menu: (baseStyles) => ({
      ...baseStyles,
      zIndex: 9999,
      ...stylesOverride?.menu
    }),
    placeholder: (baseStyles) => ({
      ...baseStyles,
      color: theme.palette.grey[300],
      ...stylesOverride?.placeholder
    })
  };

  const handleChange = (value: any) => {
    field.onChange(value);
    onChange && onChange(value);
  };
  // We scale the width by multiplying the option label length by a constant factor and add a constant
  // width to account for accessory components like pills/badges.
  const autosizedWidth = Math.max(
    (field.value?.label?.length ?? 0) * 11 + accessoryContainerWidthPx,
    autoSizeMinWidth ?? 300
  );
  return (
    <Box width={width ?? (autoSizeMinWidth ? `${autosizedWidth}px` : '100%')} maxWidth={maxWidth}>
      <div data-testid={`YBReactSelectField-${field.name}`}>
        <Select
          styles={reactSelectStyles}
          name={field.name}
          onChange={handleChange}
          onBlur={field.onBlur}
          value={field.value}
          components={{ Option: Option, ...components }}
          options={options}
          isDisabled={isDisabled}
          placeholder={placeholder}
          menuShouldScrollIntoView={true}
        />
      </div>
      {fieldState.error?.message && (
        <FormHelperText error={true}>{fieldState.error?.message}</FormHelperText>
      )}
    </Box>
  );
};

/**
 * Customized React-Select Option which adds support for disabled option tooltip.
 */
export const Option = <T extends FieldValues>({ children, ...props }: OptionProps<T>) => (
  <components.Option {...props}>
    <YBTooltip
      title={
        props.isDisabled && props.data?.disabledReason ? (
          <Typography variant="body2">{props.data.disabledReason}</Typography>
        ) : (
          ''
        )
      }
    >
      <div>{children}</div>
    </YBTooltip>
  </components.Option>
);
