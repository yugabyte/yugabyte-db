import React from 'react';
import clsx from 'clsx';
import { Box, makeStyles } from '@material-ui/core';

export enum YBHelperVariants {
  error = 'ERROR',
  warning = 'WARNING',
  primary = 'PRIMARY',
  success = 'SUCCESS'
}
interface YBHelperProps {
  variant?: YBHelperVariants;
  children?: React.ReactNode;
  dataTestId?: string;
}

const useYBHelperStyles = makeStyles((theme) => ({
  primary: {
    width: 'auto',
    alignItems: 'center',
    fontSize: '11px'
  },
  warning: {
    color: theme.palette.warning[700],
    fontSize: '12px'
  },
  error: {
    color: theme.palette.error[500]
  },
  success: {
    color: theme.palette.success[500]
  }
}));

export const YBHelper = ({
  children,
  variant = YBHelperVariants.primary,
  dataTestId
}: YBHelperProps) => {
  const classes = useYBHelperStyles();
  return (
    <Box
      className={clsx(
        variant === YBHelperVariants.primary && classes.primary,
        variant === YBHelperVariants.warning && classes.warning,
        variant === YBHelperVariants.error && classes.error,
        variant === YBHelperVariants.success && classes.success
      )}
      data-testid={dataTestId}
    >
      {children}
    </Box>
  );
};
