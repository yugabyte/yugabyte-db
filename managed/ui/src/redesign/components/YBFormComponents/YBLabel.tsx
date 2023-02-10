import React, { FC } from 'react';
import { Box, makeStyles } from '@material-ui/core';

interface YBLabelProps {
  dataTestId?: string;
  width?: string;
}

const useYBLabelStyles = makeStyles((theme) => ({
  root: ({ width }: YBLabelProps) => ({
    width: width ?? '170px',
    alignItems: 'center',
    display: 'flex',
    fontSize: '13px',
    fontWeight: 500,
    fontFamily: 'Inter',
    color: theme.palette.ybacolors.labelBackground,
    fontStyle: 'normal'
  })
}));

export const YBLabel: FC<YBLabelProps> = (props) => {
  const classes = useYBLabelStyles(props);

  return (
    <Box className={classes.root} data-testid={props.dataTestId}>
      {props.children}
    </Box>
  );
};
