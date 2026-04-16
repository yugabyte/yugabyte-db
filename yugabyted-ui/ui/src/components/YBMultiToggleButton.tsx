import React, { ReactElement } from 'react';
import { ToggleButton, ToggleButtonGroup, ToggleButtonGroupProps } from '@material-ui/lab';
import { makeStyles, Theme } from '@material-ui/core';

const useStyles = makeStyles<Theme, { dense?: boolean }>((theme) => ({
  toggleButtonGroup: {
    border: `1px solid ${theme.palette.grey[300]}`,
    height: theme.spacing(4),
    backgroundColor: theme.palette.primary[100],
    borderRadius: theme.spacing(1.25), // Add overall border radius
  },
  toggleButton: {
    '&.MuiToggleButton-root': {
      fontWeight: 400,
      fontSize: 13,
      padding: ({ dense }) => theme.spacing(0, dense ? 1 : 2),
      color: theme.palette.grey[900],
      border: 'none', // Remove individual borders
    },

    textTransform: 'none',
    '&:not(:first-child)': {
      marginLeft: 0
    },

    '&:hover': {
      backgroundColor: theme.palette.primary[100]
    },

    '&.Mui-selected': {
      backgroundColor: theme.palette.background.paper,
      color: theme.palette.primary.main,
      fontWeight: 600,
      borderRadius: theme.spacing(1.25), // Match parent radius
      border: `1px solid ${theme.palette.primary.main}`,

      '&:hover': {
        backgroundColor: theme.palette.background.paper
      },
      // Remove extra borders for a seamless look
    }
  }
}));

export interface ToggleButtonData<T> {
  label: ReactElement | string;
  value: T;
  dataTestId?: string;
}

interface YBMultiToggleButtonProps<T = string | number> extends
  Omit<ToggleButtonGroupProps, 'onChange'> {
  dense?: boolean;
  options: ToggleButtonData<T>[];
  value: T;
  onChange: (value: T) => void;
}

export const YBMultiToggleButton = <T,>({
  options,
  value,
  onChange,
  dense,
  ...rest
}: YBMultiToggleButtonProps<T>): ReactElement => {
  const classes = useStyles({ dense });
  return (
    <ToggleButtonGroup
      exclusive
      value={value}
      onChange={(_event, newValue) => {
        if (newValue !== null) {
          onChange(newValue);
        }
      }}
      className={classes.toggleButtonGroup}
      {...rest}
    >
      {options.map((option) => (
        <ToggleButton
          key={String(option.value)}
          value={option.value}
          className={classes.toggleButton}
          data-testid={option.dataTestId ?? `${option.label as string}ToggleButton`}
        >
          {option.label}
        </ToggleButton>
      ))}
    </ToggleButtonGroup>
  );
};
