import React, { FC } from 'react';
import { Box, makeStyles } from '@material-ui/core';

const useYBLabelStyles = makeStyles(() => ({
  container: {
    width: '150px',
    alignItems: 'center',
    display: 'flex',
    fontSize: '13px',
    fontWeight: 500
  }
}));

interface YBLabelProps {
  dataTestId?: string;
}

export const YBLabel: FC<YBLabelProps> = ({ children, dataTestId }) => {
  const classes = useYBLabelStyles();
  return (
    <Box className={classes.container} data-testid={dataTestId}>
      {children}
    </Box>
  );
};
