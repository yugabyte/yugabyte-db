import { FC } from 'react';
import { useTranslation } from 'react-i18next';
import pluralize from 'pluralize';
import { Box, Theme, Typography, makeStyles } from '@material-ui/core';
import { YBModal } from '../../../../components';
import { Cluster, MasterPlacementMode, UniverseDetails } from '../utils/dto';
import { getAsyncCluster, getDiffClusterData, getPrimaryCluster } from '../utils/helpers';

const useStyles = makeStyles((theme: Theme) => ({
  greyText: {
    color: '#8d8f9a'
  },
  regionBox: {
    backgroundColor: '#f7f7f7',
    borderRadius: theme.spacing[0.75],
    padding: theme.spacing(1.5, 2)
  },
  configConfirmationBox: {
    display: 'inline-block',
    marginTop: theme.spacing(2),
    width: '100%'
  }
}));

interface PlacementModalProps {
  newConfigData: UniverseDetails;
  oldConfigData: UniverseDetails;
  open: boolean;
  isPrimary: boolean;
  onClose: () => void;
  onSubmit: () => void;
}

export const PlacementModal: FC<PlacementModalProps> = ({
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
  const diffClusterData = getDiffClusterData(oldCluster, newCluster);

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
        {isPrimary && diffClusterData.masterPlacementChanged && (
          <Box className={classes.configConfirmationBox}>
            <b>
              {userIntent?.dedicatedNodes
                ? `${MasterPlacementMode.DEDICATED}`
                : `${MasterPlacementMode.COLOCATED}`}
            </b>
            &nbsp;mode
          </Box>
        )}
        {(diffClusterData.numNodesChanged || diffClusterData.masterPlacementChanged) && (
          <>
            {
              <Box className={classes.configConfirmationBox}>
                <b>
                  {isNew && diffClusterData.newNodeCount !== diffClusterData.currentNodeCount
                    ? diffClusterData.newNodeCount > diffClusterData.currentNodeCount
                      ? `Scale Up - ${userIntent?.numNodes}`
                      : `Scale Down - ${userIntent?.numNodes}`
                    : userIntent?.numNodes}
                </b>
                &nbsp;nodes
              </Box>
            }
          </>
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
                    {az.name} - {az.numNodesInAZ} {pluralize('node', az.numNodesInAZ)}{' '}
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
      title={t('universeForm.placementModal.modalTitle')}
      open={open}
      onClose={onClose}
      size="sm"
      cancelLabel={t('common.cancel')}
      submitLabel={t('common.proceed')}
      onSubmit={onSubmit}
      overrideHeight="auto"
      titleSeparator
      submitTestId="submit-full-move"
      cancelTestId="close-full-move"
    >
      <Box display="flex" width="100%" flexDirection="column" data-testid="full-move-modal">
        <Box>
          <Typography variant="body2">
            {t('universeForm.placementModal.modalDescription')}
          </Typography>
        </Box>
        <Box mt={2} display="flex" flexDirection="row">
          {oldCluster && renderConfig(oldCluster, false)}
          {newCluster && renderConfig(newCluster, true)}
        </Box>
        <Box mt={2} display="flex" flexDirection="row">
          <Typography variant="body2">{t('universeForm.placementModal.likeToProceed')}</Typography>
        </Box>
      </Box>
    </YBModal>
  );
};
