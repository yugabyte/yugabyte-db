import React, { FC } from 'react';
import { makeStyles } from '@material-ui/core';
import { YBTooltip } from '@app/components';

export interface ClusterDBVersionBadgeProps {
  text: string;
  tooltipMsg?: string;
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
    cursor: 'pointer'
  }
}));

export const getHumanVersion = (version: string): string => {
  const [major, minor, fix] = version.split('.');
  return [major, minor, fix].join('.');
};

export const ClusterDBVersionBadge: FC<ClusterDBVersionBadgeProps> = ({ text, tooltipMsg = '' }) => {
  const classes = useStyles();
  return (
    <YBTooltip title={tooltipMsg}>
      <span className={classes.badge}>{getHumanVersion(text)}</span>
    </YBTooltip>
  );
};
