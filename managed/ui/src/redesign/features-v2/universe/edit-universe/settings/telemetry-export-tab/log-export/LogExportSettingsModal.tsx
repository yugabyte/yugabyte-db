import { FC, useEffect, useMemo, useState } from 'react';
import { yupResolver } from '@hookform/resolvers/yup';
import { makeStyles, Typography } from '@material-ui/core';
import { YBAutoComplete, YBLabel } from '@yugabyte-ui-library/core';
import { Controller, useForm } from 'react-hook-form';
import { useTranslation } from 'react-i18next';
import { useQuery, useQueryClient } from 'react-query';
import { toast } from 'react-toastify';

import { YBModal } from '@app/redesign/components';
import { YBLoadingCircleIcon } from '@app/components/common/indicators';
import { getIsLogsExportSupported } from '@app/redesign/features/export-telemetry/utils';
import { api, telemetryProviderQueryKey, universeQueryKey } from '@app/redesign/helpers/api';
import { handleServerError } from '@app/utils/errorHandlingUtils';
import {
  useConfigureExportTelemetryConfig,
  useGetExportTelemetryConfig,
  getGetUniverseQueryKey
} from '@app/v2/api/universe/universe';
import { LogExportConfirmationModal } from './LogExportConfirmationModal';
import {
  buildTelemetryConfig,
  getDefaultFormValues,
  getLogExportTranslationKeyPrefix,
  getValidationSchema,
  LogExportFormValues,
  LogExportOperation,
  LogExportType
} from './logExportHelpers';

const MODAL_NAME = 'LogExportSettingsModal';
const MODAL_WIDTH = 600;
const MODAL_HEIGHT = 400;

interface LogExportSettingsModalProps {
  open: boolean;
  logExportType: LogExportType;
  operation: LogExportOperation;
  universeUuid: string;
  universeName: string;
  replicationFactor: number;
  onClose: () => void;
}

interface TelemetryProviderOption {
  uuid: string;
  name: string;
}

const useStyles = makeStyles((theme) => ({
  subtitle: {
    color: theme.palette.grey[900],
    fontSize: 13,
    fontWeight: 600,
    lineHeight: '16px'
  },
  fieldLabel: {
    color: theme.palette.grey[600],
    fontSize: 11.5,
    fontWeight: 500,
    lineHeight: '16px',
    textTransform: 'uppercase',
    padding: theme.spacing(0.25, 0)
  },
  fieldGroup: {
    display: 'flex',
    flexDirection: 'column',
    gap: theme.spacing(0.25),

    width: '100%'
  },
  exportConfigurationField: {
    width: 400,
    maxWidth: '100%'
  },
  loaderContainer: {
    display: 'flex',
    alignItems: 'center',
    justifyContent: 'center',

    minHeight: 200
  },
  modalContent: {
    display: 'flex',
    flexDirection: 'column',
    gap: theme.spacing(3)
  }
}));

