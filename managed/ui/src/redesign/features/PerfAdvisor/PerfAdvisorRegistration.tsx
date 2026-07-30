import { Box } from '@material-ui/core';
import { useForm, FormProvider } from 'react-hook-form';
import { useMutation } from 'react-query';
import { useTranslation } from 'react-i18next';
import { toast } from 'react-toastify';
import { YBButton, YBInputField, YBLabel } from '../../components';
import { PerfAdvisorAPI } from './api';
import { IN_DEVELOPMENT_MODE } from '../../../config';

interface PerfAdvisorRegistrationProps {
  onRefetchConfig: () => void;
}

export const PerfAdvisorRegistration = ({ onRefetchConfig }: PerfAdvisorRegistrationProps) => {
  const { t } = useTranslation();
  const baseUrl = window.location.origin;

  // useForm hook definition
  const formMethods = useForm<any>({
    defaultValues: {
      paUrl: '',
      ybaUrl: IN_DEVELOPMENT_MODE ? 'http://localhost:9000' : baseUrl,
      metricsUrl: IN_DEVELOPMENT_MODE ? 'http://localhost:9090' : `${baseUrl}:9090`,
      metricsUsername: '',
      metricsPassword: '',
      apiToken: '',
      tpApiToken: '',
      metricsScrapePeriodSecs: 10
    },
    mode: 'onChange',
    reValidateMode: 'onChange'
  });
  const { control, handleSubmit } = formMethods;

  const registerTpService = useMutation(
    (payload: any) =>
      PerfAdvisorAPI.registerYBAToPerfAdvisor(
        payload.paUrl,
        payload.ybaUrl,
        payload.metricsUrl,
        payload.metricsUsername,
        payload.metricsPassword,
        payload.apiToken,
        payload.tpApiToken,
        payload.metricsScrapePeriodSecs
      ),
    {
      onSuccess: () => {
        onRefetchConfig();
        toast.success(t('clusterDetail.troubleshoot.registrationSuccess'));
      },
      onError: () => {
        toast.error(t('clusterDetail.troubleshoot.registrationFailed'));
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
          data-testid="PerfAdvisorRegistration-Container"
        >
          <Box display="flex" flexDirection={'row'} mt={2}>
            <YBLabel width="300px" dataTestId="RegisterTSService-Label">
              {t('clusterDetail.troubleshoot.paServiceUrlLabel')}
            </YBLabel>
            <Box flex={1}>
              <YBInputField
                control={control}
                name="paUrl"
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

          <Box display="flex" flexDirection={'row'} mt={2}>
            <YBLabel width="300px" dataTestId="RegisterTSService-Label">
              {t('clusterDetail.troubleshoot.ybPlatformMetricsUsernameLabel')}
            </YBLabel>
            <Box flex={1}>
              <YBInputField
                control={control}
                name="metricsUsername"
                style={{ width: '300px' }}
                type="text"
              />
            </Box>
          </Box>

          <Box display="flex" flexDirection={'row'} mt={2}>
            <YBLabel width="300px" dataTestId="RegisterTSService-Label">
              {t('clusterDetail.troubleshoot.ybPlatformMetricsPasswordLabel')}
            </YBLabel>
            <Box flex={1}>
              <YBInputField
                control={control}
                name="metricsPassword"
                style={{ width: '300px' }}
                type="text"
              />
            </Box>
          </Box>

          <Box display="flex" flexDirection={'row'} mt={2}>
            <YBLabel width="300px" dataTestId="RegisterTSService-Label">
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
            <YBLabel width="300px" dataTestId="RegisterTSService-Label">
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
        <Box mt={4}>
          <YBButton variant="primary" onClick={handleFormSubmit}>
            {t('clusterDetail.troubleshoot.registerButton')}
          </YBButton>
        </Box>
      </FormProvider>
    </Box>
  );
};
