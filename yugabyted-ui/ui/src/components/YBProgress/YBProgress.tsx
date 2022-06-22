import React, { FC } from 'react';
import { makeStyles, Theme, LinearProgress } from '@material-ui/core';

const useStyles = makeStyles<Theme, ProgressProps>((theme: Theme) => ({
  root: {
    flexGrow: 1,
    height: theme.spacing(1),
    borderRadius: theme.spacing(0.5)
  },
  colorPrimary: {
    backgroundColor: theme.palette.grey[200],
    boxShadow: `inset ${theme.shape.shadowLight}`
  },
  bar: {
    borderRadius: theme.spacing(0.5),
    backgroundColor: ({ color }) => color
  }
}));

interface ProgressProps {
  color?: string | undefined;
  value?: number;
}

export const YBProgress: FC<ProgressProps> = ({ color, value }: ProgressProps) => {
  const classes = useStyles({ color });

  return (
    <LinearProgress
      classes={{
        root: classes.root,
        colorPrimary: classes.colorPrimary,
        bar: classes.bar
      }}
      variant="determinate"
      value={value}
    />
  );
};
