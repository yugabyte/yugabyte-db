import React, { FC } from 'react';
import { Grid, makeStyles } from '@material-ui/core';

// Local imports
import type { ClusterData, HealthCheckInfo } from '@app/api/src';
import { ClusterNodeWidget } from './ClusterNodeWidget';
import { ClusterTabletWidget } from './ClusterTabletWidget';
import { ClusterDiskWidget } from './ClusterDiskWidget';

const useStyles = makeStyles((theme) => ({
  container: {
    justifyContent: 'space-between',
    columnGap: theme.spacing(2)
  }
}));

interface ClusterStatusWidgetProps {
  cluster: ClusterData;
  health: HealthCheckInfo;
}

export const ClusterStatusWidget: FC<ClusterStatusWidgetProps> = ({ cluster, health }) => {
  const classes = useStyles();
  return (
      <Grid container className={classes.container}>
        <ClusterNodeWidget cluster={cluster} health={health}/>
        <ClusterTabletWidget health={health}/>
        <ClusterDiskWidget cluster={cluster}/>
      </Grid>
  );
};
