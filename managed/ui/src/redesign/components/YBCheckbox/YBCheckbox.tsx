import React, { FC } from 'react';
import {
  Typography,
  FormControlLabel,
  Checkbox,
  CheckboxProps,
  InputProps,
  TypographyProps
} from '@material-ui/core';

export interface YBCheckboxProps extends CheckboxProps {
  label: React.ReactNode;
  inputProps?: InputProps['inputProps']; // override type to make it accept custom attributes like "data-testid"
  labelProps?: TypographyProps;
}

export const YBCheckbox: FC<YBCheckboxProps> = ({
  label,
  labelProps,
  ...checkboxProps
}: YBCheckboxProps) => {
  return (
    <FormControlLabel
      control={<Checkbox color="primary" {...checkboxProps} />}
      label={
        <Typography component="span" variant="body2" {...labelProps}>
          {label}
        </Typography>
      }
    />
  );
};
