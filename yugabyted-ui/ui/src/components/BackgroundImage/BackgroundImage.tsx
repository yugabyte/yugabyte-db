import React, { FC } from 'react';
import Background from '@app/assets/bg.png';
import { Container, makeStyles } from '@material-ui/core';

const useStyles = makeStyles(() => ({
  root: {
    height: '100vh',
    width: '100vw',
    maxWidth: '100vw',
    background: `url(${Background}) repeat-x`,
    backgroundSize: 'contain',
    overflowY: 'auto'
  }
}));

export const BackgroundImage: FC = ({ children }) => {
  const classes = useStyles();

  return <Container className={classes.root}>{children ?? <span />}</Container>;
};
