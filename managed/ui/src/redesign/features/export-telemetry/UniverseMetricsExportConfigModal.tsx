import { Divider, makeStyles, Typography } from '@material-ui/core';
import { useForm } from 'react-hook-form';
import { useTranslation } from 'react-i18next';
import { toast } from 'react-toastify';
import { useMutation, useQuery, useQueryClient } from 'react-query';
import { AxiosError } from 'axios';

import { YBModal, YBModalProps } from '../../components/YBModal/YBModal';
import { YBToggleField } from '../../components';
import { api, universeQueryKey } from '@app/redesign/helpers/api';
import { getIsMetricsExportSupported, getUniverseMetricsExportConfig } from './utils';
import { configureMetricsExport } from '@app/v2/api/universe/universe';
import { ConfigureMetricsExportReqBody } from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';
import { TelemetryProviderConfigSelectField } from './telemetryProviderConfigSelect/TelemetryProviderConfigSelectField';
import { handleServerError } from '@app/utils/errorHandlingUtils';
import { fetchTaskUntilItCompletes } from '@app/actions/xClusterReplication';

import toastStyles from '../../styles/toastStyles.module.scss';

interface UniverseMetricsExportConfigModalProps {
  universeUuid: string;
  modalProps: YBModalProps;
}

interface FormValues {
  shouldExportMetrics: boolean;
  telemetryConfigUuid: string;
}

const useStyles = makeStyles((theme) => ({
  formContainer: {
    display: 'flex',
    flexDirection: 'column',
    height: '100%'
  },
  infoBanner: {
    marginBottom: theme.spacing(3)
  },
  contentContainer: {
    display: 'flex',
    flexDirection: 'column',
    gap: theme.spacing(2),

    padding: theme.spacing(2, 2, 3, 2),

    backgroundColor: theme.palette.ybacolors.backgroundGrayLightest,
    border: `1px solid ${theme.palette.ybacolors.ybBorderGray}`,
    borderRadius: theme.shape.borderRadius
  },
  toggleContainer: {
    display: 'flex',
    justifyContent: 'space-between',
    alignItems: 'flex-start'
  },
  toggleContent: {
    display: 'flex',
    flexDirection: 'column',
    gap: theme.spacing(1)
  },
  toggleTitle: {
    color: '#151730',
    fontSize: 13,
    fontWeight: 600,
    lineHeight: '16px'
  },
  nestedSection: {
    marginLeft: theme.spacing(4)
  },
  telemetryProviderConfigField: {
    display: 'flex',
    flexDirection: 'column',
    gap: theme.spacing(0.75)
  },
  fieldLabel: {
    color: theme.palette.ybacolors.textDarkGray
  }
}));

const MetricsExportConfigurationRequest = {
  ENABLE: 'enable',
  DISABLE: 'disable',
  UPDATE: 'update'
} as const;
type MetricsExportConfigurationRequest = typeof MetricsExportConfigurationRequest[keyof typeof MetricsExportConfigurationRequest];

const MODAL_NAME = 'UniverseMetricsExporterConfigModal';
const TRANSLATION_KEY_PREFIX = 'exportTelemetry.universeExportMetricModal';

