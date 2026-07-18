import React, { FC } from 'react';
import {
  makeStyles,
  FormControl,
  Typography,
  Switch,
  FormControlLabel,
  withStyles,
  Theme,
  createStyles,
  TypographyVariant,
  SwitchProps,
  InputProps
} from '@material-ui/core';

const AntSwitch = withStyles((theme: Theme) => {
  return createStyles({
    root: {
      width: 38,
      height: 20,
      marginRight: 8,
      padding: 0,
      display: 'flex'
    },
    switchBase: {
      padding: `${theme.spacing(0.5)}px !important`,
      color: theme.palette.common.white,
      '&$checked': {
        transform: 'translateX(17px)',
        color: theme.palette.common.white,
        '& + $track': {
          opacity: 1,
          backgroundColor: theme.palette.primary.main,
          borderColor: theme.palette.primary.main
        }
      }
    },
    thumb: {
      width: 12,
      height: 12,
      boxShadow: 'none',
      color: theme.palette.common.white
    },
    track: {
      border: `1px solid ${theme.palette.grey[500]}`,
      borderRadius: 22 / 2,
      opacity: 1,
      backgroundColor: theme.palette.grey[500]
    },
    checked: {}
  });
})(Switch);

const useStyles = makeStyles((theme) => ({
  root: {
    marginLeft: 0
  },
  label: {
    ...theme.typography.body2
  }
}));

interface ToggleFormControlProps {
  fullWidth?: boolean;
  error?: boolean;
}

export interface YBToggleProps extends SwitchProps {
  label?: string;
  labelVariant?: TypographyVariant;
  FormControlProps?: ToggleFormControlProps;
  inputProps?: InputProps['inputProps'];
}

export const YBToggle: FC<YBToggleProps> = ({ label, labelVariant, FormControlProps, ...props }: YBToggleProps) => {
  const formLabelClasses = useStyles();
  return (
    <FormControl {...FormControlProps}>
      <FormControlLabel
        control={<AntSwitch color="primary" {...props} />}
        labelPlacement="end"
        label={<Typography variant={labelVariant ?? 'body2'}>{label}</Typography>}
        classes={formLabelClasses}
      />
    </FormControl>
  );
};
