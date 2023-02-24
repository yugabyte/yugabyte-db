import React, { FC } from 'react';
import { Divider, makeStyles, Paper } from '@material-ui/core';

// Local imports
import type { ClusterData, HealthCheckInfo } from '@app/api/src';
import { ClusterNodeWidget } from './ClusterNodeWidget';
import { ClusterDiskWidget } from './ClusterDiskWidget';
import { ClusterActivityWidget } from './ClusterActivityWidget';

const useStyles = makeStyles((theme) => ({
  container: {
    display: "flex",
    gap: theme.spacing(2),
    padding: theme.spacing(2),
    flexGrow: 1,
    flexBasis: 0,
    border: `1px solid ${theme.palette.grey[200]}`
  },
}));

interface ClusterStatusWidgetProps {
  cluster: ClusterData;
  health: HealthCheckInfo;
}

export const ClusterStatusWidget: FC<ClusterStatusWidgetProps> = ({ cluster, health }) => {
  const classes = useStyles();
  return (
      <Paper className={classes.container}>
        <ClusterNodeWidget health={health} />
        <Divider orientation="vertical" />
        <ClusterActivityWidget />
        <Divider orientation="vertical" />
        <ClusterDiskWidget cluster={cluster} />
      </Paper>
  );
};