export const LogExportSettingsModal: FC<LogExportSettingsModalProps> = ({
  open,
  logExportType,
  operation,
  universeUuid,
  universeName,
  replicationFactor,
  onClose
}) => {
  const classes = useStyles();
  const queryClient = useQueryClient();
  const { t } = useTranslation('translation', {
    keyPrefix: getLogExportTranslationKeyPrefix(logExportType)
  });
  const [isConfirmationOpen, setIsConfirmationOpen] = useState(false);

  const telemetryConfigQuery = useGetExportTelemetryConfig(universeUuid, undefined, {
    query: { enabled: open && !!universeUuid }
  });
  const currentTelemetryConfig = telemetryConfigQuery.data;

  const telemetryProvidersQuery = useQuery(telemetryProviderQueryKey.list(), () =>
    api.fetchTelemetryProviderList()
  );

  const telemetryProviderOptions = useMemo(
    () =>
      (telemetryProvidersQuery.data ?? []).reduce((filteredProviders, telemetryProvider) => {
        if (getIsLogsExportSupported(telemetryProvider)) {
          filteredProviders.push({
            uuid: telemetryProvider.uuid,
            name: telemetryProvider.name
          });
        }
        return filteredProviders;
      }, [] as TelemetryProviderOption[]),
    [telemetryProvidersQuery.data]
  );

  const formMethods = useForm<LogExportFormValues>({
    defaultValues: getDefaultFormValues(logExportType, currentTelemetryConfig),
    resolver: yupResolver(getValidationSchema(t)),
    mode: 'onChange'
  });
  const { control, handleSubmit, reset } = formMethods;

  useEffect(() => {
    if (telemetryConfigQuery.isSuccess) {
      reset(getDefaultFormValues(logExportType, currentTelemetryConfig));
    }
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [telemetryConfigQuery.isSuccess, currentTelemetryConfig, logExportType]);

  const configureTelemetry = useConfigureExportTelemetryConfig();

  const onConfirm = handleSubmit((values) => {
    configureTelemetry.mutate(
      {
        uniUUID: universeUuid,
        data: {
          telemetry_config: buildTelemetryConfig(
            logExportType,
            values,
            currentTelemetryConfig
          )
        }
      },
      {
        onSuccess: () => {
          toast.success(
            <Typography variant="body2" component="span">
              {operation === 'create' ? t('enableInProgress') : t('updateInProgress')}
            </Typography>
          );
          queryClient.invalidateQueries(telemetryConfigQuery.queryKey);
          queryClient.invalidateQueries(universeQueryKey.detailsV2(universeUuid));
          queryClient.invalidateQueries(getGetUniverseQueryKey(universeUuid));
          setIsConfirmationOpen(false);
          onClose();
        },
        onError: (error) => {
          handleServerError(error, {
            customErrorLabel:
              operation === 'create'
                ? t('toast.enableRequestFailedLabel')
                : t('toast.updateRequestFailedLabel')
          });
          setIsConfirmationOpen(false);
        }
      }
    );
  });

  const submitLabel = operation === 'create' ? t('exportLogs') : t('applyChanges');

  const getTelemetryProviderLabel = (option: Record<string, string> | string) =>
    typeof option === 'string' ? option : option.name;

  const isFormLoading = telemetryConfigQuery.isLoading || telemetryProvidersQuery.isLoading;

  return (
    <>
      <YBModal
        open={open}
        onClose={onClose}
        title={t('title')}
        titleSeparator
        overrideWidth={MODAL_WIDTH}
        overrideHeight={MODAL_HEIGHT}
        submitLabel={submitLabel}
        cancelLabel={t('cancel')}
        onSubmit={handleSubmit(() => setIsConfirmationOpen(true))}
        buttonProps={{ primary: { disabled: isFormLoading } }}
        submitTestId={`${MODAL_NAME}-SubmitButton`}
        cancelTestId={`${MODAL_NAME}-CancelButton`}
        dialogContentProps={{ style: { padding: '24px' } }}
      >
        {isFormLoading ? (
          <div className={classes.loaderContainer}>
            <YBLoadingCircleIcon />
          </div>
        ) : (
          <div className={classes.modalContent}>
            <Typography className={classes.subtitle}>{t('subtitle')}</Typography>

            <div className={classes.exportConfigurationField}>
              <Controller
                control={control}
                name="telemetryConfigUuid"
                render={({ field, fieldState }) => {
                  const selectedProvider =
                    telemetryProviderOptions.find(
                      (providerOption) => providerOption.uuid === field.value
                    ) ?? null;

                  return (
                    <div className={classes.fieldGroup}>
                      <YBLabel className={classes.fieldLabel} error={!!fieldState.error}>
                        {t('exportConfiguration')}
                      </YBLabel>
                      <YBAutoComplete
                        value={selectedProvider as unknown as Record<string, string>}
                        options={telemetryProviderOptions as unknown as Record<string, string>[]}
                        getOptionLabel={getTelemetryProviderLabel}
                        isOptionEqualToValue={(option, value) =>
                          (option as unknown as TelemetryProviderOption).uuid ===
                          (value as unknown as TelemetryProviderOption).uuid
                        }
                        onChange={(_event, option) => {
                          const selectedOption = option as TelemetryProviderOption | null;
                          field.onChange(selectedOption?.uuid ?? '');
                        }}
                        ybInputProps={{
                          error: !!fieldState.error,
                          helperText: fieldState.error?.message,
                          placeholder: t('exportConfigurationPlaceholder'),
                          dataTestId: `${MODAL_NAME}-ExportConfiguration`
                        }}
                        dataTestId={`${MODAL_NAME}-ExportConfiguration-Container`}
                        disabled={configureTelemetry.isLoading}
                      />
                    </div>
                  );
                }}
              />
            </div>
          </div>
        )}
      </YBModal>

      {isConfirmationOpen && (
        <LogExportConfirmationModal
          logExportType={logExportType}
          operation={operation}
          universeName={universeName}
          replicationFactor={replicationFactor}
          isSubmitting={configureTelemetry.isLoading}
          onSubmit={onConfirm}
          modalProps={{
            open: isConfirmationOpen,
            onClose: () => setIsConfirmationOpen(false)
          }}
        />
      )}
    </>
  );
};
