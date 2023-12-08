import React, { FC } from 'react';
import { useTranslation } from 'react-i18next';
import { Box, Divider, Grid, makeStyles, Paper, Typography } from '@material-ui/core';
import { useGetClusterQuery } from '@app/api/src';
import { roundDecimal, getFaultTolerance } from '@app/helpers';
import { STATUS_TYPES, YBStatus } from '@app/components';
import { intlFormat } from 'date-fns';

const useStyles = makeStyles((theme) => ({
  paperContainer: {
    padding: theme.spacing(3),
    paddingBottom: theme.spacing(4),
    border: `1px solid ${theme.palette.grey[200]}`,
    width: '100%'
  },
  heading: {
    marginBottom: theme.spacing(5),
  },
  dividerHorizontal: {
    width: '100%',
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

interface GeneralOverviewProps {
}

export const GeneralOverview: FC<GeneralOverviewProps> = () => {
  const classes = useStyles();
  const { t } = useTranslation();

  const { data: clusterData } = useGetClusterQuery();
  const cluster = clusterData?.data;

  const clusterSpec = cluster?.spec;
  const clusterName = clusterSpec?.name ?? '';
  const clusterCreationDate = cluster?.info.metadata.created_on;
  const numNodes = clusterSpec?.cluster_info?.num_nodes ?? 0;
  const replicationFactor = clusterSpec?.cluster_info?.replication_factor ?? 0;
  const databaseVersion = cluster?.info.software_version ?? '';
  const totalDiskSize = clusterSpec?.cluster_info.node_info.disk_size_gb ?? 0;
  const totalCores = clusterSpec?.cluster_info?.node_info.num_cores ?? 0;
  const totalRamProvisionedGb = clusterSpec?.cluster_info?.node_info.ram_provisioned_gb ?? 0;

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
    <Paper className={classes.paperContainer}>
      <Typography variant="h4" className={classes.heading}>
        {t('clusterDetail.settings.general')}
      </Typography>
      <Grid container spacing={4}>
        <Grid item xs={2}>
          <Typography variant="subtitle2" className={classes.label}>
            {t('clusterWizard.clusterName')}
          </Typography>
          <Typography variant="body2" className={classes.value}>
            {clusterName}
          </Typography>
        </Grid>
        <Grid item xs={2}>
          <Typography variant="subtitle2" className={classes.label}>
            {t('clusterDetail.overview.databaseVersion')}
          </Typography>
          <Typography variant="body2" className={classes.value}>
            {`v${databaseVersion}`}
          </Typography>
        </Grid>
        <Grid item xs={2}>
          <Typography variant="subtitle2" className={classes.label}>
            {t('clusters.dateCreated')}
          </Typography>
          <Typography variant="body2" className={classes.value}>
            {getDate(clusterCreationDate)}
          </Typography>
        </Grid>
        <Grid item xs={2}></Grid>
        <Grid item xs={2}>
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
        <Grid item xs={2}>
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
      <Divider orientation="horizontal" className={classes.dividerHorizontal} />
      <Grid container spacing={4}>
        <Grid item xs={2}>
          <Typography variant="subtitle2" className={classes.label}>
            {t('clusters.faultTolerance')}
          </Typography>
          <Typography variant="body2" className={classes.value}>
            {getFaultTolerance(clusterSpec?.cluster_info?.fault_tolerance, t)}
          </Typography>
        </Grid>
        <Grid item xs={2}>
          <Typography variant="subtitle2" className={classes.label}>
            {t('clusterDetail.overview.replicationFactor')}
          </Typography>
          <Typography variant="body2" className={classes.value}>
            {replicationFactor}
          </Typography>
        </Grid>
        <Grid item xs={2}>
          <Typography variant="subtitle2" className={classes.label}>
            {t('clusterDetail.overview.totalNodes')}
          </Typography>
          <Typography variant="body2" className={classes.value}>
            {numNodes}
          </Typography>
        </Grid>
        <Grid item xs={2}>
          <Typography variant="subtitle2" className={classes.label}>
            {t('clusterDetail.overview.totalvCPU')}
          </Typography>
          <Typography variant="body2" className={classes.value}>
            {totalCores}
          </Typography>
        </Grid>
        <Grid item xs={2}>
          <Typography variant="subtitle2" className={classes.label}>
            {t('clusterDetail.overview.totalMemory')}
          </Typography>
          <Typography variant="body2" className={classes.value}>
            {getRamUsageText(totalRamProvisionedGb)}
          </Typography>
        </Grid>
        <Grid item xs={2}>
          <Typography variant="subtitle2" className={classes.label}>
            {t('clusterDetail.overview.totalDiskSize')}
          </Typography>
          <Typography variant="body2" className={classes.value}>
            {getDiskSizeText(totalDiskSize)}
          </Typography>
        </Grid>
      </Grid>
    </Paper>
  );
};
