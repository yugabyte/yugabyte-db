import { FC } from 'react';
import { useTranslation } from 'react-i18next';
import pluralize from 'pluralize';
import _ from 'lodash';
import { Box, Theme, Typography, makeStyles } from '@material-ui/core';
import { InstanceTags } from './InstanceTags';
import { CommunicationPorts } from './CommunicationPorts';
import { YBModal } from '../../../../components';
import {
  getAdvancedConfigChanges,
  getAsyncCluster,
  getChangedPorts,
  getPrimaryCluster,
  getSecurityConfigChanges
} from '../utils/helpers';
import { Cluster, MasterPlacementMode, UniverseDetails } from '../utils/dto';
import { isNonEmptyObject } from '../../../../../utils/ObjectUtils';

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
  isPrimary: boolean;
  onClose: () => void;
  onSubmit: () => void;
}

export const FullMoveModal: FC<FMModalProps> = ({
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
  const oldInstanceTags = oldCluster?.userIntent?.instanceTags;
  const newInstanceTags = newCluster?.userIntent?.instanceTags;
  const oldCommunicationPorts = oldConfigData?.communicationPorts;
  const newCommunicationPorts = newConfigData?.communicationPorts;
  const changedPorts = getChangedPorts(oldCommunicationPorts, newCommunicationPorts);
  const securityConfigChanges = getSecurityConfigChanges(
    getPrimaryCluster(oldConfigData),
    getPrimaryCluster(newConfigData)
  );
  const advancedConfigChanges = getAdvancedConfigChanges(
    getPrimaryCluster(oldConfigData),
    getPrimaryCluster(newConfigData)
  );
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
        {!_.isEqual(oldInstanceTags, newInstanceTags) && (
          <Box mt={2}>
            {isNew ? (
              <>{<InstanceTags tags={newInstanceTags!} />}</>
            ) : (
              <>{<InstanceTags tags={oldInstanceTags!} />}</>
            )}
          </Box>
        )}
        {securityConfigChanges?.hasChanged && (
          <Box mt={2}>
            {isNew ? (
              <>
                <b>{t('universeForm.fullMoveModal.assignPublicIP')}</b>
                &nbsp;
                {securityConfigChanges.isNewIPEnabled
                  ? t('universeForm.fullMoveModal.enabled')
                  : t('universeForm.fullMoveModal.disabled')}
              </>
            ) : (
              <>
                <b>{t('universeForm.fullMoveModal.assignPublicIP')}</b>
                &nbsp;
                {securityConfigChanges.isCurrentIPEnabled
                  ? t('universeForm.fullMoveModal.enabled')
                  : t('universeForm.fullMoveModal.disabled')}
              </>
            )}
          </Box>
        )}
        {advancedConfigChanges?.hasChanged && (
          <Box mt={2}>
            {isNew ? (
              <>
                <b>{t('universeForm.fullMoveModal.instanceProfileARN')}</b>
                &nbsp;
                {advancedConfigChanges.newArnString}
              </>
            ) : (
              <>
                <b>{t('universeForm.fullMoveModal.instanceProfileARN')}</b>
                &nbsp;
                {advancedConfigChanges.currentArnString}
              </>
            )}
          </Box>
        )}
        {isNonEmptyObject(changedPorts?.oldPorts) && isNonEmptyObject(changedPorts?.newPorts) && (
          <Box mt={2}>
            {isNew ? (
              <>{<CommunicationPorts communicationPorts={changedPorts?.newPorts} />}</>
            ) : (
              <>{<CommunicationPorts communicationPorts={changedPorts?.oldPorts} />}</>
            )}
          </Box>
        )}
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
          {oldCluster && renderConfig(oldCluster, false)}
          {newCluster && renderConfig(newCluster, true)}
        </Box>
        <Box mt={2} display="flex" flexDirection="row">
          <Typography variant="body2">{t('universeForm.fullMoveModal.likeToProceed')}</Typography>
        </Box>
      </Box>
    </YBModal>
  );
};
