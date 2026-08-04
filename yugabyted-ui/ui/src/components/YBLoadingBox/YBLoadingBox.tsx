import React, { FC } from 'react';
import clsx from 'clsx';
import { Paper, makeStyles } from '@material-ui/core';

interface LoadingBoxProps {
  theme?: 'light' | 'default';
  className?: string;
}

const useStyles = makeStyles((theme) => ({
  loadingContainer: {
    padding: theme.spacing(2),
    margin: 0,
    border: `1px dashed ${theme.palette.primary[300]}`,
    textAlign: 'center'
  },
  default: {
    background: theme.palette.primary[100]
  },
  light: {
    background: theme.palette.common.white,
    color: theme.palette.grey[600]
  }
}));

export const YBLoadingBox: FC<LoadingBoxProps> = ({ children, theme, className }) => {
  const classes = useStyles();

  return (
    <Paper
      variant="outlined"
      elevation={0}
      className={clsx(classes.loadingContainer, theme === 'light' ? classes.light : classes.default, className)}
    >
      {children}
    </Paper>
  );
};
