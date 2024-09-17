import { FC, ReactNode } from 'react';
import { TextField, StandardTextFieldProps } from '@material-ui/core';
import { YBTooltip } from '../YBTooltip/YBTooltip';

export type YBInputProps = { tooltip?: ReactNode; trimWhitespace?: boolean } & Omit<
  StandardTextFieldProps,
  'variant' | 'color' | 'classes' | 'size' | 'select' | 'FormHelperTextProps' | 'SelectProps'
>;

export const YBInput: FC<YBInputProps> = ({ label, tooltip, trimWhitespace = true, ...props }) => (
  <TextField
    onBlur={(e) => {
      // Trim whitespace from the input value on blur
      const trimmed = e.target.value.trim();
      // we check if the value has changed before updating it
      // Becuase we don't want to reset the form error unnecessarily
      if (trimWhitespace && e.target.value !== trimmed) {
        e.target.value = e.target.value.trim();
        if (props.onBlur) {
          props.onBlur(e);
        }
        if (props.onChange) {
          props.onChange(e);
        }
      }
    }}
    {...props}
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
