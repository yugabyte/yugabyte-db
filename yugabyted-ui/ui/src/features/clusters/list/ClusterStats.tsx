import React, { FC, Fragment } from 'react';
import { useTranslation } from 'react-i18next';
import { Typography, makeStyles, Box } from '@material-ui/core';
import type { ClusterData } from '@app/api/src';

const useStyles = makeStyles((theme) => ({
  label: {
    color: theme.palette.grey[600],
    fontWeight: 500,
    marginBottom: theme.spacing(0.75)
  }
}));

export const ClusterStats: FC<{ data: ClusterData[] }> = ({ data }) => {
  const classes = useStyles();
  const { t } = useTranslation();
  let clusterNum = 0;
  let clusterNodes = 0;
  let clusterCores = 0;
  let clusterPaused = 0;
  const clusterAlerts = 0;

  data.forEach((cluster) => {
    if (cluster.spec) {
      const numNodes = cluster.spec.cluster_info.num_nodes;
      clusterNum++;
      clusterNodes += numNodes;
      clusterCores += numNodes * (cluster.spec.cluster_info.node_info.cpu_usage ?? 0);
    }
    if (cluster.info) {
      if (cluster.info.state === 'Paused') {
        clusterPaused++;
      }
    }
  });

  return (
    <Fragment>
      <Box mr={6} width={'120px'} data-testid="clustersAvailable">
        <Typography variant="subtitle2" className={classes.label}>
          {t('clusters.clusters')}
        </Typography>
        <Typography variant="h3">{clusterNum}</Typography>
      </Box>
      <Box mr={6} width={'120px'} data-testid="nodeAvailable">
        <Typography variant="subtitle2" className={classes.label}>
          {t('clusters.nodes')}
        </Typography>
        <Typography variant="h3">{clusterNodes}</Typography>
      </Box>
      <Box mr={6} width={'120px'} data-testid="coreAvailable">
        <Typography variant="subtitle2" className={classes.label}>
          {t('clusters.cores')}
        </Typography>
        <Typography variant="h3">{clusterCores}</Typography>
      </Box>
      {/* TODO: unhide when paused clusters will be supported */}
      <Box mr={6} width={'120px'} hidden>
        <Typography variant="subtitle2" className={classes.label}>
          {t('clusters.pausedClusters')}
        </Typography>
        <Typography variant="h3">{clusterPaused}</Typography>
      </Box>
      <Box mr={'auto'} width={'120px'} hidden>
        <Typography variant="subtitle2" className={classes.label}>
          {t('clusters.alerts')}
        </Typography>
        <Typography variant="h3" color="error">
          {clusterAlerts}
        </Typography>
      </Box>
    </Fragment>
  );
};
