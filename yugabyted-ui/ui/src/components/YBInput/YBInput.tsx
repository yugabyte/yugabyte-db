import React, { FC, ReactNode } from 'react';
import { TextField, StandardTextFieldProps } from '@material-ui/core';
import { YBTooltip } from '@app/components/YBTooltip/YBTooltip';

export type YBInputProps = { tooltip?: ReactNode } & Omit<
  StandardTextFieldProps,
  'variant' | 'color' | 'classes' | 'size' | 'select' | 'FormHelperTextProps' | 'SelectProps'
>;

export const YBInput: FC<YBInputProps> = ({ label, tooltip, ...props }) => (
  <TextField
    {...props}
    label={
      <>
        {label} {tooltip && <YBTooltip title={tooltip} />}
      </>
    }
    variant="standard"
  />
);
