import React, { forwardRef, ReactNode } from 'react';
import { TextField, StandardTextFieldProps, makeStyles } from '@material-ui/core';
import { YBTooltip } from '@app/components/YBTooltip/YBTooltip';
import CaretDownIcon from '@app/assets/caret-down.svg';

const useStyles = makeStyles((theme) => ({
  selectMenu: {
    '& .MuiPaper-root': {
      minWidth: '100%',
      maxWidth: 'none',
      width: 'max-content',
    },
    '& .MuiMenu-list': {
      padding: theme.spacing(1, 0),
      display: 'flex !important',
      flexDirection: 'column !important',
    },
    '& .MuiMenuItem-root': {
      padding: theme.spacing(1, 2),
      minHeight: 'auto',
      display: 'block !important',
      width: '100% !important',
      whiteSpace: 'nowrap',
      '&:hover': {
        backgroundColor: theme.palette.action.hover,
      },
    },
    '& .MuiDivider-root': {
      margin: theme.spacing(0.5, 0),
      width: '100%',
    },
  },
}));

export type YBSelectProps = { tooltip?: ReactNode; renderValue?: (value: unknown) => ReactNode } & Omit<
  StandardTextFieldProps,
  'variant' | 'color' | 'classes' | 'select' | 'size' | 'placeholder' | 'FormHelperTextProps'
>;

export const YBSelect = forwardRef<HTMLInputElement, YBSelectProps>(
  ({ label, tooltip, renderValue, SelectProps: customSelectProps, ...props }, ref) => {
    const classes = useStyles();

    return (
      <TextField
        {...props}
        inputRef={ref}
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
          ...customSelectProps,
          MenuProps: {
            getContentAnchorEl: null,
            anchorOrigin: {
              vertical: 'bottom',
              horizontal: 'left'
            },
            className: classes.selectMenu,
            ...customSelectProps?.MenuProps,
          }
        }}
        variant="standard"
      />
    );
  }
);