export const UniverseMetricsExportConfigModal = ({
  universeUuid,
  modalProps
}: UniverseMetricsExportConfigModalProps) => {
  const { t } = useTranslation('translation', { keyPrefix: TRANSLATION_KEY_PREFIX });
  const classes = useStyles();
  const queryClient = useQueryClient();

  const universeQuery = useQuery(universeQueryKey.detail(universeUuid), () =>
    api.fetchUniverse(universeUuid)
  );
  const universeMetricsExportConfig = universeQuery.data
    ? getUniverseMetricsExportConfig(universeQuery.data)
    : undefined;
  const defaultMetricsExportConfigUuid = universeMetricsExportConfig?.exporterUuid;
  const configureMetricsExportMutation = useMutation(
    (formValues: FormValues) => {
      const requestBody: ConfigureMetricsExportReqBody = {
        install_otel_collector:
          formValues.shouldExportMetrics &&
          !universeQuery.data?.universeDetails.otelCollectorEnabled,
        metrics_export_config: {
          universe_metrics_exporter_config: formValues.shouldExportMetrics
            ? [{ exporter_uuid: formValues.telemetryConfigUuid ?? '' }]
            : []
        }
      };
      return configureMetricsExport(universeUuid, requestBody);
    },
    {
      onSuccess: async (response, formValues) => {
        const request = getMetricsExportConfigurationRequest(
          defaultMetricsExportConfigUuid,
          formValues.telemetryConfigUuid
        );
        const invalidateQueries = () => {
          queryClient.invalidateQueries(universeQueryKey.detail(universeUuid));
        };
        const handleTaskCompletion = (error: boolean) => {
          if (error) {
            toast.error(
              <span className={toastStyles.toastMessage}>
                <i className="fa fa-exclamation-circle" />
                <Typography variant="body2" component="span">
                  {request === MetricsExportConfigurationRequest.ENABLE
                    ? t('toast.enableFailed')
                    : request === MetricsExportConfigurationRequest.DISABLE
                    ? t('toast.disableFailed')
                    : t('toast.updateFailed')}
                </Typography>
                <a href={`/tasks/${response.task_uuid}`} rel="noopener noreferrer" target="_blank">
                  {t('viewDetails', { keyPrefix: 'task' })}
                </a>
              </span>
            );
          } else {
            toast.success(
              <Typography variant="body2" component="span">
                {request === MetricsExportConfigurationRequest.ENABLE
                  ? t('toast.enableSuccess')
                  : request === MetricsExportConfigurationRequest.DISABLE
                  ? t('toast.disableSuccess')
                  : t('toast.updateSuccess')}
              </Typography>
            );
          }
          invalidateQueries();
        };

        modalProps.onClose();
        if (response.task_uuid) {
          fetchTaskUntilItCompletes(response.task_uuid, handleTaskCompletion, invalidateQueries);
        }
      },
      onError: (error: Error | AxiosError, formValues) => {
        const request = getMetricsExportConfigurationRequest(
          defaultMetricsExportConfigUuid,
          formValues.telemetryConfigUuid
        );
        handleServerError(error, {
          customErrorLabel:
            request === MetricsExportConfigurationRequest.ENABLE
              ? t('toast.enableRequestFailed')
              : request === MetricsExportConfigurationRequest.DISABLE
              ? t('toast.disableRequestFailed')
              : t('toast.updateRequestFailed')
        });
      }
    }
  );

  const formMethods = useForm<FormValues>({
    defaultValues: {
      shouldExportMetrics: !!defaultMetricsExportConfigUuid,
      telemetryConfigUuid: defaultMetricsExportConfigUuid
    }
  });

  const onSubmit = (formValues: FormValues) => {
    configureMetricsExportMutation.mutateAsync(formValues);
  };

  const shouldExportMetrics = formMethods.watch('shouldExportMetrics');
  const isFormDisabled = false;
  return (
    <YBModal
      title={t('title')}
      submitLabel={t('applyChanges', { keyPrefix: 'common' })}
      cancelLabel={t('cancel', { keyPrefix: 'common' })}
      onSubmit={formMethods.handleSubmit(onSubmit)}
      isSubmitting={formMethods.formState.isSubmitting}
      size="md"
      submitTestId={`${MODAL_NAME}-SubmitButton`}
      cancelTestId={`${MODAL_NAME}-CancelButton`}
      overrideHeight={520}
      overrideWidth={640}
      {...modalProps}
    >
      <div className={classes.contentContainer}>
        <div className={classes.toggleContainer}>
          <div className={classes.toggleContent}>
            <Typography variant="body1">{t('exportMetricsToggle.label')}</Typography>
            <Typography variant="subtitle1">{t('exportMetricsToggle.infoText')}</Typography>
          </div>
          <YBToggleField name="shouldExportMetrics" control={formMethods.control} />
        </div>

        {shouldExportMetrics && (
          <>
            <Divider />
            <div className={classes.nestedSection}>
              <div className={classes.telemetryProviderConfigField}>
                <Typography variant="body2" className={classes.fieldLabel}>
                  {t('exportConfigSelect.label')}
                </Typography>
                <TelemetryProviderConfigSelectField
                  useControllerProps={{
                    name: 'telemetryConfigUuid',
                    control: formMethods.control,
                    rules: { required: t('formFieldRequired', { keyPrefix: 'common' }) }
                  }}
                  isDisabled={isFormDisabled}
                  telemetryProviderFilter={getIsMetricsExportSupported}
                />
              </div>
            </div>
          </>
        )}
      </div>
    </YBModal>
  );
};

/**
 * This function assumes the request has been validated. i.e. Invalid requests are not expected.
 */
const getMetricsExportConfigurationRequest = (
  currentMetricsExporterUuid: string | undefined,
  targetMetricsExporterUuid: string | undefined
): MetricsExportConfigurationRequest => {
  if (!currentMetricsExporterUuid && targetMetricsExporterUuid) {
    return MetricsExportConfigurationRequest.ENABLE;
  }

  if (
    currentMetricsExporterUuid &&
    targetMetricsExporterUuid &&
    targetMetricsExporterUuid !== currentMetricsExporterUuid
  ) {
    return MetricsExportConfigurationRequest.UPDATE;
  }
  return MetricsExportConfigurationRequest.DISABLE;
};
