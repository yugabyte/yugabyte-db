import { FC } from 'react';
import { useTranslation } from 'react-i18next';
import { Box, Theme, Typography, makeStyles } from '@material-ui/core';
import { YBModal, YBButton } from '../../../../components';
import { getAsyncCluster, getPrimaryCluster } from '../utils/helpers';
import { UniverseDetails } from '../utils/dto';

const useStyles = makeStyles((theme: Theme) => ({
  greyText: {
    color: '#8d8f9a'
  }
}));

interface SRModalProps {
  newConfigData: UniverseDetails;
  oldConfigData: UniverseDetails;
  open: boolean;
  isPrimary: boolean;
  handleFullMove: () => void;
  handleSmartResize: () => void;
  onClose: () => void;
}

export const SmartResizeModal: FC<SRModalProps> = ({
  newConfigData,
  oldConfigData,
  open,
  isPrimary,
  handleFullMove,
  handleSmartResize,
  onClose
}) => {
  const { t } = useTranslation();
  const classes = useStyles();
  const oldIntent = isPrimary
    ? getPrimaryCluster(oldConfigData)?.userIntent
    : getAsyncCluster(oldConfigData)?.userIntent;
  const newIntent = isPrimary
    ? getPrimaryCluster(newConfigData)?.userIntent
    : getAsyncCluster(newConfigData)?.userIntent;
  const isVolumeChanged = oldIntent?.deviceInfo?.volumeSize !== newIntent?.deviceInfo?.volumeSize;

  return (
    <YBModal
      title={t('universeForm.fullMoveModal.modalTitle')}
      open={open}
      onClose={onClose}
      size="sm"
      cancelLabel={t('common.cancel')}
      submitLabel={t('universeForm.fullMoveModal.submitLabel')}
      dialogContentProps={{ style: { paddingTop: 20 } }}
      onSubmit={handleFullMove}
      titleSeparator
      submitTestId="SmartResizeModal-FM"
      cancelTestId="SmartResizeModal-Cancel"
      footerAccessory={
        <YBButton data-testid="SmartResizeModal-SR" variant="primary" onClick={handleSmartResize}>
          {t('universeForm.smartResizeModal.buttonLabel')}
        </YBButton>
      }
    >
      <Box display="flex" width="100%" flexDirection="column" data-testid="smart-resize-modal">
        <Box>
          <Typography variant="body2">
            {t('universeForm.smartResizeModal.modalDescription', {
              value: isVolumeChanged ? 'and volume size' : ''
            })}
          </Typography>
        </Box>
        <Box mt={2} display="flex" width="100%" flexDirection="row">
          <Box flex={1} className={classes.greyText} p={1}>
            <Typography variant="h5">{t('universeForm.current')}</Typography>
            <Box mt={2} display="inline-block" width="100%">
              <b data-testid="old-instance-type">{oldIntent?.instanceType}</b>&nbsp;
              {t('universeForm.perInstanceType')}
              <br />
              <b data-testid="old-volume-size">{oldIntent?.deviceInfo?.volumeSize}Gb</b>&nbsp;
              {t('universeForm.perInstance')}
            </Box>
          </Box>
          <Box flex={1} p={1}>
            <Typography variant="h5">{t('universeForm.new')}</Typography>
            <Box mt={2} display="inline-block" width="100%">
              <b data-testid="new-instance-type">{newIntent?.instanceType}</b>&nbsp;
              {t('universeForm.perInstanceType')}
              <br />
              <b data-testid="new-volume-size">{newIntent?.deviceInfo?.volumeSize}Gb</b>&nbsp;
              {t('universeForm.perInstance')}
            </Box>
          </Box>
        </Box>
      </Box>
    </YBModal>
  );
};
