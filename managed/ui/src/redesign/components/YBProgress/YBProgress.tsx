import React, { FC } from 'react';
import { makeStyles, Theme, LinearProgress, Box, Typography } from '@material-ui/core';

const useStyles = makeStyles<Theme, ProgressProps>((theme: Theme) => ({
  root: {
    flexGrow: 1,
    minWidth: '100px',
    height: theme.spacing(1),
    borderRadius: theme.spacing(0.5)
  },
  colorPrimary: {
    backgroundColor: theme.palette.grey[200],
    boxShadow: ({ flat }) => (flat ? 'none' : `inset ${theme.shape.shadowLight}`)
  },
  bar: {
    borderRadius: theme.spacing(0.5),
    backgroundColor: ({ color }) => color
  }
}));

interface ProgressProps {
  color?: string | undefined;
  value?: number;
  flat?: boolean;
}

export const YBProgress: FC<ProgressProps> = ({ color, value, flat }: ProgressProps) => {
  const classes = useStyles({ color, flat });

  return (
    <Box display="flex" alignItems="center">
      <Box mr={1}>
        <LinearProgress
          classes={{
            root: classes.root,
            colorPrimary: classes.colorPrimary,
            bar: classes.bar
          }}
          variant="determinate"
          value={value}
        />
      </Box>

      <Box minWidth={25}>
        <Typography variant="body2" color="textSecondary">{`${value}%`}</Typography>
      </Box>
    </Box>
  );
};
