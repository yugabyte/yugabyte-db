import { FC, useState } from 'react';
import { useForm } from 'react-hook-form';
import { useTranslation } from 'react-i18next';
import { Box, Theme, Typography, makeStyles } from '@material-ui/core';
import { YBModal, YBButton, YBCheckbox, YBLabel, YBInputField } from '../../../../components';
import { api } from '../utils/api';
import { getAsyncCluster, getPrimaryCluster } from '../utils/helpers';
import { SmartResizeFormValues } from '@app/redesign/utils/dtos';
import { UniverseDetails, ClusterType, UpdateActions } from '../utils/dto';
import { transitToUniverse } from '../utils/helpers';

const useStyles = makeStyles((theme: Theme) => ({
  greyText: {
    color: '#8d8f9a'
  },
  labelWidth: {
    '& .MuiFormControlLabel-root': {
      width: '190px'
    }
  }
}));

interface SRModalProps {
  newConfigData: UniverseDetails;
  oldConfigData: UniverseDetails;
  open: boolean;
  isPrimary: boolean;
  handlePrechecks: (runOnlyPrechecks: boolean) => void;
  onClose: () => void;
}

const defaultValues: SmartResizeFormValues = {
  timeDelay: 180
};

export const SmartResizeModal: FC<SRModalProps> = ({
  newConfigData,
  oldConfigData,
  open,
  isPrimary,
  handlePrechecks,
  onClose
}) => {
  const { t } = useTranslation();
  const classes = useStyles();

  const [isResizeConfirmed, setResizeConfirm] = useState(false);

  const { control, handleSubmit } = useForm<SmartResizeFormValues>({
    defaultValues
  });
  const oldIntent = isPrimary
    ? getPrimaryCluster(oldConfigData)?.userIntent
    : getAsyncCluster(oldConfigData)?.userIntent;
  const newIntent = isPrimary
    ? getPrimaryCluster(newConfigData)?.userIntent
    : getAsyncCluster(newConfigData)?.userIntent;
  const isInstanceTypeChanged = oldIntent?.instanceType !== newIntent?.instanceType;
  const isVolumeChanged = oldIntent?.deviceInfo?.volumeSize !== newIntent?.deviceInfo?.volumeSize;


  const submitResizeForm = async (finalPayload: any, uuid: string) => {
    try {
      await api.resizeNodes(finalPayload, uuid);
    } catch (e) {
      console.error(e);
    } finally {
      transitToUniverse(uuid);
    }
  };

  const handleFormSubmit = handleSubmit((formValues) => {
    const cluster = newConfigData?.clusters?.find(
      (c) => c.clusterType === (isPrimary ? ClusterType.PRIMARY : ClusterType.ASYNC)
    );
    if (cluster && newConfigData) {
      const payload = {
        clusters: [cluster],
        nodePrefix: newConfigData?.nodePrefix,
        sleepAfterMasterRestartMillis: formValues.timeDelay * 1000,
        sleepAfterTServerRestartMillis: formValues.timeDelay * 1000,
        taskType: 'Resize_Node',
        universeUUID: newConfigData?.universeUUID,
        upgradeOption: 'Rolling',
        ybSoftwareVersion: cluster?.userIntent.ybSoftwareVersion
      };
      newConfigData?.universeUUID && submitResizeForm(payload, newConfigData.universeUUID);
    }
  });

  const confirmResizeCheckBox = () => {
    return (
      <Box className={classes.labelWidth}>
      <YBCheckbox
        defaultChecked={isResizeConfirmed}
        value={isResizeConfirmed}
        onChange={(e) => setResizeConfirm(e.target.checked)}
        label={t('universeForm.smartResizeModal.confirmResizeCheckbox')}
        size="medium"
      />
      </Box>
    );
  };

  const isNonRestartSmartResize = newConfigData?.updateOptions?.includes(UpdateActions.SMART_RESIZE_NON_RESTART);

  return (
    <YBModal
      title={t('universeForm.smartResizeModal.modalTitle')}
      open={open}
      onClose={onClose}
      size="sm"
      overrideWidth={640}
      dialogContentProps={{ style: { paddingTop: 20 } }}
      titleSeparator
      footerAccessory={
        <>
        {isNonRestartSmartResize && confirmResizeCheckBox()}
        <div
          style={{
            display: 'flex',
            alignItems: 'center',
            justifyContent: 'flex-end',
            gap: '8px',
            width: '100%'
          }}
        >
          <YBButton
            type="button"
            variant="secondary"
            data-testid="SmartResizeModal-CancelButton"
            onClick={onClose}
          >
            {t('common.cancel')}
          </YBButton>
          <YBButton
            type="button"
            variant="secondary"
            onClick={() => handlePrechecks(true)}
            data-testid="SmartResizeModal-RunPrechecksButton"
          >
            {t('universeActions.runPrecheckOnlyButton')}
          </YBButton>
          <YBButton data-testid="SmartResizeModal-SR" variant="primary" onClick={handleFormSubmit} disabled={isNonRestartSmartResize && !isResizeConfirmed}>
            {t('universeForm.smartResizeModal.buttonLabel')}
          </YBButton>
        </div>
        </>
       
      }
    >
      <Box display="flex" width="100%" flexDirection="column" data-testid="smart-resize-modal">
        <Box>
          <Typography variant="body2">
            {t('universeForm.smartResizeModal.modalDescription', {
              value: isInstanceTypeChanged ? 'changes instance type' : '',
              value2: isVolumeChanged ? `${isInstanceTypeChanged ? 'and' : 'changes'} volume size` : ''
            })}
          </Typography>
          {isNonRestartSmartResize && (
            <Typography variant="body2">
              {t('universeForm.smartResizeModal.modalDescriptionNonRestart')}
            </Typography>
          )}
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
        {!isNonRestartSmartResize && <Box ml={1} mt={1}>
          <YBLabel width='300px'>{t('universeForm.smartResizeModal.timeDelayLabel')}</YBLabel>
          <Box flex={1} mt={0.5}>
            <YBInputField
              control={control}
              type="number"
              name="timeDelay"
              fullWidth
              inputProps={{
                autoFocus: true,
                'data-testid': 'time-delay'
              }}
            />
          </Box>
        </Box>}
      </Box>
    </YBModal>
  );
};
