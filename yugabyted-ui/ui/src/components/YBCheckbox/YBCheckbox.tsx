import React, { FC } from 'react';
import { makeStyles, Typography, FormControlLabel, Checkbox, CheckboxProps, InputProps } from '@material-ui/core';

export interface YBCheckboxProps extends CheckboxProps {
  label: React.ReactNode;
  inputProps?: InputProps['inputProps']; // override type to make it accept custom attributes like "data-testid"
}

const useStyles = makeStyles((theme) => ({
  checkboxIcon: {
    width: 16,
    height: 16,
    margin: '3.14px',
    boxShadow: `inset 0 0 0 2px ${theme.palette.grey[400]}, inset 0 -1px 0 ${theme.palette.grey[400]}`,
    backgroundColor: theme.palette.grey[100],
    'input:hover ~ &': {
      backgroundColor: theme.palette.grey[100]
    },
    'input:disabled ~ &': {
      boxShadow: `inset 0 0 0 2px ${theme.palette.grey[200]}, inset 0 -1px 0 ${theme.palette.grey[200]}`
    }
  }
}));

export const YBCheckbox: FC<YBCheckboxProps> = ({ label, ...checkboxProps }: YBCheckboxProps) => {
  const classes = useStyles();
  return (
    <FormControlLabel
      control={<Checkbox color="primary" {...checkboxProps} icon={<span className={classes.checkboxIcon} />} />}
      label={
        <Typography component="span" variant="body2">
          {label}
        </Typography>
      }
    />
  );
};
