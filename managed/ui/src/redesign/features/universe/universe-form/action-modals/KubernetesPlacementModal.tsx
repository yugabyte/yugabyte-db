import React, { FC } from 'react';
import { useTranslation } from 'react-i18next';
import { Box, Theme, Typography, makeStyles } from '@material-ui/core';
import clsx from 'clsx';
import pluralize from 'pluralize';
import { YBModal } from '../../../../components';
import { Cluster, UniverseDetails } from '../utils/dto';
import { getAsyncCluster, getKubernetesDiffClusterData, getPrimaryCluster } from '../utils/helpers';

const useStyles = makeStyles((theme: Theme) => ({
  greyText: {
    color: '#8d8f9a'
  },
  regionBox: {
    backgroundColor: '#f7f7f7',
    borderRadius: theme.spacing[0.75],
    padding: theme.spacing(1.5, 2)
  },
  flexColumnBox: {
    display: 'flex',
    flexDirection: 'column'
  },
  configConfirmationBox: {
    display: 'inline-block',
    marginTop: theme.spacing(2),
    width: '100%'
  }
}));

interface KubernetesPlacementModalProps {
  newConfigData: UniverseDetails;
  oldConfigData: UniverseDetails;
  open: boolean;
  isPrimary: boolean;
  onClose: () => void;
  onSubmit: () => void;
}

export const KubernetesPlacementModal: FC<KubernetesPlacementModalProps> = ({
  newConfigData,
  oldConfigData,
  open,
  isPrimary,
  onClose,
  onSubmit
}) => {
  const { t } = useTranslation();
  const classes = useStyles();
  const oldCluster = isPrimary ? getPrimaryCluster(oldConfigData) : getAsyncCluster(oldConfigData);
  const newCluster = isPrimary ? getPrimaryCluster(newConfigData) : getAsyncCluster(newConfigData);
  const diffClusterData = getKubernetesDiffClusterData(oldCluster, newCluster);
  const tserverConfigChanged =
    diffClusterData.oldTServerNumCores !== diffClusterData.newTServerNumCores ||
    diffClusterData.oldTServerMemory !== diffClusterData.newTServerMemory ||
    diffClusterData.oldTServerVolumeSize !== diffClusterData.newTServerVolumeSize;
  const masterConfigChanged =
    diffClusterData.oldMasterNumCores !== diffClusterData.newMasterNumCores ||
    diffClusterData.oldMasterMemory !== diffClusterData.newMasterMemory;

  const renderConfig = (cluster: Cluster, isNew: boolean) => {
    const { placementInfo, userIntent } = cluster;

    return (
      <Box
        display="flex"
        flexDirection="column"
        flex={1}
        p={1}
        className={!isNew ? classes.greyText : undefined}
      >
        <Typography variant="h5">
          {isNew ? t('universeForm.new') : t('universeForm.current')}
        </Typography>
        {tserverConfigChanged && (
          <>
            {
              <Box className={clsx(classes.configConfirmationBox, classes.flexColumnBox)}>
                <Box>
                  <b>{'TServer'}</b>
                </Box>
                <Box>
                  {t('universeForm.kubernetesPlacementModal.cores')}&nbsp;
                  <b>
                    {isNew
                      ? diffClusterData.newTServerNumCores
                      : diffClusterData.oldTServerNumCores}
                  </b>
                  &nbsp;
                </Box>
                <Box>
                  {t('universeForm.kubernetesPlacementModal.memory')}&nbsp;
                  <b>
                    {isNew ? diffClusterData.newTServerMemory : diffClusterData.oldTServerMemory}
                  </b>
                </Box>
                <Box>
                  {t('universeForm.kubernetesPlacementModal.volumeSize')} &nbsp;
                  <b>
                    {isNew
                      ? diffClusterData.newTServerVolumeSize
                      : diffClusterData.oldTServerVolumeSize}
                  </b>
                </Box>
              </Box>
            }
          </>
        )}

        {masterConfigChanged && (
          <>
            {
              <Box className={clsx(classes.configConfirmationBox, classes.flexColumnBox)}>
                <Box>
                  <b>{'Master'}</b>
                </Box>
                <Box>
                  {t('universeForm.kubernetesPlacementModal.cores')}&nbsp;
                  <b>
                    {isNew ? diffClusterData.newMasterNumCores : diffClusterData.oldMasterNumCores}
                  </b>
                </Box>
                <Box>
                  {t('universeForm.kubernetesPlacementModal.memory')}&nbsp;
                  <b>{isNew ? diffClusterData.newMasterMemory : diffClusterData.oldMasterMemory}</b>
                </Box>
              </Box>
            }
          </>
        )}

        {diffClusterData.numNodesChanged && (
          <>
            {
              <Box className={classes.configConfirmationBox}>
                <b>
                  {isNew && diffClusterData.newNodeCount !== diffClusterData.currentNodeCount
                    ? diffClusterData.newNodeCount! > diffClusterData.currentNodeCount!
                      ? `${t('universeForm.kubernetesPlacementModal.scaleUp')} - ${
                          userIntent?.numNodes
                        }`
                      : `${t('universeForm.kubernetesPlacementModal.scaleDown')} - ${
                          userIntent?.numNodes
                        }`
                    : userIntent?.numNodes}
                </b>
                &nbsp;{t('universeForm.kubernetesPlacementModal.pods')}
              </Box>
            }
          </>
        )}
        {!isPrimary && diffClusterData.oldNumReadReplicas !== diffClusterData.newNumReadReplicas && (
          <Box className={classes.configConfirmationBox}>
            {t('universeForm.kubernetesPlacementModal.readReplicas')} -&nbsp;
            <b>{isNew ? diffClusterData.newNumReadReplicas : diffClusterData.oldNumReadReplicas}</b>
          </Box>
        )}
        <Box mt={1} display="flex" flexDirection="column">
          {placementInfo?.cloudList[0].regionList?.map((region) => (
            <Box
              display="flex"
              key={region.code}
              mt={1}
              flexDirection="column"
              className={classes.regionBox}
            >
              <Typography variant="h5">{region.code}</Typography>
              <Box pl={2}>
                {region.azList.map((az) => (
                  <Typography key={az.name} variant="body2">
                    {az.name} - {az.numNodesInAZ} {pluralize('node', az.numNodesInAZ)}
                  </Typography>
                ))}
              </Box>
            </Box>
          ))}
        </Box>
      </Box>
    );
  };

  return (
    <YBModal
      title={t('universeForm.kubernetesPlacementModal.modalTitle')}
      open={open}
      onClose={onClose}
      size="sm"
      cancelLabel={t('common.cancel')}
      submitLabel={t('common.proceed')}
      onSubmit={onSubmit}
      overrideHeight="auto"
      titleSeparator
      submitTestId="submit-kuberentes-config"
      cancelTestId="close-kubernetes-config"
    >
      <Box
        display="flex"
        width="100%"
        flexDirection="column"
        data-testid="kubernetes-placement-modal"
      >
        <Box>
          <Typography variant="body2">
            {isPrimary
              ? t('universeForm.kubernetesPlacementModal.modalDescription')
              : t('universeForm.kubernetesPlacementModal.modalDescriptionRR')}
          </Typography>
        </Box>
        <Box mt={2} display="flex" flexDirection="row">
          {oldCluster && renderConfig(oldCluster, false)}
          {newCluster && renderConfig(newCluster, true)}
        </Box>
        <Box mt={2} display="flex" flexDirection="row">
          <Typography variant="body2">
            {t('universeForm.kubernetesPlacementModal.likeToProceed')}
          </Typography>
        </Box>
      </Box>
    </YBModal>
  );
};
