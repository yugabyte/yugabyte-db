import React, { FC } from 'react';
import { makeStyles, Theme, LinearProgress } from '@material-ui/core';

const useStyles = makeStyles<Theme, ProgressProps>((theme: Theme) => ({
  root: {
    flexGrow: 1,
    height: theme.spacing(1.5),
    borderRadius: theme.spacing(0.4),
  },
  colorPrimary: {
    backgroundColor: theme.palette.grey[200],
  },
  bar: {
    borderRadius: 0,
    background: ({ color }) => color ?? `linear-gradient(90deg, 
      rgba(35, 109, 246, 0.6) 0%, 
      rgba(118, 51, 254, 0.6) 85.4%, 
      rgba(224, 30, 90, 0.6) 118.81%, 
      rgba(218, 21, 21, 0.6) 136.3%)`,
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
