import { useState } from 'react';
import { FormProvider, useForm } from 'react-hook-form';
import { toast } from 'react-toastify';
import { useMutation } from 'react-query';
import { useTranslation } from 'react-i18next';
import { Box, makeStyles, Typography } from '@material-ui/core';
import { YBCheckbox, YBInputField, YBLabel, YBModal } from '../../../components';
import { TroubleshootingAPI } from '../api';

interface EditTPConfigDialogProps {
  open: boolean;
  onRefetchConfig: () => void;
  onClose: () => void;
  data: any;
}

const useStyles = makeStyles((theme) => ({
  root: {
    padding: `${theme.spacing(3)}px ${theme.spacing(4.5)}px`
  },
  modalTitle: {
    marginLeft: theme.spacing(2.25)
  }
}));

export const EditTPConfigDialog = ({
  open,
  onRefetchConfig,
  onClose,
  data
}: EditTPConfigDialogProps) => {
  const { t } = useTranslation();
  const helperClasses = useStyles();

  const [forceUpdate, setForceUpdate] = useState<boolean>(false);
  const { tpUuid, customerUUID } = data;

  // useForm hook definition
  const formMethods = useForm<any>({
    defaultValues: {
      tpUrl: data.tpUrl,
      ybaUrl: data.ybaUrl,
      metricsUrl: data.metricsUrl,
      apiToken: data.apiToken,
      metricsScrapePeriodSecs: data.metricsScrapePeriodSecs
    },
    mode: 'onChange',
    reValidateMode: 'onChange'
  });
  const { control, handleSubmit } = formMethods;

  const updateTPServiceMetadata = useMutation(
    (payload: any) => TroubleshootingAPI.updateTpMetadata(payload, tpUuid, forceUpdate),
    {
      onSuccess: (response: any) => {
        toast.success(t('clusterDetail.troubleshoot.editDialog.updateMetadataSuccess'));
        onRefetchConfig();
        onClose();
      },
      onError: () => {
        toast.error(t('clusterDetail.troubleshoot.editDialog.updateMetadataFailed'));
      }
    }
  );

  const handleFormSubmit = handleSubmit((formValues: any) => {
    const payload = { ...formValues };
    payload.uuid = tpUuid;
    payload.customerUUID = customerUUID;
    updateTPServiceMetadata.mutateAsync(payload);
  });

  return (
    <YBModal
      open={open}
      onClose={onClose}
      title={t('clusterDetail.troubleshoot.editDialog.title')}
      onSubmit={handleFormSubmit}
      cancelLabel={t('common.cancel')}
      submitLabel={t('clusterDetail.troubleshoot.editDialog.updateButton')}
      size="md"
      overrideHeight={'480px'}
      titleSeparator
      enableBackdropDismiss
      dialogContentProps={{
        className: helperClasses.root,
        dividers: true
      }}
      submitButtonTooltip={
        data.inUseStatus ? (
          <Typography
            variant="body2"
            style={{
              fontSize: '13px',
              fontWeight: 400,
              lineHeight: 1.25
            }}
          >
            {t('clusterDetail.troubleshoot.editDialog.tooltipMessage')}
          </Typography>
        ) : (
          ''
        )
      }
      footerAccessory={
        <YBCheckbox
          checked={forceUpdate}
          onChange={() => setForceUpdate(!forceUpdate)}
          label={t('clusterDetail.troubleshoot.editDialog.forceUpdateCheckBoxLabel')}
          inputProps={{
            'data-testid': 'EditTPConfigDialog-ForceUpdate'
          }}
        />
      }
      buttonProps={{
        primary: { disabled: data.inUseStatus && !forceUpdate }
      }}
      titleContentProps={helperClasses.modalTitle}
    >
      <FormProvider {...formMethods}>
        <Box
          display="flex"
          width="100%"
          flexDirection={'column'}
          data-testid="RegisterTroubleshootingService-Container"
        >
          <Box display="flex" flexDirection={'row'} mt={2}>
            <YBLabel width="250px" dataTestId="RegisterTSService-Label">
              {t('clusterDetail.troubleshoot.tpServiceUrlLabel')}
            </YBLabel>
            <Box flex={1}>
              <YBInputField
                control={control}
                name="tpUrl"
                style={{ width: '300px' }}
                type="text"
                rules={{
                  required: t('clusterDetail.troubleshoot.urlRequired')
                }}
              />
            </Box>
          </Box>

          <Box display="flex" flexDirection={'row'} mt={2}>
            <YBLabel width="250px" dataTestId="RegisterTSService-Label">
              {t('clusterDetail.troubleshoot.ybPlatformServiceUrlLabel')}
            </YBLabel>
            <Box flex={1}>
              <YBInputField
                control={control}
                name="ybaUrl"
                style={{ width: '300px' }}
                type="text"
                rules={{
                  required: t('clusterDetail.troubleshoot.urlRequired')
                }}
              />
            </Box>
          </Box>

          <Box display="flex" flexDirection={'row'} mt={2}>
            <YBLabel width="250px" dataTestId="RegisterTSService-Label">
              {t('clusterDetail.troubleshoot.ybPlatformMetricsUrlLabel')}
            </YBLabel>
            <Box flex={1}>
              <YBInputField
                control={control}
                name="metricsUrl"
                style={{ width: '300px' }}
                type="text"
                rules={{
                  required: t('clusterDetail.troubleshoot.urlRequired')
                }}
              />
            </Box>
          </Box>

          <Box display="flex" flexDirection={'row'} mt={2}>
            <YBLabel width="250px" dataTestId="RegisterTSService-Label">
              {t('clusterDetail.troubleshoot.apiTokenLabel')}
            </YBLabel>
            <Box flex={1}>
              <YBInputField
                control={control}
                name="apiToken"
                style={{ width: '300px' }}
                type="text"
                rules={{
                  required: t('clusterDetail.troubleshoot.apiTokenRequired')
                }}
              />
            </Box>
          </Box>

          <Box display="flex" flexDirection={'row'} mt={2}>
            <YBLabel width="250px" dataTestId="RegisterTSService-Label">
              {t('clusterDetail.troubleshoot.metricsScrapePeriodSecLabel')}
            </YBLabel>
            <Box flex={1}>
              <YBInputField
                control={control}
                name="metricsScrapePeriodSecs"
                style={{ width: '300px' }}
                type="text"
                rules={{
                  required: t('clusterDetail.troubleshoot.metricsScrapePeriodSecsRequired')
                }}
              />
            </Box>
          </Box>
        </Box>
      </FormProvider>
    </YBModal>
  );
};
