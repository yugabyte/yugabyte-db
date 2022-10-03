import React, { useContext, FC } from 'react';
import { useTranslation } from 'react-i18next';
import { Typography, makeStyles, Box, Paper } from '@material-ui/core';
import { ClusterTier, ClusterData, ClusterFaultTolerance } from '@app/api/src';
import { YBButton } from '@app/components';
import { ClusterContext } from '@app/features/clusters/details/ClusterDetails';
// import { getCloudProviderIcon } from '@app/features/clusters/list/ClusterCard';
import { OPEN_EDIT_INFRASTRUCTURE_MODAL } from '@app/helpers';

import EditIcon from '@app/assets/edit.svg';

const useStyles = makeStyles((theme) => ({
  widget: {
    padding: theme.spacing(2),
    position: 'relative',
    gridArea: 'infra'
  },
  editIcon: {
    color: theme.palette.grey[600],
    marginLeft: theme.spacing(1.5),
    cursor: 'pointer'
  },
  actionBtn: {
    position: 'absolute',
    top: theme.spacing(1),
    right: theme.spacing(1)
  },
  infraStat: {
    minWidth: 'fit-content'
  },
  region: {
    display: 'flex',
    alignItems: 'center',
    margin: theme.spacing(1.125, 0),

    '& > svg': {
      marginRight: theme.spacing(1)
    }
  }
}));

interface InfrastructureWidgetProps {
  cluster: ClusterData;
}

export const InfrastructureWidget: FC<InfrastructureWidgetProps> = ({ cluster }) => {
  const classes = useStyles();
  const { t } = useTranslation();
  const context = useContext(ClusterContext);
  const clusterInfo = cluster.spec.cluster_info;
  const currentNumNodes = clusterInfo.num_nodes ?? 0;
  const averageCpuUsage = clusterInfo.node_info.cpu_usage ?? 0;
  const clusterDiskSize = clusterInfo.node_info.disk_size_gb;
  const faultTolerance = clusterInfo.fault_tolerance;
  let faultToleranceDisplay = t('clusterWizard.faultToleranceNone');
  if (faultTolerance === ClusterFaultTolerance.Node) {
    faultToleranceDisplay = t('clusterWizard.faultToleranceInstance');
  } else if (faultTolerance === ClusterFaultTolerance.Zone) {
    faultToleranceDisplay = t('clusterWizard.faultToleranceAZ');
  }

  const openEditInfraModal = () => {
    if (context?.dispatch) {
      context.dispatch({ type: OPEN_EDIT_INFRASTRUCTURE_MODAL });
    }
  };

  const originalRegion = cluster?.spec?.cloud_info?.region;
  const clusterTier = cluster?.spec.cluster_info.cluster_tier;
  const totalDiskSize = clusterDiskSize * currentNumNodes;

  const editingDisabled = false;

  return (
    <Paper className={classes.widget}>
      <Typography variant="h5">{t('clusterDetail.settings.infrastructure')}</Typography>
      {cluster?.spec && clusterTier !== ClusterTier.Free && (
        <YBButton
          variant="ghost"
          startIcon={<EditIcon />}
          className={classes.actionBtn}
          onClick={openEditInfraModal}
          disabled={editingDisabled}
        >
          {t('clusterDetail.settings.editInfrastructure')}
        </YBButton>
      )}
      <Box mt={2} display="flex" justifyContent="space-between">
        <div className={classes.infraStat}>
          <Typography variant="subtitle1" color="textSecondary">
            {t('clusterDetail.settings.region')}
          </Typography>
          <Typography variant="body2" className={classes.region}>
            {/* {getCloudProviderIcon(cluster?.spec.cloud_info.code)} */}
            {originalRegion}
          </Typography>
        </div>
        <div className={classes.infraStat}>
          <Typography variant="subtitle1" color="textSecondary">
            {t('clusterDetail.settings.nodes')}
          </Typography>
          <p>{currentNumNodes}</p>
        </div>
        <div className={classes.infraStat}>
          <Typography variant="subtitle1" color="textSecondary">
            {t('clusterDetail.settings.totalvCpu')}
          </Typography>
          <p>{averageCpuUsage}</p>
        </div>
        <div className={classes.infraStat}>
          <Typography variant="subtitle1" color="textSecondary">
            {t('clusterDetail.settings.totalDiskSize')}
          </Typography>
          <p>{t('units.GB', { value: totalDiskSize })}</p>
        </div>
        <div className={classes.infraStat}>
          <Typography variant="subtitle1" color="textSecondary">
            {t('clusterDetail.settings.faultTolerance')}
          </Typography>
          <p>{faultToleranceDisplay}</p>
        </div>
      </Box>
    </Paper>
  );
};
