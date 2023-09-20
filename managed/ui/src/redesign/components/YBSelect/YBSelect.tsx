import { FC, ReactNode } from 'react';
import { TextField, StandardTextFieldProps } from '@material-ui/core';
import { ReactComponent as CaretDownIcon } from '../../assets/caret-down.svg';

export type YBSelectProps = {
  tooltip?: ReactNode;
  renderValue?: (value: unknown) => ReactNode;
} & Omit<
  StandardTextFieldProps,
  | 'variant'
  | 'color'
  | 'classes'
  | 'select'
  | 'size'
  | 'placeholder'
  | 'FormHelperTextProps'
  | 'SelectProps'
>;

export const YBSelect: FC<YBSelectProps> = ({ renderValue, ...props }) => (
  <TextField
    {...props}
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
        }
      }
    }}
    variant="standard"
  />
);
