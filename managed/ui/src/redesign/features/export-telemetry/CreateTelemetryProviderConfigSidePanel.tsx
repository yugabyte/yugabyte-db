import { FC } from 'react';
import { toast } from 'react-toastify';
import { useMutation, useQuery } from 'react-query';
import { useTranslation, Trans } from 'react-i18next';
import { useForm, FormProvider } from 'react-hook-form';
import { Box, Typography, MenuItem, Divider } from '@material-ui/core';
import clsx from 'clsx';

import {
  YBSidePanel,
  YBLabel,
  YBInputField,
  YBRadioGroupField,
  RadioGroupOrientation,
  YBSelect,
  YBTooltip,
  YBPasswordField
} from '../../components';
import { YBDropZoneField } from '../../../components/configRedesign/providerRedesign/components/YBDropZone/YBDropZoneField';
import { createErrorMessage } from '../universe/universe-form/utils/helpers';
import { readFileAsText } from '../../../components/configRedesign/providerRedesign/forms/utils';
import {
  ExportLogFormFields,
  TelemetryProviderType,
  ExportLogPayload,
  TelemetryProviderItem
} from './types';
import { DATADOG_SITES, LOKI_AUTH_TYPES } from './constants';
import { api, runtimeConfigQueryKey } from '../../helpers/api';
import InfoIcon from '../../assets/info-message.svg?img';

//RBAC
import { RuntimeConfigKey } from '../../helpers/constants';
import { hasNecessaryPerm } from '../rbac/common/RbacApiPermValidator';
import { ApiPermissionMap } from '../rbac/ApiAndUserPermMapping';
import { RBAC_ERR_MSG_NO_PERM } from '../rbac/common/validator/ValidatorUtils';

//styles
import { useExportTelemetryStyles } from './styles';
import { usePillStyles } from '@app/redesign/styles/styles';

interface CreateTelemetryProviderConfigSidePanelProps {
  open: boolean;
  onClose: () => void;
  formProps: TelemetryProviderItem | null;
}

const TRANSLATION_KEY_PREFIX = 'exportTelemetry';

