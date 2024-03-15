import { FC, ReactNode } from 'react';
import { TextField, StandardTextFieldProps, makeStyles } from '@material-ui/core';
import { ReactComponent as CaretDownIcon } from '../assets/caret-down.svg';

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

const useStyles = makeStyles((theme) => ({
  menuPaper: {
    maxHeight: 400,
    marginTop: '6px'
  }
}));

export const YBSelect: FC<YBSelectProps> = ({ renderValue, ...props }) => {
  const classes = useStyles();
  return (
    <TextField
      {...props}
      select
      SelectProps={{
        IconComponent: CaretDownIcon,
        displayEmpty: true,
        renderValue,
        MenuProps: {
          classes: { paper: classes.menuPaper },
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
};
