import React, { FC } from 'react';
import { Grid, makeStyles, Paper } from '@material-ui/core';

import type { ClusterData } from '@app/api/src';
import { DiskUsageGraph } from './DiskUsageGraph';


const useStyles = makeStyles((theme) => ({
  clusterInfo: {
    padding: theme.spacing(2),
    flexGrow: 1,
    flexBasis: 0,
    border: `1px solid ${theme.palette.grey[200]}`
  },
  container: {
    justifyContent: 'space-between'
  },
  label: {
    color: theme.palette.grey[600],
    fontWeight: theme.typography.fontWeightMedium as number,
    marginBottom: theme.spacing(0.75),
    textTransform: 'uppercase'
  },
  value: {
    paddingTop: theme.spacing(0.57)
  }
}));

interface ClusterDiskWidgetProps {
  cluster: ClusterData;
}

export const ClusterDiskWidget: FC<ClusterDiskWidgetProps> = ({ cluster }) => {
  const classes = useStyles();

  const clusterSpec = cluster?.spec;
  const totalDiskSize = clusterSpec?.cluster_info?.node_info.disk_size_gb ?? 0;
  const usedDiskSize = clusterSpec?.cluster_info?.node_info.disk_size_used_gb ?? 0;

  return (
    <Paper className={classes.clusterInfo}>
      <Grid container className={classes.container}>
        <DiskUsageGraph totalDiskSize={totalDiskSize} usedDiskSize={usedDiskSize}/>
      </Grid>
    </Paper>
  );
};