export const CreateTelemetryProviderConfigSidePanel: FC<CreateTelemetryProviderConfigSidePanelProps> = ({
  open,
  onClose,
  formProps
}) => {
  const classes = useExportTelemetryStyles();
  const pillClasses = usePillStyles();
  const { t } = useTranslation('translation', { keyPrefix: TRANSLATION_KEY_PREFIX });
  const isViewMode = formProps !== null;
  const formDefaultValues = isViewMode
    ? formProps
    : {
        config: {
          type: TelemetryProviderType.DATA_DOG,
          site: DATADOG_SITES[0].value
        }
      };

  const formMethods = useForm<ExportLogFormFields>({
    defaultValues: formDefaultValues,
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
        toast.success('Created export configuration', data?.name);
        onClose();
      },
      onError: (error) => {
        toast.error(createErrorMessage(error));
      }
    }
  );

  const providerTypeValue = watch('config.type');
  const dataDogSiteValue = watch('config.site');
  const lokiAuthTypeValue = watch('config.authType');

  const runtimeConfigQuery = useQuery(runtimeConfigQueryKey.globalScope(), () =>
    api.fetchRuntimeConfigs()
  );
  const runtimeConfigEntries = runtimeConfigQuery.data.configEntries ?? [];
  const isLokiTelemetryEnabled =
    runtimeConfigEntries.find((config: any) => config.key === RuntimeConfigKey.LOKI_TELEMETRY_ALLOW)
      ?.value ?? false;

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
              toast.error(`An error occurred while parsing the service account JSON: ${error}`);
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
        payload.config.accessKey = values.config?.accessKey;
        payload.config.secretKey = values.config?.secretKey;
        payload.config.logGroup = values.config?.logGroup;
        payload.config.logStream = values.config?.logStream;
        payload.config.region = values.config?.region;
        payload.config.roleARN = values.config?.roleARN;
        payload.config.endpoint = values.config?.endpoint;
      }
      if (values.config.type === TelemetryProviderType.LOKI) {
        payload.config.endpoint = values.config?.endpoint;

        payload.config.organizationID = values.config?.organizationID;

        payload.config.authType = values.config?.authType;

        if (values.config.authType === 'BasicAuth') {
          payload.config.basicAuth = values.config?.basicAuth;
        }
      }
      if (values.config.type === TelemetryProviderType.DYNATRACE) {
        payload.config.endpoint = values.config?.endpoint;
        payload.config.apiToken = values.config?.apiToken;
      }
      return await createTelemetryProvider.mutateAsync(payload);
    } catch (e) {}
    return;
  });

  const renderDatadogForm = () => {
    const isSelfHosted = DATADOG_SITES.every((ds) => ds.value !== dataDogSiteValue);
    return (
      <>
        <Box display={'flex'} flexDirection={'column'} width={'100%'} mt={3}>
          <YBLabel>{t('dataDogApiKey')}</YBLabel>
          <YBInputField
            control={control}
            name="config.apiKey"
            fullWidth
            disabled={isViewMode}
            inputProps={{
              'data-testid': 'DatadogForm-APIKey'
            }}
          />
        </Box>
        <Box display={'flex'} flexDirection={'row'} mt={3}>
          <Box display={'flex'} flexShrink={1} width="200px" flexDirection={'column'}>
            <YBLabel>{t('dataDogSite')}</YBLabel>
            <YBSelect
              fullWidth
              value={dataDogSiteValue}
              onChange={(e) => setValue('config.site', e.target.value)}
              renderValue={() =>
                DATADOG_SITES.find((ds) => ds.value === dataDogSiteValue)?.name ?? ' Self-hosted'
              }
              disabled={isViewMode}
              inputProps={{
                'data-testid': 'DatadogForm-SiteSelect'
              }}
            >
              {DATADOG_SITES.map((item) => (
                <MenuItem
                  key={item.value}
                  value={item.value}
                  className={classes.datadogMenuItem}
                  data-testid={`DatadogForm-${item.value}`}
                >
                  <Typography variant="body1">{item.name}</Typography>
                  <Typography variant="subtitle1" color="textSecondary">
                    {item.value}
                  </Typography>
                </MenuItem>
              ))}
              <Divider />
              <MenuItem
                key={'selfHosted'}
                value={''}
                className={classes.datadogMenuItem}
                data-testid={`DatadogForm-SelfHosted`}
              >
                <Typography variant="body1">{'Self-hosted'}</Typography>
                <Typography variant="subtitle1" color="textSecondary">
                  {t('dataDogURLPlaceholder')}
                </Typography>
              </MenuItem>
            </YBSelect>
          </Box>
          <Box display={'flex'} ml={2} width="100%" flexDirection={'column'}>
            <YBLabel>{t('siteURL')}</YBLabel>
            <YBInputField
              control={control}
              name="config.site"
              disabled={!isSelfHosted || isViewMode}
              fullWidth
              inputProps={{
                'data-testid': 'DatadogForm-SiteInput'
              }}
            />
          </Box>
        </Box>
      </>
    );
  };

  const renderSplunkForm = () => {
    return (
      <>
        <Box display={'flex'} flexDirection={'column'} width={'100%'} mt={3}>
          <YBLabel>{t('splunkToken')}</YBLabel>
          <YBInputField
            control={control}
            name="config.token"
            fullWidth
            disabled={isViewMode}
            inputProps={{
              'data-testid': 'SplunkForm-Token'
            }}
          />
        </Box>
        <Box display={'flex'} flexDirection={'column'} width={'100%'} mt={3}>
          <YBLabel>{t('endpointURL')}</YBLabel>
          <YBInputField
            control={control}
            name="config.endpoint"
            placeholder="https://"
            fullWidth
            disabled={isViewMode}
            inputProps={{
              'data-testid': 'SplunkForm-EndPoint'
            }}
          />
        </Box>
        <Box display={'flex'} flexDirection={'column'} width={'100%'} mt={3}>
          <YBLabel>{t('source')}</YBLabel>
          <YBInputField
            control={control}
            name="config.source"
            fullWidth
            disabled={isViewMode}
            inputProps={{
              'data-testid': 'SplunkForm-Source'
            }}
          />
        </Box>
        <Box display={'flex'} flexDirection={'column'} width={'100%'} mt={3}>
          <YBLabel>{t('sourceType')}</YBLabel>
          <YBInputField
            control={control}
            name="config.sourceType"
            fullWidth
            disabled={isViewMode}
            inputProps={{
              'data-testid': 'SplunkForm-SourceType'
            }}
          />
        </Box>
        <Box display={'flex'} flexDirection={'column'} width={'100%'} mt={3}>
          <YBLabel>{t('splunkIndex')}</YBLabel>
          <YBInputField
            control={control}
            name="config.index"
            fullWidth
            disabled={isViewMode}
            inputProps={{
              'data-testid': 'SplunkForm-Index'
            }}
          />
        </Box>
      </>
    );
  };

  const renderLokiForm = () => {
    return (
      <>
        <Box display={'flex'} flexDirection={'column'} width={'100%'} mt={3}>
          <Box display={'flex'} flexDirection={'row'} alignItems={'center'} mt={4}>
            <YBLabel width="200px">
              {t('lokiEndpoint')} &nbsp;
              <YBTooltip title={t('lokiEndpointTooltip')}>
                <img src={InfoIcon} />
              </YBTooltip>
            </YBLabel>
          </Box>
          <YBInputField
            control={control}
            name="config.endpoint"
            placeholder={t('lokiEndpointPlaceholder')}
            fullWidth
            disabled={isViewMode}
            inputProps={{
              'data-testid': 'LokiForm-EndPoint'
            }}
            required={true}
            rules={{ required: 'This field is required' }}
          />
        </Box>
        <Box display={'flex'} flexDirection={'column'} width={'100%'} mt={3}>
          <Box display={'flex'} flexDirection={'row'} alignItems={'center'} mt={3}>
            <Typography variant="body2">{t('lokiOrganizationID')} &nbsp;</Typography>
            <YBTooltip title={t('lokiOrganizationIDTooltip')}>
              <img src={InfoIcon} />
            </YBTooltip>
          </Box>
          <YBInputField
            control={control}
            name="config.organizationID"
            fullWidth
            disabled={isViewMode}
            inputProps={{
              'data-testid': 'LokiForm-OrganizationID'
            }}
          />
        </Box>
        {/* Auth Type Selection */}
        <Box className={classes.mainFieldContainer}>
          <Typography className={classes.exportToTitle}>{t('authType')}</Typography>
          <YBRadioGroupField
            name="config.authType"
            control={control}
            options={LOKI_AUTH_TYPES}
            orientation={RadioGroupOrientation.VERTICAL}
            isDisabled={isViewMode}
          />
          {lokiAuthTypeValue === 'BasicAuth' && renderLokiBasicAuthForm()}
        </Box>
      </>
    );
  };

  const renderLokiBasicAuthForm = () => {
    {
      /* Username/Password Fields (Only when Basic Auth selected) */
    }
    return (
      <>
        <Box display="flex" flexDirection="column" width="100%" mt={3}>
          <YBLabel>{t('lokiUsername')}</YBLabel>
          <YBInputField
            control={control}
            name="config.basicAuth.username"
            fullWidth
            disabled={isViewMode}
            inputProps={{
              'data-testid': 'LokiForm-Username'
            }}
            required={true}
            rules={{ required: 'This field is required' }}
          />
        </Box>
        <Box display="flex" flexDirection="column" width="100%" mt={3}>
          <YBLabel>{t('lokiPassword')}</YBLabel>
          <YBInputField
            control={control}
            name="config.basicAuth.password"
            fullWidth
            disabled={isViewMode}
            inputProps={{
              'data-testid': 'LokiForm-Password'
            }}
            required={true}
            rules={{ required: 'This field is required' }}
          />
        </Box>
      </>
    );
  };

  const renderAWSWatchForm = () => {
    return (
      <>
        <Box display={'flex'} flexDirection={'column'} width={'100%'} mt={3}>
          <YBLabel>{t('awsAccessKey')}</YBLabel>
          <YBInputField
            control={control}
            rules={{ required: 'This field is required' }}
            name="config.accessKey"
            fullWidth
            disabled={isViewMode}
            inputProps={{
              'data-testid': 'AWSWatchForm-AccessKey'
            }}
          />
        </Box>
        <Box display={'flex'} flexDirection={'column'} width={'100%'} mt={3}>
          <YBLabel>{t('awsSecretKey')}</YBLabel>
          <YBInputField
            control={control}
            rules={{ required: 'This field is required' }}
            name="config.secretKey"
            fullWidth
            disabled={isViewMode}
            inputProps={{
              'data-testid': 'AWSWatchForm-SecretKey'
            }}
          />
        </Box>
        <Box display={'flex'} flexDirection={'column'} width={'100%'} mt={3}>
          <YBLabel>{t('logGroup')}</YBLabel>
          <YBInputField
            control={control}
            rules={{ required: 'This field is required' }}
            name="config.logGroup"
            fullWidth
            disabled={isViewMode}
            inputProps={{
              'data-testid': 'AWSWatchForm-LogGroup'
            }}
          />
        </Box>
        <Box display={'flex'} flexDirection={'column'} width={'100%'} mt={3}>
          <YBLabel>{t('logStream')}</YBLabel>
          <YBInputField
            control={control}
            rules={{ required: 'This field is required' }}
            name="config.logStream"
            fullWidth
            disabled={isViewMode}
            inputProps={{
              'data-testid': 'AWSWatchForm-LogStream'
            }}
          />
        </Box>
        <Box display={'flex'} flexDirection={'column'} width={'100%'} mt={3}>
          <YBLabel>{t('awsRegion')}</YBLabel>
          <YBInputField
            control={control}
            rules={{ required: 'This field is required' }}
            name="config.region"
            fullWidth
            disabled={isViewMode}
            inputProps={{
              'data-testid': 'AWSWatchForm-Region'
            }}
          />
        </Box>
        <Box display={'flex'} flexDirection={'column'} width={'100%'} mt={3}>
          <YBLabel>{t('awsRoleARN')}</YBLabel>
          <YBInputField
            control={control}
            name="config.roleARN"
            fullWidth
            disabled={isViewMode}
            inputProps={{
              'data-testid': 'AWSWatchForm-RoleARN'
            }}
          />
        </Box>
        <Box display={'flex'} flexDirection={'column'} width={'100%'} mt={3}>
          <YBLabel>{t('awsEndpoint')}</YBLabel>
          <YBInputField
            control={control}
            name="config.endpoint"
            placeholder="https://"
            fullWidth
            disabled={isViewMode}
            inputProps={{
              'data-testid': 'AWSWatchForm-EndPoint'
            }}
          />
        </Box>
      </>
    );
  };

  const renderGCPCloudForm = () => {
    return (
      <>
        <Box display={'flex'} flexDirection={'column'} width={'100%'} mt={3}>
          <YBLabel>{t('gcpProject')}</YBLabel>
          <YBInputField
            control={control}
            name="config.project"
            fullWidth
            disabled={isViewMode}
            inputProps={{
              'data-testid': 'GCPCloudForm-Project'
            }}
          />
        </Box>
        <Box display={'flex'} flexDirection={'column'} mt={3} width="100%">
          <YBDropZoneField
            name="config.gcpCredentials"
            control={control}
            actionButtonText={t('uploadGCPCredentails')}
            multipleFiles={false}
            showHelpText={true}
            className={classes.gcpJSONUploader}
            disabled={isViewMode}
            descriptionText={
              <span>
                <Trans i18nKey={`${TRANSLATION_KEY_PREFIX}.gcpDescription`} />
              </span>
            }
          />
        </Box>
      </>
    );
  };

  /**
   * Tech debt: For consistency this follows the existing render<telemetryProvider>Form
   * pattern where each of the form fields are returned by a function in this React
   * component.
   * Given we are supporting more providers, we should refactor this so each provider
   * integration is a separate file.
   * Opened a ticket to track this refactoring: PLAT-18780
   */
  const renderDynatraceForm = () => {
    return (
      <>
        <Box display={'flex'} flexDirection={'column'} mt={3} width="100%">
          <YBLabel className={classes.ybLabel}>
            {t('dynatrace.endpointUrl')}
            <YBTooltip
              title={
                <Trans
                  i18nKey={`${TRANSLATION_KEY_PREFIX}.dynatrace.endpointUrlTooltip`}
                  components={{ bold: <b /> }}
                />
              }
            >
              <img src={InfoIcon} />
            </YBTooltip>
          </YBLabel>
          <YBInputField
            control={control}
            name="config.endpoint"
            fullWidth
            disabled={isViewMode}
            inputProps={{
              'data-testid': 'DynatraceForm-EndpointUrl'
            }}
            required={true}
            placeholder="https://<ENVIRONMENT_ID>.live.dynatrace.com/"
            rules={{
              required: 'This field is required',
              validate: {
                validUrl: (value) => {
                  if (!value || typeof value !== 'string') {
                    return t('dynatrace.endpointUrlValidationError');
                  }
                  try {
                    const url = new URL(value);
                    if (!url.protocol || !url.hostname) {
                      return t('dynatrace.endpointUrlValidationError');
                    }
                    return true;
                  } catch (error) {
                    return t('dynatrace.endpointUrlValidationError');
                  }
                }
              }
            }}
          />
        </Box>
        <Box display="flex" flexDirection="column" width="100%" mt={3}>
          <YBLabel className={classes.ybLabel}>
            {t('dynatrace.apiToken')}
            <YBTooltip
              title={
                <Trans
                  i18nKey={`${TRANSLATION_KEY_PREFIX}.dynatrace.apiTokenTooltip`}
                  components={{ bold: <b /> }}
                />
              }
            >
              <img src={InfoIcon} />
            </YBTooltip>
          </YBLabel>
          <YBPasswordField
            control={control}
            name="config.apiToken"
            fullWidth
            disabled={isViewMode}
            inputProps={{
              'data-testid': 'DynatraceForm-AccessToken'
            }}
            required={true}
            rules={{
              required: 'This field is required',
              pattern: {
                value: /^[a-zA-Z0-9]{6}\.[a-zA-Z0-9]{24}\.[a-zA-Z0-9]{64}$/,
                message: t('dynatrace.apiTokenValidationError')
              }
            }}
          />
          <Box marginTop={1}>
            <Typography variant="subtitle1">
              <Trans
                i18nKey={`${TRANSLATION_KEY_PREFIX}.dynatrace.apiTokenHelper`}
                components={{ bold: <b /> }}
              />
            </Typography>
          </Box>
        </Box>
      </>
    );
  };

  const canCreateTelemetryProvider = hasNecessaryPerm(ApiPermissionMap.CREATE_TELEMETRY_PROVIDER);

  const TELEMETRY_PROVIDER_OPTIONS = [
    {
      label: (
        <div className={classes.telemetryRadioOptionLabel}>
          <Typography variant="body2">{t('telemetryProviderOption.label.datadog')}</Typography>
          <div className={classes.pillContainer}>
            <div className={clsx(pillClasses.pill, classes.exportSupportPill)}>
              {t('telemetryProviderOption.exportSupport.metrics')}
            </div>
            <div className={clsx(pillClasses.pill, classes.exportSupportPill)}>
              {t('telemetryProviderOption.exportSupport.logs')}
            </div>
          </div>
        </div>
      ),
      value: TelemetryProviderType.DATA_DOG
    },
    {
      label: (
        <div className={classes.telemetryRadioOptionLabel}>
          <Typography variant="body2">{t('telemetryProviderOption.label.splunk')}</Typography>
          <div className={classes.pillContainer}>
            <div className={clsx(pillClasses.pill, classes.exportSupportPill)}>
              {t('telemetryProviderOption.exportSupport.logs')}
            </div>
          </div>
        </div>
      ),
      value: TelemetryProviderType.SPLUNK
    },
    {
      label: (
        <div className={classes.telemetryRadioOptionLabel}>
          <Typography variant="body2">
            {t('telemetryProviderOption.label.awsCloudWatch')}
          </Typography>
          <div className={classes.pillContainer}>
            <div className={clsx(pillClasses.pill, classes.exportSupportPill)}>
              {t('telemetryProviderOption.exportSupport.logs')}
            </div>
          </div>
        </div>
      ),
      value: TelemetryProviderType.AWS_CLOUDWATCH
    },
    {
      label: (
        <div className={classes.telemetryRadioOptionLabel}>
          <Typography variant="body2">
            {t('telemetryProviderOption.label.gcpCloudMonitoring')}
          </Typography>
          <div className={classes.pillContainer}>
            <div className={clsx(pillClasses.pill, classes.exportSupportPill)}>
              {t('telemetryProviderOption.exportSupport.logs')}
            </div>
          </div>
        </div>
      ),
      value: TelemetryProviderType.GCP_CLOUD_MONITORING
    },
    {
      label: (
        <div className={classes.telemetryRadioOptionLabel}>
          <Typography variant="body2">{t('telemetryProviderOption.label.loki')}</Typography>
          <div className={classes.pillContainer}>
            <div className={clsx(pillClasses.pill, classes.exportSupportPill)}>
              {t('telemetryProviderOption.exportSupport.logs')}
            </div>
          </div>
        </div>
      ),
      value: TelemetryProviderType.LOKI
    },
    {
      label: (
        <div className={classes.telemetryRadioOptionLabel}>
          <Typography variant="body2">{t('telemetryProviderOption.label.dynatrace')}</Typography>
          <div className={classes.pillContainer}>
            <div className={clsx(pillClasses.pill, classes.exportSupportPill)}>
              {t('telemetryProviderOption.exportSupport.metrics')}
            </div>
          </div>
        </div>
      ),
      value: TelemetryProviderType.DYNATRACE
    }
  ];
  return (
    <YBSidePanel
      open={open}
      title={isViewMode ? t('viewExportTelemetryProviderModalTitle') : t('modalTitle')}
      submitLabel={!isViewMode ? t('submitLabel') : undefined}
      onClose={onClose}
      cancelLabel={
        isViewMode ? t('close', { keyPrefix: 'common' }) : t('cancel', { keyPrefix: 'common' })
      }
      overrideWidth={680}
      onSubmit={handleFormSubmit}
      submitTestId="ExportLogModalForm-Submit"
      cancelTestId="ExportLogModalForm-Cancel"
      buttonProps={{
        primary: {
          disabled: isViewMode || !canCreateTelemetryProvider
        }
      }}
      submitButtonTooltip={!canCreateTelemetryProvider ? RBAC_ERR_MSG_NO_PERM : ''}
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
          <YBLabel>{t('exportName')}</YBLabel>
          <Box display={'flex'} width={'384px'} mt={0.5}>
            <YBInputField
              control={control}
              rules={{ required: 'This field is required' }}
              name="name"
              placeholder="config_name_01"
              fullWidth
              disabled={isViewMode}
              inputProps={{
                'data-testid': 'ExportLogModalForm-ConfigName'
              }}
            />
          </Box>
          <Box className={classes.mainFieldContainer}>
            <Typography className={classes.exportToTitle}>{t('exportTo')}</Typography>
            <YBRadioGroupField
              name="config.type"
              control={control}
              options={TELEMETRY_PROVIDER_OPTIONS}
              orientation={RadioGroupOrientation.VERTICAL}
              isDisabled={isViewMode}
            />
            {providerTypeValue === TelemetryProviderType.DATA_DOG && renderDatadogForm()}
            {providerTypeValue === TelemetryProviderType.SPLUNK && renderSplunkForm()}
            {providerTypeValue === TelemetryProviderType.AWS_CLOUDWATCH && renderAWSWatchForm()}
            {providerTypeValue === TelemetryProviderType.GCP_CLOUD_MONITORING &&
              renderGCPCloudForm()}
            {providerTypeValue === TelemetryProviderType.LOKI &&
              isLokiTelemetryEnabled &&
              renderLokiForm()}
            {providerTypeValue === TelemetryProviderType.DYNATRACE && renderDynatraceForm()}
          </Box>
        </Box>
      </FormProvider>
    </YBSidePanel>
  );
};
