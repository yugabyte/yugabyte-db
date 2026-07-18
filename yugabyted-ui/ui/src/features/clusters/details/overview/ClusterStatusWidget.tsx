import React, { FC } from 'react';
import { Box, Divider, makeStyles, Paper } from '@material-ui/core';

// Local imports
import type { ClusterData, HealthCheckInfo } from '@app/api/src';
import { ClusterNodeWidget } from './ClusterNodeWidget';
import { ClusterResourceWidget } from './ClusterResourceWidget';
import { ClusterActivityWidget } from './ClusterActivityWidget';

const useStyles = makeStyles((theme) => ({
  container: {
    display: "flex",
    gap: theme.spacing(2),
    padding: theme.spacing(2),
    flexGrow: 1,
    overflow: 'auto',
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
        <Box flex={2}>
          <ClusterNodeWidget health={health} />
        </Box>
        <Divider orientation="vertical" />
        <Box flex={1}>
          <ClusterActivityWidget />
        </Box>
        <Divider orientation="vertical" />
        <Box flex={2}>
          <ClusterResourceWidget cluster={cluster} />
        </Box>
      </Paper>
  );
};
