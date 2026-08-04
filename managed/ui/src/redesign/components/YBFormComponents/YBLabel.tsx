import { FC } from 'react';
import { Box, makeStyles, useTheme } from '@material-ui/core';
import clsx from 'clsx';

interface YBLabelProps {
  dataTestId?: string;
  width?: string;
}

const useYBLabelStyles = makeStyles((theme) => ({
  container: ({ width }: YBLabelProps) => ({
    width: width ?? '170px',
    alignItems: 'center',
    display: 'flex',
    fontSize: '13px',
    fontWeight: 400,
    fontFamily: 'Inter',
    color: theme.palette.ybacolors.labelBackground,
    fontStyle: 'normal'
  })
}));

interface YBLabelProps {
  className?: string;
  dataTestId?: string;
}

export const YBLabel: FC<YBLabelProps> = (props) => {
  const { className, children, dataTestId } = props;
  const classes = useYBLabelStyles(props);
  return (
    <Box className={clsx(classes.container, className)} data-testid={dataTestId}>
      {children}
    </Box>
  );
};
