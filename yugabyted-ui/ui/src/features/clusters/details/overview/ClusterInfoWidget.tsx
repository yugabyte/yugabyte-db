import React, { FC } from 'react';
import { useTranslation } from 'react-i18next';
import { Box, Divider, Grid, makeStyles, Paper, Typography } from '@material-ui/core';
// import { intlFormat } from 'date-fns';

// Local imports
import type { ClusterData } from '@app/api/src';
import {
  roundDecimal,
  getFaultTolerance,
} from '@app/helpers';
import { STATUS_TYPES, YBStatus } from '@app/components';

const useStyles = makeStyles((theme) => ({
  clusterInfo: {
    padding: theme.spacing(2),
    border: `1px solid ${theme.palette.grey[200]}`,
    width: '100%'
  },
  divider: {
    width: '100%',
    marginLeft: 0,
    marginTop: theme.spacing(2.5),
    marginBottom: theme.spacing(2.5),
  },
  label: {
    color: theme.palette.grey[600],
    fontWeight: theme.typography.fontWeightMedium as number,
    marginBottom: theme.spacing(0.75),
    textTransform: 'uppercase',
    textAlign: 'start'
  },
  value: {
    paddingTop: theme.spacing(0.36),
    textAlign: 'start'
  }
}));

interface ClusterInfoWidgetProps {
  cluster: ClusterData;
}

/* const getDate = (rawDate?: string): string => {
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
}; */

export const ClusterInfoWidget: FC<ClusterInfoWidgetProps> = ({ cluster }) => {
  const classes = useStyles();
  const { t } = useTranslation();
  // const context = useContext(ClusterContext);

  const clusterSpec = cluster?.spec;
  const numNodes = clusterSpec?.cluster_info?.num_nodes ?? 0;
  const databaseVersion = cluster.info.software_version ?? '';
  const totalRamUsageMb = clusterSpec.cluster_info.node_info.memory_mb ?? 0; // TODO: Use total memory instead of used memory
  const totalDiskSize = clusterSpec.cluster_info.node_info.disk_size_gb ?? 0;
  const totalCores = clusterSpec?.cluster_info?.node_info.num_cores ?? 0;
  const authentication = "Unknown" // TODO: Get authentication value from the API

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

  // Get text for disk usage
  const getDiskSizeText = (diskSizeGb: number) => {
    diskSizeGb = roundDecimal(diskSizeGb)
    return t('units.GB', { value: diskSizeGb });
  }

  // Get text for encryption
  const encryptionAtRest = clusterSpec?.encryption_info?.encryption_at_rest ?? false;
  const encryptionInTransit = clusterSpec?.encryption_info?.encryption_in_transit ?? false;
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
  const encryption = getEncryptionText(encryptionAtRest, encryptionInTransit);

  return (
    <Paper className={classes.clusterInfo}>
      <Grid container spacing={4}>
        <Grid item xs={3}>
          <Typography variant="subtitle2" className={classes.label}>
            {t('clusters.faultTolerance')}
          </Typography>
          <Typography variant="body2" className={classes.value}>
            {getFaultTolerance(clusterSpec?.cluster_info?.fault_tolerance, t)}
          </Typography>
        </Grid>
        <Grid item xs={3}>
          <Typography variant="subtitle2" className={classes.label}>
            {t('clusterDetail.overview.replicationFactor')}
          </Typography>
          <Typography variant="body2" className={classes.value}>
            {numNodes}
          </Typography>
        </Grid>
        <Grid item xs={3}>
          <Typography variant="subtitle2" className={classes.label}>
            {t('clusterDetail.overview.databaseVersion')}
          </Typography>
          <Typography variant="body2" className={classes.value}>
            {`v${databaseVersion}`}
          </Typography>
        </Grid>
        <Grid item xs={3}>
          <Typography variant="subtitle2" className={classes.label}>
            {t('clusters.encryption')}
          </Typography>
          <Box display="flex">
            {!encryptionAtRest && !encryptionInTransit &&
              <YBStatus type={STATUS_TYPES.WARNING}/>
            }
            <Typography variant="body2" className={classes.value}>
              {encryption}
            </Typography>
          </Box>
        </Grid>
      </Grid>
      <Divider orientation="horizontal" variant="middle" className={classes.divider} />
      <Grid container spacing={4}>
        <Grid item xs={3}>
          <Typography variant="subtitle2" className={classes.label}>
            {t('clusterDetail.overview.totalvCPU')}
          </Typography>
          <Typography variant="body2" className={classes.value}>
            {totalCores}
          </Typography>
        </Grid>
        <Grid item xs={3}>
          <Typography variant="subtitle2" className={classes.label}>
            {t('clusterDetail.overview.totalMemory')}
          </Typography>
          <Typography variant="body2" className={classes.value}>
            {getRamUsageText(totalRamUsageMb)}
          </Typography>
        </Grid>
        <Grid item xs={3}>
          <Typography variant="subtitle2" className={classes.label}>
            {t('clusterDetail.overview.totalDiskSize')}
          </Typography>
          <Typography variant="body2" className={classes.value}>
          {getDiskSizeText(totalDiskSize)}
          </Typography>
        </Grid>
        <Grid item xs={3}>
          <Typography variant="subtitle2" className={classes.label}>
            {t('clusterDetail.overview.authentication')}
          </Typography>
          <Box display="flex">
            {authentication === "Unknown" &&
              <YBStatus type={STATUS_TYPES.WARNING}/>
            }
            <Typography variant="body2" className={classes.value}>
              {authentication}
            </Typography>
          </Box>
        </Grid>
      </Grid>
    </Paper>
  );
};
