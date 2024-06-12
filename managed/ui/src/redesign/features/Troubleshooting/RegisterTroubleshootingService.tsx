import { Box } from '@material-ui/core';
import { useForm, FormProvider } from 'react-hook-form';
import { useMutation } from 'react-query';
import { useTranslation } from 'react-i18next';
import { toast } from 'react-toastify';
import { YBButton, YBInputField, YBLabel } from '../../components';
import { TroubleshootingAPI } from './api';
import { isNonEmptyString } from '../../../utils/ObjectUtils';
import { IN_DEVELOPMENT_MODE } from '../../../config';

interface RegisterTroubleshootingServiceProps {
  onRefetchConfig: () => void;
}

export const RegisterTroubleshootingService = ({
  onRefetchConfig
}: RegisterTroubleshootingServiceProps) => {
  const { t } = useTranslation();
  const baseUrl = window.location.origin;

  // useForm hook definition
  const formMethods = useForm<any>({
    defaultValues: {
      tpUrl: '',
      ybaUrl: IN_DEVELOPMENT_MODE ? 'http://localhost:9000' : baseUrl,
      metricsUrl: IN_DEVELOPMENT_MODE ? 'http://localhost:9090' : `${baseUrl}:9090`
    },
    mode: 'onChange',
    reValidateMode: 'onChange'
  });
  const { control, handleSubmit } = formMethods;

  const registerTpService = useMutation(
    (payload: any) =>
      TroubleshootingAPI.registerTp(payload.tpUrl, payload.ybaUrl, payload.metricsUrl),
    {
      onSuccess: () => {
        onRefetchConfig();
        toast.success(t('clusterDetail.troubleshoot.registrationSuccess'));
      },
      onError: (error: any) => {
        toast.error(
          isNonEmptyString(error?.response?.data?.error)
            ? error?.response?.data?.error
            : t('clusterDetail.troubleshoot.registrationFailed')
        );
      }
    }
  );

  const handleFormSubmit = handleSubmit((formValues) => {
    const payload = { ...formValues };
    registerTpService.mutate(payload);
  });

  return (
    <Box>
      <FormProvider {...formMethods}>
        <Box
          mt={4}
          display="flex"
          width="100%"
          flexDirection={'column'}
          data-testid="RegisterTroubleshootingService-Container"
        >
          <Box display="flex" flexDirection={'row'} mt={2}>
            <YBLabel width="300px" dataTestId="RegisterTSService-Label">
              {t('clusterDetail.troubleshoot.tpServiceUrlLabel')}
            </YBLabel>
            <Box flex={1}>
              <YBInputField
                control={control}
                name="tpUrl"
                style={{ width: '300px' }}
                placeholder={t('clusterDetail.troubleshoot.urlPlaceholder')}
                type="text"
                helperText={t('clusterDetail.troubleshoot.tpServiceUrlHelperText')}
                rules={{
                  required: t('clusterDetail.troubleshoot.urlRequired')
                }}
              />
            </Box>
          </Box>

          <Box display="flex" flexDirection={'row'} mt={2}>
            <YBLabel width="300px" dataTestId="RegisterTSService-Label">
              {t('clusterDetail.troubleshoot.ybPlatformServiceUrlLabel')}
            </YBLabel>
            <Box flex={1}>
              <YBInputField
                control={control}
                name="ybaUrl"
                style={{ width: '300px' }}
                placeholder={t('clusterDetail.troubleshoot.urlPlaceholder')}
                type="text"
                rules={{
                  required: t('clusterDetail.troubleshoot.urlRequired')
                }}
              />
            </Box>
          </Box>

          <Box display="flex" flexDirection={'row'} mt={2}>
            <YBLabel width="300px" dataTestId="RegisterTSService-Label">
              {t('clusterDetail.troubleshoot.ybPlatformMetricsUrlLabel')}
            </YBLabel>
            <Box flex={1}>
              <YBInputField
                control={control}
                name="metricsUrl"
                style={{ width: '300px' }}
                placeholder={t('clusterDetail.troubleshoot.urlPlaceholder')}
                type="text"
                rules={{
                  required: t('clusterDetail.troubleshoot.urlRequired')
                }}
              />
            </Box>
          </Box>
        </Box>
        <Box mt={4}>
          <YBButton variant="primary" onClick={handleFormSubmit}>
            {t('clusterDetail.troubleshoot.registerButton')}
          </YBButton>
        </Box>
      </FormProvider>
    </Box>
  );
};
