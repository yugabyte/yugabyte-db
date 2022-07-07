import React, { FC } from 'react';
import { useTranslation } from 'react-i18next';
import { Box, Grid, makeStyles, Paper, Typography } from '@material-ui/core';
import { intlFormat } from 'date-fns';
// import clsx from 'clsx';

// Local imports
import { ClusterData, ClusterRegionInfo, useGetRegionsQuery } from '@app/api/src';
// import { getCloudProviderIcon } from '@app/features/clusters/list/ClusterCard';
import {
  roundDecimal,
  getFaultTolerance,
  // OPEN_EDIT_INFRASTRUCTURE_MODAL
} from '@app/helpers';
// import { YBButton } from '@app/components';
// import { ClusterContext } from '@app/features/clusters/details/ClusterDetails';

// Icons
// import PlusIcon from '@app/assets/plus_icon.svg';

const useStyles = makeStyles((theme) => ({
  clusterInfo: {
    padding: theme.spacing(2),
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
  },
  region: {
    display: 'flex',
    alignItems: 'center',

    '& > svg': {
      marginRight: theme.spacing(1)
    }
  },
  chip: {
    border: `1px solid ${theme.palette.grey[200]}`,
    padding: theme.spacing(0.8, 1.5),
    borderRadius: theme.spacing(0.8),
    height: 'auto',
    fontSize: 11.5
  },
  btnAddRegion: {
    padding: theme.spacing(1),
    color: theme.palette.grey[900]
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
  const cloud = clusterSpec?.cloud_info?.code;
  const { data: regions } = useGetRegionsQuery({ cloud });

  const numNodes = clusterSpec?.cluster_info?.num_nodes ?? 0;
  const totalDiskSize = clusterSpec?.cluster_info?.node_info.disk_size_gb ?? 0;
  const totalRamUsageMb = clusterSpec?.cluster_info?.node_info.memory_mb ?? 0;
  const averageCpuUsage = clusterSpec?.cluster_info?.node_info.cpu_usage ?? 0;

  const availableRegions: ClusterRegionInfo[] = clusterSpec?.cluster_region_info ?? [];

  // const editingDisabled =
  //   isClusterEditingDisabled(cluster) || clusterSpec?.cluster_info?.cluster_tier === ClusterTier.Free;

  // const editingDisabled = true;

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
      t('clusters.inTransit')
    }
    return t('clusters.none')
  }

  // Open edit infra
  // const openEditInfraModal = () => {
  //   if (context?.dispatch) {
  //     context.dispatch({ type: OPEN_EDIT_INFRASTRUCTURE_MODAL });
  //   }
  // };

  return (
    <Paper className={classes.clusterInfo}>
      <Grid container className={classes.container}>
        <div>
          <Typography variant="subtitle2" className={classes.label}>
            {t('clusterDetail.overview.provider')}
          </Typography>
          <Typography variant="body2" className={classes.region}>
            {/* {getCloudProviderIcon(clusterSpec?.cloud_info?.code)} */}
            {clusterSpec?.cloud_info?.code}
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
          <Typography variant="subtitle2" className={classes.label}>
            {t('clusterDetail.overview.averageCpu')}
          </Typography>
          <Typography variant="body2" className={classes.value}>
            {t('units.percent', { value : roundDecimal(averageCpuUsage) })}
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
            {t('clusterDetail.overview.ramUsed')}
          </Typography>
          <Typography variant="body2" className={classes.value}>
            {getRamUsageText(totalRamUsageMb)}
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
            {getEncryptionText(clusterSpec?.encryption_info?.encryption_at_rest ?? false,
              clusterSpec?.encryption_info?.encryption_in_transit ?? false)}
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

      <Grid container className={classes.container}>
        {availableRegions && regions && (
          <Box mt={4} display="flex">
            <Box>
              <Typography variant="subtitle2" className={classes.label}>
                {t('clusterDetail.overview.regions')}
              </Typography>
              <Box display="flex">
                {availableRegions?.map((region) => (
                  <Box mr={1} className={classes.chip} key={region?.placement_info?.cloud_info?.region}>
                    {/* <RegionWithFlag code={region?.placement_info?.cloud_info?.region} regions={regions?.data} /> */}
                    {region?.placement_info?.cloud_info?.region}
                  </Box>
                ))}
              </Box>
            </Box>
            {/* <Box alignSelf="flex-end">
              <YBButton
                variant="secondary"
                onClick={() => openEditInfraModal()}
                disabled={editingDisabled}
                className={clsx(classes.chip, classes.btnAddRegion)}
              >
                <PlusIcon />
              </YBButton>
              </Box> */}
          </Box>
        )}
      </Grid>
    </Paper>
  );
};
