import { FC } from 'react';
import { toast } from 'react-toastify';
import { useMutation } from 'react-query';
import { useTranslation, Trans } from 'react-i18next';
import { useForm, FormProvider } from 'react-hook-form';
import { Box, Typography, MenuItem, Divider } from '@material-ui/core';
import {
  YBSidePanel,
  YBLabel,
  YBInputField,
  YBRadioGroupField,
  RadioGroupOrientation,
  YBSelect
} from '../../../components';
import { YBDropZoneField } from '../../../../components/configRedesign/providerRedesign/components/YBDropZone/YBDropZoneField';
import { api } from '../../../utils/api';
import { createErrorMessage } from '../../universe/universe-form/utils/helpers';
import { readFileAsText } from '../../../../components/configRedesign/providerRedesign/forms/utils';
import { ExportLogFormFields, TelemetryProviderType, ExportLogPayload } from '../utils/types';
import { TELEMETRY_PROVIDER_OPTIONS, DATADOG_SITES } from '../utils/constants';

//styles
import { exportLogStyles } from '../utils/ExportLogStyles';

interface ExportLogFormProps {
  open: boolean;
  onClose: () => void;
}

export const ExportLogModalForm: FC<ExportLogFormProps> = ({ open, onClose }) => {
  const classes = exportLogStyles();
  const { t } = useTranslation();

  const formMethods = useForm<ExportLogFormFields>({
    defaultValues: {
      config: {
        type: TelemetryProviderType.DATA_DOG,
        site: DATADOG_SITES[0].value
      }
    },
    mode: 'onChange',
    reValidateMode: 'onChange'
  });

  const { control, handleSubmit, watch, setValue } = formMethods;

  const createTelemetryProvider = useMutation(
    (values: ExportLogPayload) => {
      return api.createTelemetryProvider(values);
    },
    {
      onSuccess: (data: any) => {
        toast.success('Create export configuration', data?.name);
        onClose();
      },
      onError: (error) => {
        toast.error(createErrorMessage(error));
      }
    }
  );

  const providerTypeValue = watch('config.type');
  const dataDogSiteValue = watch('config.site');

  const handleFormSubmit = handleSubmit(async (values) => {
    try {
      const payload: ExportLogPayload = {
        name: values.name,
        config: {
          type: values.config.type
        }
      };
      if (values.config.type === TelemetryProviderType.DATA_DOG) {
        payload.config.site = values.config?.site;
        payload.config.apiKey = values.config?.apiKey;
      }
      if (values.config.type === TelemetryProviderType.GCP_CLOUD_MONITORING) {
        payload.config.project = values.config?.project;
        if (values.config?.gcpCredentials) {
          const jsonVal = await readFileAsText(values.config.gcpCredentials);
          if (jsonVal) {
            try {
              payload.config.credentials = JSON.parse(jsonVal);
            } catch (error) {
              toast.error(`An error occured while parsing the service account JSON: ${error}`);
              return;
            }
          }
        }
      }
      if (values.config.type === TelemetryProviderType.SPLUNK) {
        payload.config.endpoint = values.config?.endpoint;
        payload.config.token = values.config?.token;
        payload.config.source = values.config?.source;
        payload.config.sourceType = values.config?.sourceType;
        payload.config.index = values.config?.index;
      }
      if (values.config.type === TelemetryProviderType.AWS_CLOUDWATCH) {
        payload.config.awsAccessKeyID = values.config?.awsAccessKeyID;
        payload.config.awsAccessKeySecret = values.config?.awsAccessKeySecret;
        payload.config.logGroup = values.config?.logGroup;
        payload.config.logStream = values.config?.logStream;
        payload.config.region = values.config?.region;
        payload.config.roleARN = values.config?.roleARN;
        payload.config.endpoint = values.config?.endpoint;
      }
      await createTelemetryProvider.mutateAsync(payload);
    } catch (e) {
      toast.error(createErrorMessage(e));
    }
  });

  const renderDatadogForm = () => {
    const isSelfHosted = DATADOG_SITES.every((ds) => ds.value !== dataDogSiteValue);
    return (
      <>
        <Box display={'flex'} flexDirection={'column'} width={'100%'} mt={3}>
          <YBLabel>{t('exportAuditLog.dataDogApiKey')}</YBLabel>
          <YBInputField control={control} name="config.apiKey" fullWidth />
        </Box>
        <Box display={'flex'} flexDirection={'row'} mt={3}>
          <Box display={'flex'} flexShrink={1} width="200px" flexDirection={'column'}>
            <YBLabel>{t('exportAuditLog.dataDogSite')}</YBLabel>
            <YBSelect
              fullWidth
              value={dataDogSiteValue}
              onChange={(e) => setValue('config.site', e.target.value)}
              renderValue={() =>
                DATADOG_SITES.find((ds) => ds.value === dataDogSiteValue)?.name ?? ' Self-hosted'
              }
            >
              {DATADOG_SITES.map((item) => (
                <MenuItem key={item.value} value={item.value} className={classes.dataDogmenuItem}>
                  <Typography variant="body1">{item.name}</Typography>
                  <Typography variant="subtitle1" color="textSecondary">
                    {item.value}
                  </Typography>
                </MenuItem>
              ))}
              <Divider />
              <MenuItem key={'selfHosted'} value={''} className={classes.dataDogmenuItem}>
                <Typography variant="body1">{'Self-hosted'}</Typography>
                <Typography variant="subtitle1" color="textSecondary">
                  {t('exportAuditLog.dataDogURLPlaceholder')}
                </Typography>
              </MenuItem>
            </YBSelect>
          </Box>
          <Box display={'flex'} ml={2} width="100%" flexDirection={'column'}>
            <YBLabel>{t('exportAuditLog.siteURL')}</YBLabel>
            <YBInputField control={control} name="config.site" disabled={!isSelfHosted} fullWidth />
          </Box>
        </Box>
      </>
    );
  };

  const renderSplunkForm = () => {
    return (
      <>
        <Box display={'flex'} flexDirection={'column'} width={'100%'} mt={3}>
          <YBLabel>{t('exportAuditLog.splunkToken')}</YBLabel>
          <YBInputField control={control} name="config.token" fullWidth />
        </Box>
        <Box display={'flex'} flexDirection={'column'} width={'100%'} mt={3}>
          <YBLabel>{t('exportAuditLog.endpointURL')}</YBLabel>
          <YBInputField control={control} name="config.endpoint" placeholder="https://" fullWidth />
        </Box>
        <Box display={'flex'} flexDirection={'column'} width={'100%'} mt={3}>
          <YBLabel>{t('exportAuditLog.source')}</YBLabel>
          <YBInputField control={control} name="config.source" fullWidth />
        </Box>
        <Box display={'flex'} flexDirection={'column'} width={'100%'} mt={3}>
          <YBLabel>{t('exportAuditLog.sourceType')}</YBLabel>
          <YBInputField control={control} name="config.sourceType" fullWidth />
        </Box>
        <Box display={'flex'} flexDirection={'column'} width={'100%'} mt={3}>
          <YBLabel>{t('exportAuditLog.splunkIndex')}</YBLabel>
          <YBInputField control={control} name="config.index" fullWidth />
        </Box>
      </>
    );
  };

  const renderAWSWatchForm = () => {
    return (
      <>
        <Box display={'flex'} flexDirection={'column'} width={'100%'} mt={3}>
          <YBLabel>{t('exportAuditLog.awsAccessKey')}</YBLabel>
          <YBInputField control={control} name="config.awsAccessKeyID" fullWidth />
        </Box>
        <Box display={'flex'} flexDirection={'column'} width={'100%'} mt={3}>
          <YBLabel>{t('exportAuditLog.awsSecretKey')}</YBLabel>
          <YBInputField control={control} name="config.awsAccessKeySecret" fullWidth />
        </Box>
        <Box display={'flex'} flexDirection={'column'} width={'100%'} mt={3}>
          <YBLabel>{t('exportAuditLog.logGroup')}</YBLabel>
          <YBInputField control={control} name="config.logGroup" fullWidth />
        </Box>
        <Box display={'flex'} flexDirection={'column'} width={'100%'} mt={3}>
          <YBLabel>{t('exportAuditLog.logStream')}</YBLabel>
          <YBInputField control={control} name="config.logStream" fullWidth />
        </Box>
        <Box display={'flex'} flexDirection={'column'} width={'100%'} mt={3}>
          <YBLabel>{t('exportAuditLog.awsRegion')}</YBLabel>
          <YBInputField control={control} name="config.region" fullWidth />
        </Box>
        <Box display={'flex'} flexDirection={'column'} width={'100%'} mt={3}>
          <YBLabel>{t('exportAuditLog.awsRoleARN')}</YBLabel>
          <YBInputField control={control} name="config.roleARN" fullWidth />
        </Box>
        <Box display={'flex'} flexDirection={'column'} width={'100%'} mt={3}>
          <YBLabel>{t('exportAuditLog.awsEndpoint')}</YBLabel>
          <YBInputField control={control} name="config.endpoint" placeholder="https://" fullWidth />
        </Box>
      </>
    );
  };

  const renderGCPCloudForm = () => {
    return (
      <>
        <Box display={'flex'} flexDirection={'column'} width={'100%'} mt={3}>
          <YBLabel>{t('exportAuditLog.gcpProject')}</YBLabel>
          <YBInputField control={control} name="config.project" fullWidth />
        </Box>
        <Box display={'flex'} flexDirection={'column'} mt={3} width="100%">
          <YBDropZoneField
            name="config.gcpCredentials"
            control={control}
            actionButtonText={t('exportAuditLog.uploadGCPCredentails')}
            multipleFiles={false}
            showHelpText={true}
            className={classes.gcpJSONUploader}
            descriptionText={
              <span>
                <Trans i18nKey={'exportAuditLog.gcpDescription'} />
              </span>
            }
          />
        </Box>
      </>
    );
  };

  return (
    <YBSidePanel
      open={open}
      title={t('exportAuditLog.modalTitle')}
      submitLabel={t('exportAuditLog.submitLabel')}
      onClose={onClose}
      cancelLabel={t('common.cancel')}
      overrideWidth={680}
      onSubmit={handleFormSubmit}
    >
      <FormProvider {...formMethods}>
        <Box
          height="100%"
          width="100%"
          display="flex"
          flexDirection={'column'}
          pl={1}
          pt={1}
          pr={1}
          mb={3}
        >
          <YBLabel>{t('exportAuditLog.exportName')}</YBLabel>
          <Box display={'flex'} width={'384px'} mt={0.5}>
            <YBInputField
              control={control}
              rules={{ required: 'This field is required' }}
              name="name"
              placeholder="config_name_01"
              fullWidth
            />
          </Box>
          <Box className={classes.mainFieldContainer}>
            <Typography className={classes.exportToTitle}>
              {t('exportAuditLog.exportTo')}
            </Typography>
            <YBRadioGroupField
              name="config.type"
              control={control}
              options={TELEMETRY_PROVIDER_OPTIONS}
              orientation={RadioGroupOrientation.VERTICAL}
            />
            {providerTypeValue === TelemetryProviderType.DATA_DOG && renderDatadogForm()}
            {providerTypeValue === TelemetryProviderType.SPLUNK && renderSplunkForm()}
            {providerTypeValue === TelemetryProviderType.AWS_CLOUDWATCH && renderAWSWatchForm()}
            {providerTypeValue === TelemetryProviderType.GCP_CLOUD_MONITORING &&
              renderGCPCloudForm()}
          </Box>
        </Box>
      </FormProvider>
    </YBSidePanel>
  );
};
