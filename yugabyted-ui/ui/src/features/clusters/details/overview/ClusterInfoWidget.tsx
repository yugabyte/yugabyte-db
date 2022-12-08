import React, { FC } from 'react';
import { useTranslation } from 'react-i18next';
import { Grid, makeStyles, Paper, Typography } from '@material-ui/core';
import { intlFormat } from 'date-fns';

// Local imports
import type { ClusterData } from '@app/api/src';
import {
  roundDecimal,
  getFaultTolerance,
} from '@app/helpers';

const useStyles = makeStyles((theme) => ({
  clusterInfo: {
    padding: theme.spacing(2),
    border: `1px solid ${theme.palette.grey[200]}`,
    width: '100%'
  },
  container: {
    justifyContent: 'space-between'
  },
  label: {
    color: theme.palette.grey[600],
    fontWeight: theme.typography.fontWeightMedium as number,
    marginBottom: theme.spacing(0.75),
    textTransform: 'uppercase',
    textAlign: 'center'
  },
  value: {
    paddingTop: theme.spacing(0.57),
    textAlign: 'center'
  }
}));

interface ClusterInfoWidgetProps {
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

export const ClusterInfoWidget: FC<ClusterInfoWidgetProps> = ({ cluster }) => {
  const classes = useStyles();
  const { t } = useTranslation();
  // const context = useContext(ClusterContext);

  const clusterSpec = cluster?.spec;
  const numNodes = clusterSpec?.cluster_info?.num_nodes ?? 0;
  const totalRamUsageMb = clusterSpec?.cluster_info?.node_info.memory_mb ?? 0;
  const totalCores = clusterSpec?.cluster_info?.node_info.num_cores ?? 0;
  // const averageCpuUsage = clusterSpec?.cluster_info?.node_info.cpu_usage ?? 0;

  // Convert ram from MB to GB
  // const getTotalRamText = (value: number, numberOfNodes: number) => {
  //   const ramGbPerNode = convertMBtoGB(value, true);
  //   const totalRam = ramGbPerNode * numberOfNodes;
  //   return value ? t('units.GB', { value: totalRam }) : '';
  // };

  // Get text for ram usage
  const getRamUsageText = (ramUsageMb: number) => {
    ramUsageMb = roundDecimal(ramUsageMb)
    return t('units.MB', { value: ramUsageMb });
  }

  // Get text for encryption
  const getEncryptionText = (encryptionAtRest: boolean, encryptionInTransit: boolean) => {
    if (encryptionAtRest && encryptionInTransit) {
      return t('clusters.inTransitAtRest');
    }
    if (encryptionAtRest) {
      return t('clusters.atRest')
    }
    if (encryptionInTransit) {
      return t('clusters.inTransit')
    }
    return t('clusters.none')
  }

  return (
    <Paper className={classes.clusterInfo}>
      <Grid container className={classes.container}>
        <div>
          <Typography variant="subtitle2" className={classes.label}>
            {t('clusterDetail.overview.replicationFactor')}
          </Typography>
          <Typography variant="body2" className={classes.value}>
            {numNodes}
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
            {t('clusters.encryption')}
          </Typography>
          <Typography variant="body2" className={classes.value}>
            {getEncryptionText(clusterSpec?.encryption_info?.encryption_at_rest ?? false,
              clusterSpec?.encryption_info?.encryption_in_transit ?? false)}
          </Typography>
        </div>
        <div>
          <Typography variant="subtitle2" className={classes.label}>
            {t('clusterDetail.overview.ramUsed')}
          </Typography>
          <Typography variant="body2" className={classes.value}>
            {getRamUsageText(totalRamUsageMb)}
          </Typography>
        </div>
        {/*<div>
          <Typography variant="subtitle2" className={classes.label}>
            {t('clusterDetail.overview.totalRam')}
          </Typography>
          <Typography variant="body2" className={classes.value}>
            {getRamUsageText(totalRamUsageMb)}
          </Typography>
        </div>*/}
        <div>
          <Typography variant="subtitle2" className={classes.label}>
            {t('clusterDetail.overview.totalCores')}
          </Typography>
          <Typography variant="body2" className={classes.value}>
            {totalCores}
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
