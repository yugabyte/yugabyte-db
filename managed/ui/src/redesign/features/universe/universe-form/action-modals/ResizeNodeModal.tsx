import { FC, useState } from 'react';
import { useTranslation } from 'react-i18next';
import { useForm } from 'react-hook-form';
import { Box, Typography } from '@material-ui/core';
import { YBCheckbox, YBInputField, YBLabel, YBModal } from '../../../../components';
import { api } from '../utils/api';
import { transitToUniverse } from '../utils/helpers';
import { UniverseConfigure, ClusterType, UpdateActions } from '../utils/dto';

interface RNModalProps {
  open: boolean;
  isPrimary: boolean;
  universeData: UniverseConfigure;
  onClose: () => void;
}

type ResizeFormValues = {
  timeDelay: number;
};

const defaultValues: ResizeFormValues = {
  timeDelay: 180
};

export const ResizeNodeModal: FC<RNModalProps> = ({ open, isPrimary, universeData, onClose }) => {
  const [isResizeConfirmed, setResizeConfirm] = useState(false);
  const { t } = useTranslation();
  const { control, handleSubmit } = useForm<ResizeFormValues>({
    defaultValues
  });

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
    const cluster = universeData?.clusters?.find(
      (c) => c.clusterType === (isPrimary ? ClusterType.PRIMARY : ClusterType.ASYNC)
    );
    if (cluster && universeData) {
      let payload = {
        clusters: [cluster],
        nodePrefix: universeData?.nodePrefix,
        sleepAfterMasterRestartMillis: formValues.timeDelay * 1000,
        sleepAfterTServerRestartMillis: formValues.timeDelay * 1000,
        taskType: 'Resize_Node',
        universeUUID: universeData?.universeUUID,
        upgradeOption: 'Rolling',
        ybSoftwareVersion: cluster?.userIntent.ybSoftwareVersion
      };
      universeData?.universeUUID && submitResizeForm(payload, universeData.universeUUID);
    }
  });

  const confirmResizeCheckBox = () => {
    return (
      <YBCheckbox
        defaultChecked={isResizeConfirmed}
        value={isResizeConfirmed}
        onChange={(e) => setResizeConfirm(e.target.checked)}
        label={t('universeForm.resizeNodeModal.confirmResizeCheckbox')}
        size="medium"
      />
    );
  };

  return (
    <YBModal
      title={t('universeForm.resizeNodeModal.modalTitle')}
      open={open}
      onClose={onClose}
      size="xs"
      cancelLabel={t('universeForm.resizeNodeModal.cancelLabel')}
      submitLabel={t('universeForm.resizeNodeModal.confirmLabel')}
      buttonProps={{
        primary: {
          disabled: !isResizeConfirmed
        }
      }}
      dialogContentProps={{ style: { paddingTop: 20 } }}
      onSubmit={handleFormSubmit}
      overrideHeight={220}
      overrideWidth={600}
      titleSeparator
      footerAccessory={confirmResizeCheckBox()}
      submitTestId="submit-resize-node"
      cancelTestId="close-resize-node"
    >
      <Box display="flex" width="100%" data-testid="resize-node-modal">
        {universeData?.updateOptions?.includes(UpdateActions.SMART_RESIZE_NON_RESTART) ? (
          <Typography variant="body2">
            {t('universeForm.resizeNodeModal.modalDescription')}
          </Typography>
        ) : (
          <>
            <YBLabel>{t('universeForm.resizeNodeModal.timeDelayLabel')}</YBLabel>
            <Box flex={1} ml={0.5}>
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
          </>
        )}
      </Box>
    </YBModal>
  );
};
