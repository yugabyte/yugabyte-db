import React, { FC } from 'react';
import { useTranslation } from 'react-i18next';
import { Box, Divider, Grid, Link, makeStyles, Paper, Typography } from '@material-ui/core';
import type { ClusterData } from '@app/api/src';
import { Link as RouterLink } from 'react-router-dom';
import { roundDecimal, getFaultTolerance } from '@app/helpers';
import { STATUS_TYPES, YBStatus } from '@app/components';

const useStyles = makeStyles((theme) => ({
  clusterInfo: {
    padding: theme.spacing(2),
    border: `1px solid ${theme.palette.grey[200]}`,
    width: '100%'
  },
  dividerHorizontal: {
    width: '100%',
    marginTop: theme.spacing(2.5),
    marginBottom: theme.spacing(2.5),
  },
  dividerVertical: {
    marginLeft: theme.spacing(2.5),
    marginRight: theme.spacing(2.5),
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
  },
  link: {
    '&:link, &:focus, &:active, &:visited, &:hover': {
      textDecoration: 'none',
      color: theme.palette.text.primary
    }
  }
}));

interface ClusterInfoWidgetProps {
  cluster: ClusterData;
}

export const ClusterInfoWidget: FC<ClusterInfoWidgetProps> = ({ cluster }) => {
  const classes = useStyles();
  const { t } = useTranslation();
  // const context = useContext(ClusterContext);

  const clusterSpec = cluster?.spec;
  const replicationFactor = clusterSpec?.cluster_info?.replication_factor ?? 0;
  const databaseVersion = cluster.info.software_version ?? '';
  const totalDiskSize = clusterSpec.cluster_info.node_info.disk_size_gb ?? 0;
  const totalCores = clusterSpec?.cluster_info?.node_info.num_cores ?? 0;
  const totalRamProvisionedGb = clusterSpec?.cluster_info?.node_info.ram_provisioned_gb ?? 0;

  // Convert ram from MB to GB
  // const getTotalRamText = (value: number, numberOfNodes: number) => {
  //   const ramGbPerNode = convertMBtoGB(value, true);
  //   const totalRam = ramGbPerNode * numberOfNodes;
  //   return value ? t('units.GB', { value: totalRam }) : '';
  // };

  // Get text for ram usage
  const getRamUsageText = (ramUsageGb: number) => {
    ramUsageGb = roundDecimal(ramUsageGb)
    return t('units.GB', { value: ramUsageGb });
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

  const authentication = encryptionAtRest || encryptionInTransit ?
    t('clusters.password') : t('clusters.none');

  return (
    <Paper className={classes.clusterInfo}>
      <Link className={classes.link} component={RouterLink} to="?tab=tabSettings">
        <Box display="flex">
          <Box flexGrow={3}>
            <Grid container spacing={4}>
              <Grid item xs={4}>
                <Typography variant="subtitle2" className={classes.label}>
                  {t('clusters.faultTolerance')}
                </Typography>
                <Typography variant="body2" className={classes.value}>
                  {getFaultTolerance(clusterSpec?.cluster_info?.fault_tolerance, t)}
                </Typography>
              </Grid>
              <Grid item xs={4}>
                <Typography variant="subtitle2" className={classes.label}>
                  {t('clusterDetail.overview.replicationFactor')}
                </Typography>
                <Typography variant="body2" className={classes.value}>
                  {replicationFactor}
                </Typography>
              </Grid>
              <Grid item xs={4}>
                <Typography variant="subtitle2" className={classes.label}>
                  {t('clusterDetail.overview.databaseVersion')}
                </Typography>
                <Typography variant="body2" className={classes.value}>
                  {`v${databaseVersion}`}
                </Typography>
              </Grid>
            </Grid>
            <Divider orientation="horizontal" className={classes.dividerHorizontal} />
            <Grid container spacing={4}>
              <Grid item xs={4}>
                <Typography variant="subtitle2" className={classes.label}>
                  {t('clusterDetail.overview.totalvCPU')}
                </Typography>
                <Typography variant="body2" className={classes.value}>
                  {totalCores}
                </Typography>
              </Grid>
              <Grid item xs={4}>
                <Typography variant="subtitle2" className={classes.label}>
                  {t('clusterDetail.overview.totalMemory')}
                </Typography>
                <Typography variant="body2" className={classes.value}>
                  {getRamUsageText(totalRamProvisionedGb)}
                </Typography>
              </Grid>
              <Grid item xs={4}>
                <Typography variant="subtitle2" className={classes.label}>
                  {t('clusterDetail.overview.totalDiskSize')}
                </Typography>
                <Typography variant="body2" className={classes.value}>
                  {getDiskSizeText(totalDiskSize)}
                </Typography>
              </Grid>
            </Grid>
          </Box>
          <Divider orientation="vertical" className={classes.dividerVertical} flexItem />
          <Box flexGrow={1}>
            <Grid container spacing={4}>
              <Grid item xs={12}>
                <Typography variant="subtitle2" className={classes.label}>
                  {t('clusters.encryption')}
                </Typography>
                <Box display="flex">
                  {!encryptionAtRest && !encryptionInTransit &&
                    <YBStatus type={STATUS_TYPES.WARNING} />
                  }
                  <Typography variant="body2" className={classes.value}>
                    {encryption}
                  </Typography>
                </Box>
              </Grid>
            </Grid>
            <Box className={classes.dividerHorizontal} />
            <Grid container spacing={4}>
              <Grid item xs={12}>
                <Typography variant="subtitle2" className={classes.label}>
                  {t('clusterDetail.overview.authentication')}
                </Typography>
                <Box display="flex">
                  {!encryptionAtRest && !encryptionInTransit &&
                    <YBStatus type={STATUS_TYPES.WARNING} />
                  }
                  <Typography variant="body2" className={classes.value}>
                    {authentication}
                  </Typography>
                </Box>
              </Grid>
            </Grid>
          </Box>
        </Box>
      </Link>
    </Paper>
  );
};
