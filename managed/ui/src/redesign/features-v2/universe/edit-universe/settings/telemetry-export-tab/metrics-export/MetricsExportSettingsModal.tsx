import { FC, useEffect, useMemo, useState } from 'react';
import { yupResolver } from '@hookform/resolvers/yup';
import { Link, makeStyles, Typography } from '@material-ui/core';
import SettingsOutlinedIcon from '@material-ui/icons/SettingsOutlined';
import { YBAutoComplete, YBInput, YBLabel, mui } from '@yugabyte-ui-library/core';
import { Controller, useForm } from 'react-hook-form';
import { Trans, useTranslation } from 'react-i18next';
import { useQuery, useQueryClient } from 'react-query';
import { toast } from 'react-toastify';

import { YBModal } from '@app/redesign/components';
import { YBLoadingCircleIcon } from '@app/components/common/indicators';
import { createErrorMessage } from '@app/redesign/features/universe/universe-form/utils/helpers';
import { getIsMetricsExportSupported } from '@app/redesign/features/export-telemetry/utils';
import { api, telemetryProviderQueryKey } from '@app/redesign/helpers/api';
import {
  useConfigureExportTelemetryConfig,
  useGetExportTelemetryConfig,
  getGetUniverseQueryKey
} from '@app/v2/api/universe/universe';
import { MetricsExportConfirmationModal } from './MetricsExportConfirmationModal';
import {
  buildTelemetryConfig,
  COLLECTION_LEVEL_OPTIONS,
  CollectionLevelOption,
  getDefaultFormValues,
  getValidationSchema,
  METRICS_EXPORT_DOCS_URL,
  METRICS_EXPORT_TRANSLATION_KEY_PREFIX,
  MetricsExportFormValues,
  MetricsExportOperation,
  SCRAPE_CONFIG_TARGET_OPTIONS,
  ScrapeConfigTargetOption
} from './metricsExportHelpers';

import TriangleArrowDownIcon from '@app/redesign/assets/approved/triangle-arrow-down.svg';
import SettingsIcon from '@app/redesign/assets/approved/admin.svg';

const { Collapse } = mui;

const MODAL_NAME = 'MetricsExportSettingsModal';
const MODAL_WIDTH = 800;

