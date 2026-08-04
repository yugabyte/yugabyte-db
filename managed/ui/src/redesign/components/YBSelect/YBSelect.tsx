import { FC, ReactNode } from 'react';
import { TextField, StandardTextFieldProps } from '@material-ui/core';
import CaretDownIcon from '../../assets/caret-down.svg';

export type YBSelectProps = {
  tooltip?: ReactNode;
  renderValue?: (value: unknown) => ReactNode;
} & Omit<
  StandardTextFieldProps,
  'variant' | 'color' | 'classes' | 'select' | 'size' | 'placeholder' | 'FormHelperTextProps'
>;

export const YBSelect: FC<YBSelectProps> = ({ renderValue, ...props }) => {
  const { SelectProps: selectPropsOverride, ...remainingTextFieldProps } = props;
  const { MenuProps: menuPropsOverride, ...remainingSelectProps } = selectPropsOverride ?? {};
  return (
    <TextField
      {...remainingTextFieldProps}
      select
      SelectProps={{
        IconComponent: CaretDownIcon,
        displayEmpty: true,
        renderValue,
        MenuProps: {
          getContentAnchorEl: null,
          anchorOrigin: {
            vertical: 'bottom',
            horizontal: 'left'
          },
          ...menuPropsOverride
        },
        ...remainingSelectProps
      }}
      variant="standard"
    />
  );
};
