import React, { FC, ReactNode } from 'react';
import { TextField, StandardTextFieldProps } from '@material-ui/core';
import { YBTooltip } from '@app/components/YBTooltip/YBTooltip';
import CaretDownIcon from '@app/assets/caret-down.svg';

export type YBSelectProps = { tooltip?: ReactNode; renderValue?: (value: unknown) => ReactNode } & Omit<
  StandardTextFieldProps,
  'variant' | 'color' | 'classes' | 'select' | 'size' | 'placeholder' | 'FormHelperTextProps' | 'SelectProps'
>;

export const YBSelect: FC<YBSelectProps> = ({ label, tooltip, renderValue, ...props }) => (
  <TextField
    {...props}
    label={
      <>
        {label} {tooltip && <YBTooltip title={tooltip} />}
      </>
    }
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
