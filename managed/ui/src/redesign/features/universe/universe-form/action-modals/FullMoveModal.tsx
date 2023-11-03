import { FC } from 'react';
import { useTranslation } from 'react-i18next';
import pluralize from 'pluralize';
import { Box, Theme, Typography, makeStyles } from '@material-ui/core';
import { YBModal } from '../../../../components';
import { getPrimaryCluster } from '../utils/helpers';
import { Cluster, MasterPlacementMode, UniverseDetails } from '../utils/dto';

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

interface FMModalProps {
  newConfigData: UniverseDetails;
  oldConfigData: UniverseDetails;
  open: boolean;
  onClose: () => void;
  onSubmit: () => void;
}

export const FullMoveModal: FC<FMModalProps> = ({
  newConfigData,
  oldConfigData,
  open,
  onClose,
  onSubmit
}) => {
  const { t } = useTranslation();
  const classes = useStyles();
  const oldPrimaryCluster = getPrimaryCluster(oldConfigData);
  const newPrimaryCluster = getPrimaryCluster(newConfigData);

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
        <Box className={classes.configConfirmationBox}>
          <b>{userIntent?.instanceType}</b>&nbsp;type
          <br />
          <b>{userIntent?.deviceInfo?.numVolumes}</b>{' '}
          {pluralize('volume', userIntent?.deviceInfo?.numVolumes)} of &nbsp;
          <b>{userIntent?.deviceInfo?.volumeSize}Gb</b> {t('universeForm.perInstance')}
        </Box>
        <Box className={classes.configConfirmationBox}>
          <b>
            {userIntent?.dedicatedNodes
              ? `${MasterPlacementMode.DEDICATED}`
              : `${MasterPlacementMode.COLOCATED}`}
          </b>
          &nbsp;mode
        </Box>
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
      title={t('universeForm.fullMoveModal.modalTitle')}
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
            {t('universeForm.fullMoveModal.modalDescription')}
          </Typography>
        </Box>
        <Box mt={2} display="flex" flexDirection="row">
          {oldPrimaryCluster && renderConfig(oldPrimaryCluster, false)}
          {newPrimaryCluster && renderConfig(newPrimaryCluster, true)}
        </Box>
        <Box mt={2} display="flex" flexDirection="row">
          <Typography variant="body2">{t('universeForm.fullMoveModal.likeToProceed')}</Typography>
        </Box>
      </Box>
    </YBModal>
  );
};
