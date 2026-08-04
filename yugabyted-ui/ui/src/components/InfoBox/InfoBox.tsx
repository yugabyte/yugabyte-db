import React, { FC } from 'react';
import clsx from 'clsx';
import { Link as MUILink, Box, makeStyles, Typography } from '@material-ui/core';
import { Link } from 'react-router-dom';

const useStyles = makeStyles((theme) => ({
  root: {
    color: theme.palette.grey[600],
    backgroundColor: theme.palette.background.paper,
    borderRadius: theme.shape.borderRadius,
    border: `1px solid ${theme.palette.grey[200]}`,
    padding: theme.spacing(2),
    display: 'flex',
    alignItems: 'center',
    width: 'fit-content'
  },
  fullWidth: {
    width: '90%'
  },
  dence: {
    padding: theme.spacing(1.2, 1.5)
  }
}));

interface InfoBoxProps {
  message: string;
  fullWidth?: boolean;
  dense?: boolean;
  url?: string;
  target?: string;
  linkText?: string;
}

export const InfoBox: FC<InfoBoxProps> = ({ message, fullWidth, dense, url, target = '_self', linkText }) => {
  const classes = useStyles();

  return (
    <div className={clsx(classes.root, fullWidth && classes.fullWidth, dense && classes.dence)}>
      <Box ml={dense ? 0.5 : 2}>
        <Typography variant="body2" component="span">
          {message}
        </Typography>{' '}
        {url && (
          <>
            {target === '_self' && (
              <MUILink variant="body2" noWrap component={Link} to={url}>
                {linkText}
              </MUILink>
            )}
            {target === '_blank' && (
              <MUILink variant="body2" noWrap href={url} target={target}>
                {linkText}
              </MUILink>
            )}
          </>
        )}
      </Box>
    </div>
  );
};
