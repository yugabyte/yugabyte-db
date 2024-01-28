import { FC, ReactNode } from 'react';
import { TextField, StandardTextFieldProps } from '@material-ui/core';
import { YBTooltip } from '../YBTooltip/YBTooltip';

export type YBInputProps = { tooltip?: ReactNode; trimWhitespace?: boolean } & Omit<
  StandardTextFieldProps,
  'variant' | 'color' | 'classes' | 'size' | 'select' | 'FormHelperTextProps' | 'SelectProps'
>;

export const YBInput: FC<YBInputProps> = ({ label, tooltip, trimWhitespace = true, ...props }) => (
  <TextField
    {...props}
    onBlur={(e) => {
      if (trimWhitespace) {
        e.target.value = e.target.value.trim();
        if (props.onBlur) {
          props.onBlur(e);
        }
        if (props.onChange) {
          props.onChange(e);
        }
      }
    }}
    label={
      label && (
        <>
          {label} {tooltip && <YBTooltip title={tooltip} />}
        </>
      )
    }
    variant="standard"
  />
);
