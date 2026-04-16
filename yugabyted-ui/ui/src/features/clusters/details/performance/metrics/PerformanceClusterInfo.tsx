import React, { FC } from 'react';
import { useTranslation } from 'react-i18next';
import { Grid, makeStyles, Paper, Typography } from '@material-ui/core';

import type { ClusterData } from '@app/api/src';
import { convertMBtoGB, getFaultTolerance } from '@app/helpers';

const useStyles = makeStyles((theme) => ({
  clusterInfo: {
    padding: theme.spacing(2),
    border: `1px solid ${theme.palette.grey[200]}`
  },
  container: {
    display: 'grid',
    gridTemplateColumns: '1.5fr 1.5fr 1fr 1fr 1fr 1fr 1fr',
    gridGap: theme.spacing(2)
  },
  label: {
    color: theme.palette.grey[600],
    fontWeight: 500,
    marginBottom: theme.spacing(0.75),
    textTransform: 'uppercase'
  },
  labelVcpu: {
    color: theme.palette.grey[600],
    fontWeight: theme.typography.fontWeightMedium as number,
    marginBottom: theme.spacing(0.75)
  },
  value: {
    paddingTop: theme.spacing(0.8)
  },
  regionBox: {
    padding: theme.spacing(0.7, 1, 0.7, 1),
    marginTop: theme.spacing(-1),
    maxWidth: 186,
    borderRadius: theme.shape.borderRadius,
    border: `1px solid ${theme.palette.grey[200]}`
  }
}));

interface ClusterInfoProps {
  cluster?: ClusterData;
  region?: string;
}

export const PerformanceClusterInfo: FC<ClusterInfoProps> = ({ cluster, region }) => {
  const classes = useStyles();
  const { t } = useTranslation();

  const clusterSpec = cluster?.spec;
  const regionInfo = clusterSpec?.cluster_region_info?.filter(
    (regInfo) => regInfo?.placement_info?.cloud_info?.region === region
  )[0];
  const numNodes = region ? regionInfo?.placement_info?.num_nodes ?? 0 : clusterSpec?.cluster_info?.num_nodes ?? 0;
  const totalDiskSize = clusterSpec?.cluster_info?.node_info.disk_size_gb ?? 0;
  const averageCpuUsage = clusterSpec?.cluster_info?.node_info.cpu_usage ?? 0;
  const totalRamUsageMb = clusterSpec?.cluster_info?.node_info?.memory_mb ?? 0;

  const getRamText = (value: number | undefined) => {
    return value ? t('units.GB', { value: convertMBtoGB(value, true) }) : '';
  };

  return (
    <Paper className={classes.clusterInfo}>
      <Grid container className={classes.container}>
        <div>
          <Typography variant="subtitle2" className={classes.label}>
            {region ? t('clusterDetail.performance.metrics.region') : t('clusterDetail.performance.metrics.regions')}
          </Typography>
          <Typography variant="body2" className={classes.value}>
            {region ? (
              region
            ) : (
              numNodes
            )}
          </Typography>
        </div>
        <div>
          <Typography variant="subtitle2" className={classes.label}>
            {t('clusters.faultTolerance')}
          </Typography>
          <Typography variant="body2" className={classes.value}>
            {getFaultTolerance(clusterSpec?.cluster_info?.fault_tolerance, t)}
          </Typography>
        </div>
        <div>
          <Typography variant="subtitle2" className={classes.label}>
            {t('clusterDetail.overview.totalNodes')}
          </Typography>
          <Typography variant="body2" className={classes.value}>
            {numNodes}
          </Typography>
        </div>
        <div>
          <Typography variant="subtitle2" className={classes.labelVcpu}>
            {t('clusterDetail.overview.totalVcpu')}
          </Typography>
          <Typography variant="body2" className={classes.value}>
            {averageCpuUsage}
          </Typography>
        </div>
        <div>
          <Typography variant="subtitle2" className={classes.label}>
            {t('clusterDetail.totalRam')}
          </Typography>
          <Typography variant="body2" className={classes.value}>
            {getRamText(totalRamUsageMb)}
          </Typography>
        </div>
        <div>
          <Typography variant="subtitle2" className={classes.label}>
            {t('clusterDetail.totalDiskSize')}
          </Typography>
          <Typography variant="body2" className={classes.value}>
            {t('units.GB', { value: totalDiskSize })}
          </Typography>
        </div>
      </Grid>
    </Paper>
  );
};