interface MetricsExportSettingsModalProps {
  open: boolean;
  operation: MetricsExportOperation;
  universeUuid: string;
  universeName: string;
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
  collectionSettingsPanel: {
    display: 'flex',
    flexDirection: 'column',
    gap: theme.spacing(3),

    width: '100%',
    padding: theme.spacing(2.5),

    backgroundColor: theme.palette.ybacolors.grey005,
    border: `1px solid ${theme.palette.grey[200]}`,
    borderRadius: theme.shape.borderRadius
  },
  collectionSettingsHeader: {
    display: 'flex',
    alignItems: 'center',
    gap: theme.spacing(1)
  },
  collectionSettingsTitle: {
    color: theme.palette.grey[900],
    fontSize: 11.5,
    fontWeight: 500,
    lineHeight: '16px',
    textTransform: 'uppercase'
  },
  intervalTimeoutRow: {
    display: 'flex',
    gap: theme.spacing(3),

    width: '100%'
  },
  intervalTimeoutField: {
    flex: 1,

    display: 'flex',
    flexDirection: 'column',
    gap: theme.spacing(0.25)
  },
  advancedToggle: {
    display: 'flex',
    alignItems: 'center',
    gap: theme.spacing(1),

    width: 'fit-content',
    padding: 0,

    background: 'none',
    border: 'none',
    cursor: 'pointer'
  },
  advancedToggleLabel: {
    color: theme.palette.grey[900],
    fontSize: 13,
    fontWeight: 600,
    lineHeight: '16px'
  },
  advancedToggleIcon: {
    display: 'flex',
    flexShrink: 0,

    width: 24,
    height: 24,

    transition: theme.transitions.create('transform')
  },
  advancedToggleIconCollapsed: {
    transform: 'rotate(-90deg)'
  },
  advancedSectionContainer: {
    display: 'flex',
    flexDirection: 'column'
  },
  advancedSection: {
    display: 'flex',
    flexDirection: 'column',
    gap: theme.spacing(3),

    marginTop: theme.spacing(3)
  },
  collectionLevelField: {
    maxWidth: 344
  },
  fieldWithHelper: {
    display: 'flex',
    flexDirection: 'column',
    gap: theme.spacing(1),

    width: '100%'
  },
  helperText: {
    color: theme.palette.grey[600]
  },
  learnMoreLink: {
    color: theme.palette.grey[600],
    fontSize: 11.5,
    lineHeight: '16px',
    textDecoration: 'underline',
    textDecorationSkipInk: 'none',
    textUnderlinePosition: 'from-font'
  },
  metricSourcesAutocomplete: {
    '& .MuiAutocomplete-inputRoot': {
      alignItems: 'flex-start',
      minHeight: 40,
      padding: '8px !important'
    },
    '& .MuiChip-root': {
      backgroundColor: theme.palette.primary[200],
      borderRadius: 6,
      color: '#1E154B',
      fontSize: 11.5,
      height: 24,
      lineHeight: '16px'
    },
    '& .MuiChip-label': {
      fontSize: 11.5,
      paddingLeft: 8,
      paddingRight: 4
    }
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

export const MetricsExportSettingsModal: FC<MetricsExportSettingsModalProps> = ({
  open,
  operation,
  universeUuid,
  universeName,
  onClose
}) => {
  const classes = useStyles();
  const queryClient = useQueryClient();
  const { t } = useTranslation('translation', { keyPrefix: METRICS_EXPORT_TRANSLATION_KEY_PREFIX });
  const [isConfirmationOpen, setIsConfirmationOpen] = useState(false);
  const [isAdvancedExpanded, setIsAdvancedExpanded] = useState(false);

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
        if (getIsMetricsExportSupported(telemetryProvider)) {
          filteredProviders.push({
            uuid: telemetryProvider.uuid,
            name: telemetryProvider.name
          });
        }
        return filteredProviders;
      }, [] as TelemetryProviderOption[]),
    [telemetryProvidersQuery.data]
  );

  const formMethods = useForm<MetricsExportFormValues>({
    defaultValues: getDefaultFormValues(currentTelemetryConfig?.metrics),
    resolver: yupResolver(getValidationSchema(t)),
    mode: 'onChange'
  });
  const { control, handleSubmit, reset } = formMethods;

  useEffect(() => {
    if (telemetryConfigQuery.isSuccess) {
      reset(getDefaultFormValues(currentTelemetryConfig?.metrics));
    }
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [telemetryConfigQuery.isSuccess, currentTelemetryConfig]);

  const configureTelemetry = useConfigureExportTelemetryConfig();

  const onConfirm = handleSubmit((values) => {
    configureTelemetry.mutate(
      {
        uniUUID: universeUuid,
        data: {
          upgrade_options: { rolling_upgrade: true },
          telemetry_config: buildTelemetryConfig(values, currentTelemetryConfig)
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
          queryClient.invalidateQueries(getGetUniverseQueryKey(universeUuid));
          setIsConfirmationOpen(false);
          onClose();
        },
        onError: (error) => {
          toast.error(createErrorMessage(error));
          setIsConfirmationOpen(false);
        }
      }
    );
  });

  const submitLabel = operation === 'create' ? t('exportMetrics') : t('applyChanges');

  const getCollectionLevelLabel = (option: CollectionLevelOption) => t(option.labelKey);

  const getCollectionLevelOptionLabel = (option: Record<string, string> | string) => {
    if (typeof option === 'string') {
      const matchedOption = COLLECTION_LEVEL_OPTIONS.find(
        (collectionLevelOption) => collectionLevelOption.value === option
      );
      return matchedOption ? getCollectionLevelLabel(matchedOption) : option;
    }
    return getCollectionLevelLabel(option as unknown as CollectionLevelOption);
  };

  const getTelemetryProviderLabel = (option: Record<string, string> | string) =>
    typeof option === 'string' ? option : option.name;

  const getScrapeTargetLabel = (option: Record<string, string> | string) =>
    typeof option === 'string' ? option : option.label;

  const isFormLoading = telemetryConfigQuery.isLoading || telemetryProvidersQuery.isLoading;

  return (
    <>
      <YBModal
        open={open}
        onClose={onClose}
        title={t('title')}
        titleSeparator
        overrideWidth={MODAL_WIDTH}
        overrideHeight="fit-content"
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

            <div className={classes.collectionSettingsPanel}>
              <div className={classes.collectionSettingsHeader}>
                <SettingsIcon width={16} height={16} />
                <Typography className={classes.collectionSettingsTitle}>
                  {t('collectionSettings')}
                </Typography>
              </div>

              <div className={classes.intervalTimeoutRow}>
                <div className={classes.intervalTimeoutField}>
                  <Controller
                    control={control}
                    name="scrapeIntervalSeconds"
                    render={({ field, fieldState }) => (
                      <>
                        <YBLabel className={classes.fieldLabel} error={!!fieldState.error}>
                          {t('collectionInterval')}
                        </YBLabel>
                        <YBInput
                          type="number"
                          fullWidth
                          size="medium"
                          inputMode="numeric"
                          value={field.value ?? ''}
                          onChange={(event) => {
                            const nextValue = event.target.value;
                            field.onChange(nextValue === '' ? '' : Number(nextValue));
                          }}
                          onBlur={field.onBlur}
                          slotProps={{
                            htmlInput: {
                              min: 1,
                              'data-testid': `${MODAL_NAME}-CollectionInterval`
                            }
                          }}
                          error={!!fieldState.error}
                          helperText={fieldState.error?.message}
                          dataTestId={`${MODAL_NAME}-CollectionInterval-Input`}
                          disabled={configureTelemetry.isLoading}
                        />
                      </>
                    )}
                  />
                </div>
                <div className={classes.intervalTimeoutField}>
                  <Controller
                    control={control}
                    name="scrapeTimeoutSeconds"
                    render={({ field, fieldState }) => (
                      <>
                        <YBLabel className={classes.fieldLabel} error={!!fieldState.error}>
                          {t('collectionTimeout')}
                        </YBLabel>
                        <YBInput
                          type="number"
                          fullWidth
                          size="medium"
                          inputMode="numeric"
                          value={field.value ?? ''}
                          onChange={(event) => {
                            const nextValue = event.target.value;
                            field.onChange(nextValue === '' ? '' : Number(nextValue));
                          }}
                          onBlur={field.onBlur}
                          slotProps={{
                            htmlInput: {
                              min: 1,
                              'data-testid': `${MODAL_NAME}-CollectionTimeout`
                            }
                          }}
                          error={!!fieldState.error}
                          helperText={fieldState.error?.message}
                          dataTestId={`${MODAL_NAME}-CollectionTimeout-Input`}
                          disabled={configureTelemetry.isLoading}
                        />
                      </>
                    )}
                  />
                </div>
              </div>
              <div className={classes.advancedSectionContainer}>
                <button
                  type="button"
                  className={classes.advancedToggle}
                  onClick={() => setIsAdvancedExpanded((expanded) => !expanded)}
                  data-testid={`${MODAL_NAME}-AdvancedToggle`}
                >
                  <span
                    className={`${classes.advancedToggleIcon} ${
                      isAdvancedExpanded ? '' : classes.advancedToggleIconCollapsed
                    }`}
                  >
                    <TriangleArrowDownIcon width={24} height={24} />
                  </span>
                  <Typography className={classes.advancedToggleLabel}>{t('advanced')}</Typography>
                </button>

                <Collapse in={isAdvancedExpanded}>
                  <div className={classes.advancedSection}>
                    <div className={`${classes.fieldWithHelper} ${classes.collectionLevelField}`}>
                      <Controller
                        control={control}
                        name="collectionLevel"
                        render={({ field, fieldState }) => {
                          const selectedLevel =
                            COLLECTION_LEVEL_OPTIONS.find(
                              (collectionLevelOption) => collectionLevelOption.value === field.value
                            ) ?? null;

                          return (
                            <div className={classes.fieldGroup}>
                              <YBLabel className={classes.fieldLabel} error={!!fieldState.error}>
                                {t('collectionLevel')}
                              </YBLabel>
                              <YBAutoComplete
                                value={selectedLevel as unknown as Record<string, string>}
                                options={
                                  COLLECTION_LEVEL_OPTIONS as unknown as Record<string, string>[]
                                }
                                getOptionLabel={getCollectionLevelOptionLabel}
                                isOptionEqualToValue={(option, value) =>
                                  (option as unknown as CollectionLevelOption).value ===
                                  (value as unknown as CollectionLevelOption).value
                                }
                                onChange={(_event, option) => {
                                  const selectedOption = option as CollectionLevelOption | null;
                                  field.onChange(selectedOption?.value ?? field.value);
                                }}
                                ybInputProps={{
                                  error: !!fieldState.error,
                                  helperText: fieldState.error?.message,
                                  dataTestId: `${MODAL_NAME}-CollectionLevel`
                                }}
                                dataTestId={`${MODAL_NAME}-CollectionLevel-Container`}
                                disabled={configureTelemetry.isLoading}
                              />
                            </div>
                          );
                        }}
                      />
                      <Typography variant="body2" className={classes.helperText}>
                        <Trans
                          t={t}
                          i18nKey="collectionLevelHelper"
                          components={{
                            learnMore: (
                              <Link
                                className={classes.learnMoreLink}
                                href={METRICS_EXPORT_DOCS_URL}
                                target="_blank"
                                rel="noopener noreferrer"
                              />
                            )
                          }}
                        />
                      </Typography>
                    </div>

                    <div className={classes.fieldWithHelper}>
                      <Controller
                        control={control}
                        name="scrapeConfigTargets"
                        render={({ field, fieldState }) => {
                          const selectedTargets = SCRAPE_CONFIG_TARGET_OPTIONS.filter(
                            (targetOption) => field.value.includes(targetOption.value)
                          );

                          return (
                            <div className={classes.fieldGroup}>
                              <YBLabel className={classes.fieldLabel} error={!!fieldState.error}>
                                {t('metricSources')}
                              </YBLabel>
                              <YBAutoComplete
                                multiple
                                filterSelectedOptions
                                className={classes.metricSourcesAutocomplete}
                                value={selectedTargets as unknown as Record<string, string>[]}
                                options={
                                  SCRAPE_CONFIG_TARGET_OPTIONS as unknown as Record<
                                    string,
                                    string
                                  >[]
                                }
                                getOptionLabel={getScrapeTargetLabel}
                                isOptionEqualToValue={(option, value) =>
                                  (option as unknown as ScrapeConfigTargetOption).value ===
                                  (value as unknown as ScrapeConfigTargetOption).value
                                }
                                onChange={(_event, options) => {
                                  const selectedOptions = (options ??
                                    []) as unknown as ScrapeConfigTargetOption[];
                                  field.onChange(selectedOptions.map((option) => option.value));
                                }}
                                ybInputProps={{
                                  error: !!fieldState.error,
                                  helperText: fieldState.error?.message,
                                  dataTestId: `${MODAL_NAME}-MetricSources`
                                }}
                                dataTestId={`${MODAL_NAME}-MetricSources-Container`}
                                disabled={configureTelemetry.isLoading}
                              />
                            </div>
                          );
                        }}
                      />
                      <Typography variant="body2" className={classes.helperText}>
                        <Trans
                          t={t}
                          i18nKey="metricSourcesHelper"
                          components={{
                            learnMore: (
                              <Link
                                className={classes.learnMoreLink}
                                href={METRICS_EXPORT_DOCS_URL}
                                target="_blank"
                                rel="noopener noreferrer"
                              />
                            )
                          }}
                        />
                      </Typography>
                    </div>
                  </div>
                </Collapse>
              </div>
            </div>
          </div>
        )}
      </YBModal>

      {isConfirmationOpen && (
        <MetricsExportConfirmationModal
          operation={operation}
          universeName={universeName}
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
