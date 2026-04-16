import React, { FC } from 'react';
import { makeStyles, Typography } from '@material-ui/core';
import { useTranslation } from 'react-i18next';
import { themeVariables } from '@app/theme/variables';

const useStyles = makeStyles((theme) => ({
  root: {
    position: 'absolute',
    bottom: 0,
    left: theme.spacing(2),
    right: theme.spacing(2),
    height: themeVariables.footerHeight,
    borderTop: `1px solid ${theme.palette.grey[200]}`,
    display: 'grid',
    placeContent: 'center end'
  }
}));

export const Footer: FC = () => {
  const classes = useStyles();
  const { t } = useTranslation();

  return (
    <footer className={classes.root}>
      <Typography variant="subtitle1" color="textSecondary">
        {t('copyright', { year: new Date().getFullYear() })}
      </Typography>
    </footer>
  );
};
