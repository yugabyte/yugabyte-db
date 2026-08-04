import React, { FC } from 'react';
import { makeStyles } from '@material-ui/core';
import { useTranslation } from 'react-i18next';

export interface ClusterDBVersionReleaseBadgeProps {
  text: string;
}

const useStyles = makeStyles((theme) => ({
  badge: {
    fontSize: 10,
    fontWeight: 500,
    display: 'flex',
    alignItems: 'center',
    height: theme.spacing(2.5),
    padding: theme.spacing(0.25, 0.75),
    color: theme.palette.grey[900],
    borderRadius: theme.spacing(0.5),
    border: `1px solid ${theme.palette.grey[300]}`,
    backgroundColor: theme.palette.common.white,
    textTransform: 'uppercase'
  }
}));

export const ClusterDBVersionReleaseBadge: FC<ClusterDBVersionReleaseBadgeProps> = ({ text }) => {
  const classes = useStyles();
  const { t } = useTranslation();
  const [, minor] = text.split('.');
  const minorInt = parseInt(minor);
  var releaseType = "";
  if (!isNaN(minorInt)) {
    if (minorInt % 2 == 0) {
        releaseType = t('common.stable');
    } else {
        releaseType = t('common.preview');
    }
  }
  return (
    <>
    {releaseType && (
      <span className={classes.badge}>{releaseType}</span>
    )}
    </>
  );
};
