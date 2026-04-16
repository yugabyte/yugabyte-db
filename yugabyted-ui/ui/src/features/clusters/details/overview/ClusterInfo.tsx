import React, { FC } from 'react';
import { useTranslation } from 'react-i18next';
import { Grid, makeStyles, Paper, Typography } from '@material-ui/core';
import { intlFormat } from 'date-fns';

import type { ClusterData } from '@app/api/src';
// import { getCloudProviderIcon } from '@app/features/clusters/list/ClusterCard';
import { convertMBtoGB, getFaultTolerance } from '@app/helpers';

const useStyles = makeStyles((theme) => ({
  clusterInfo: {
    display: 'flex',
    padding: theme.spacing(2),
    border: `1px solid ${theme.palette.grey[200]}`
  },
  container: {
    justifyContent: 'space-between'
  },
  label: {
    color: theme.palette.grey[600],
    fontWeight: 500,
    marginBottom: theme.spacing(0.75),
    textTransform: 'uppercase'
  },
  value: {
    paddingTop: '4.5px' // Because of the SVG image pushing text field down
  },
  region: {
    display: 'flex',
    alignItems: 'center',

    '& > svg': {
      marginRight: theme.spacing(1)
    }
  }
}));

interface ClusterInfoProps {
  cluster: ClusterData;
}

const getDate = (rawDate?: string): string => {
  if (rawDate) {
    return intlFormat(new Date(rawDate), {
      year: 'numeric',
      month: 'numeric',
      day: 'numeric',
      hour: 'numeric',
      minute: 'numeric',
      // @ts-ignore: Parameter is not yet supported by `date-fns` but
      // is supported by underlying Intl.DateTimeFormat. CLOUDGA-5283
      hourCycle: 'h23'
    });
  }
  return '-';
};

export const ClusterInfo: FC<ClusterInfoProps> = ({ cluster }) => {
  const classes = useStyles();
  const { t } = useTranslation();

  const getRamText = (value: number | undefined) => {
    return value ? t('units.GB', { value: convertMBtoGB(value, true) }) : '';
  };

  const clusterSpec = cluster?.spec;
  const numNodes = clusterSpec?.cluster_info?.num_nodes ?? 0;
  const totalDiskSize = clusterSpec?.cluster_info?.node_info.disk_size_gb ?? 0;
  const averageCpuUsage = clusterSpec?.cluster_info?.node_info.cpu_usage ?? 0;
  return (
    <Paper className={classes.clusterInfo}>
      <Grid container className={classes.container}>
        <Grid item xs={3}>
          <Typography variant="subtitle2" className={classes.label}>
            {t('clusters.infrastructure')}
          </Typography>
          <Typography variant="body2" className={classes.region}>
            {/* {getCloudProviderIcon(clusterSpec?.cloud_info?.code)} */}
            {t('clusterDetail.infrastructureBrief', {
              region: clusterSpec?.cloud_info?.region ?? '',
              nodes: numNodes,
              cpu: averageCpuUsage
            })}
          </Typography>
        </Grid>
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
            {t('clusterDetail.totalRam')}
          </Typography>
          <Typography variant="body2" className={classes.value}>
            {getRamText(clusterSpec?.cluster_info?.node_info.memory_mb)}
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
        <div>
          <Typography variant="subtitle2" className={classes.label}>
            {t('clusters.encryption')}
          </Typography>
          <Typography variant="body2" className={classes.value}>
            {t('clusters.inTransitAtRest')}
          </Typography>
        </div>
        <div>
          <Typography variant="subtitle2" className={classes.label}>
            {t('clusters.dateCreated')}
          </Typography>
          <Typography variant="body2" className={classes.value}>
            {getDate(cluster?.info.metadata?.created_on)}
          </Typography>
        </div>
      </Grid>
    </Paper>
  );
};
