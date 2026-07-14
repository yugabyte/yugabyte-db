import { FC, useEffect, useState } from 'react';
import { yupResolver } from '@hookform/resolvers/yup';
import { FormHelperText, Link, makeStyles, Typography } from '@material-ui/core';
import {
  YBCheckbox,
  YBCheckboxField,
  YBInput,
  YBSelect,
  YBSelectField,
  mui
} from '@yugabyte-ui-library/core';
import { Controller, useForm } from 'react-hook-form';
import { useTranslation } from 'react-i18next';
import { useQueryClient } from 'react-query';
import { toast } from 'react-toastify';

import { YBModal, YBTooltip } from '@app/redesign/components';
import { createErrorMessage } from '@app/redesign/features/universe/universe-form/utils/helpers';
import { taskQueryKey, universeQueryKey } from '@app/redesign/helpers/api';
import {
  getGetUniverseQueryKey,
  useConfigureExportTelemetryConfig,
  useGetExportTelemetryConfig
} from '@app/v2/api/universe/universe';
import {
  YSQLQueryLogConfigLogErrorVerbosity,
  YSQLQueryLogConfigLogMinErrorStatement,
  YSQLQueryLogConfigLogStatement
} from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';
import { YBLoadingCircleIcon } from '@app/components/common/indicators';
import { QueryLogConfirmationModal } from './QueryLogConfirmationModal';
import {
  buildTelemetryConfig,
  getDefaultFormValues,
  getValidationSchema,
  LOG_MIN_DURATION_MAX_VALUE,
  LOG_MIN_DURATION_MIN_VALUE,
  QUERY_LOG_DOCS_URL,
  QUERY_LOG_TRANSLATION_KEY_PREFIX,
  QueryLogFormValues,
  QueryLogOperation
} from './queryLogHelpers';

import InfoIcon from '@app/redesign/assets/approved/info-new.svg';
import DocumentationIcon from '@app/redesign/assets/documentation.svg';

const { MenuItem } = mui;

const MODAL_NAME = 'QueryLogSettingsPanel';
const SIDE_PANEL_WIDTH = 736;

// The duration input grows with its content but never below the minimum. The extra
// width covers the input padding and the number spinner controls.
const DURATION_INPUT_MIN_WIDTH = 96;
const DURATION_INPUT_EXTRA_WIDTH = 48;
// Longest valid value is INT_MAX (10 digits); cap the field so it never grows past that.
const DURATION_INPUT_MAX_CHARS = String(LOG_MIN_DURATION_MAX_VALUE).length;

interface QueryLogSettingsPanelProps {
  open: boolean;
  operation: QueryLogOperation;
  universeUuid: string;
  universeName: string;
  onClose: () => void;
}

const useStyles = makeStyles((theme) => ({
  sectionTitle: {
    display: 'flex',
    alignItems: 'center',

    padding: theme.spacing(1, 0),

    color: theme.palette.grey[900],
    fontSize: 13,
    fontWeight: 600,
    lineHeight: '16px'
  },
  card: {
    display: 'flex',
    flexDirection: 'column',
    gap: theme.spacing(1),

    padding: theme.spacing(1.5, 2),

    border: `1px solid ${theme.palette.grey[200]}`,
    borderRadius: theme.shape.borderRadius,

    // The checkbox renders its FormControlLabel as a <label>, which picks up the
    // global Bootstrap `label { margin-bottom: 5px }` rule. This breaks alignment,
    // so we are resetting it here.
    '& .yb-MuiFormControlLabel-root': {
      marginBottom: 0
    }
  },
  groupLabel: {
    paddingTop: theme.spacing(1),
    paddingBottom: theme.spacing(0.5),

    color: theme.palette.grey[600],
    fontSize: 11.5,
    lineHeight: '16px'
  },
  row: {
    display: 'flex',
    alignItems: 'center',
    gap: theme.spacing(1),

    minHeight: theme.spacing(4)
  },
  rowLabel: {
    color: theme.palette.grey[900],
    fontSize: 13,
    fontWeight: 400,
    lineHeight: '16px',
    whiteSpace: 'nowrap'
  },
  spacer: {
    flex: 1
  },
  checkbox: {
    padding: 0
  },
  checkboxRow: {
    display: 'flex',
    alignItems: 'center',
    gap: theme.spacing(1)
  },
  checkboxCell: {
    display: 'flex',
    alignItems: 'center',

    minHeight: theme.spacing(4)
  },
  fieldColumn: {
    flex: 1,

    display: 'flex',
    flexDirection: 'column',
    gap: theme.spacing(0.5)
  },
  fieldError: {
    margin: 0
  },
  infoIcon: {
    flexShrink: 0,

    display: 'flex',
    alignItems: 'center',

    color: theme.palette.grey[500]
  },
  learnMore: {
    display: 'flex',
    alignItems: 'center',
    gap: theme.spacing(0.5),
    justifyContent: 'flex-end',

    paddingTop: theme.spacing(1)
  },
  learnMoreLink: {
    color: theme.palette.primary[600],
    fontSize: 11.5,
    lineHeight: '16px',
    textDecoration: 'underline'
  },
  loaderContainer: {
    display: 'flex',
    alignItems: 'center',
    justifyContent: 'center',

    minHeight: 200
  }
}));

const InfoTooltip: FC<{ title: string; testId: string }> = ({ title, testId }) => {
  const classes = useStyles();
  return (
    <YBTooltip
      title={
        <Typography variant="body2" style={{ whiteSpace: 'pre-line' }}>
          {title}
        </Typography>
      }
    >
      <span className={classes.infoIcon} data-testid={testId}>
        <InfoIcon width={18} height={18} />
      </span>
    </YBTooltip>
  );
};

export const QueryLogSettingsPanel: FC<QueryLogSettingsPanelProps> = ({
  open,
  operation,
  universeUuid,
  universeName,
  onClose
}) => {
  const classes = useStyles();
  const queryClient = useQueryClient();
  const { t } = useTranslation('translation', { keyPrefix: QUERY_LOG_TRANSLATION_KEY_PREFIX });
  const [isConfirmationOpen, setIsConfirmationOpen] = useState(false);

  const telemetryConfigQuery = useGetExportTelemetryConfig(universeUuid, undefined, {
    query: { enabled: open && !!universeUuid }
  });
  const currentTelemetryConfig = telemetryConfigQuery.data;

  const formMethods = useForm<QueryLogFormValues>({
    defaultValues: getDefaultFormValues(currentTelemetryConfig?.query_logs?.ysql_query_log_config),
    resolver: yupResolver(getValidationSchema(t)),
    mode: 'onChange'
  });
  const { control, handleSubmit, watch, reset, clearErrors, trigger, getValues, formState } =
    formMethods;
  const { errors } = formState;

  // The telemetry config loads asynchronously, so reset the form once it arrives to
  // populate accurate edit defaults.
  useEffect(() => {
    if (telemetryConfigQuery.isSuccess) {
      reset(getDefaultFormValues(currentTelemetryConfig?.query_logs?.ysql_query_log_config));
    }
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [telemetryConfigQuery.isSuccess, currentTelemetryConfig]);

  const includeLogStatement = watch('includeLogStatement');
  const includeLogMinDuration = watch('includeLogMinDuration');

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
          queryClient.invalidateQueries(telemetryConfigQuery.queryKey);
          queryClient.invalidateQueries(universeQueryKey.detailsV2(universeUuid));
          queryClient.invalidateQueries(getGetUniverseQueryKey(universeUuid));
          queryClient.invalidateQueries(taskQueryKey.universe(universeUuid));
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

  const submitLabel = operation === 'create' ? t('enableQueryLogging') : t('applyChanges');

  return (
    <>
      <YBModal
        open={open}
        onClose={onClose}
        title={t('title')}
        isSidePanel
        overrideWidth={SIDE_PANEL_WIDTH}
        titleSeparator
        submitLabel={submitLabel}
        cancelLabel={t('cancel')}
        onSubmit={handleSubmit(() => setIsConfirmationOpen(true))}
        buttonProps={{ primary: { disabled: telemetryConfigQuery.isLoading } }}
        submitTestId={`${MODAL_NAME}-SubmitButton`}
        cancelTestId={`${MODAL_NAME}-CancelButton`}
      >
        {telemetryConfigQuery.isLoading ? (
          <div className={classes.loaderContainer}>
            <YBLoadingCircleIcon />
          </div>
        ) : (
          <>
            <div className={classes.sectionTitle}>{t('selectParameters')}</div>
            <div className={classes.card}>
              <Typography className={classes.groupLabel}>{t('groups.general')}</Typography>
              <div className={classes.row}>
                <YBCheckboxField
                  control={control}
                  name="includeLogStatement"
                  label=""
                  className={classes.checkbox}
                  dataTestId={`${MODAL_NAME}-IncludeLogStatement`}
                />
                <Typography className={classes.rowLabel}>{t('logStatement.prefix')}</Typography>
                <YBSelectField
                  control={control}
                  name="logStatement"
                  sx={{ width: 120 }}
                  disabled={!includeLogStatement}
                  renderValue={(value) => String(value).toLowerCase()}
                  dataTestId={`${MODAL_NAME}-LogStatement`}
                >
                  {Object.values(YSQLQueryLogConfigLogStatement)
                    .filter((option) => option !== YSQLQueryLogConfigLogStatement.NONE)
                    .map((option) => (
                      <MenuItem key={option} value={option}>
                        {option.toLowerCase()}
                      </MenuItem>
                    ))}
                </YBSelectField>
                <Typography className={classes.rowLabel}>{t('logStatement.suffix')}</Typography>
                <span className={classes.spacer} />
                <InfoTooltip
                  title={t('tooltips.logStatement')}
                  testId={`${MODAL_NAME}-LogStatementInfo`}
                />
              </div>

              <Typography className={classes.groupLabel}>{t('groups.errorStatement')}</Typography>
              <div className={classes.row}>
                <YBCheckbox
                  label=""
                  className={classes.checkbox}
                  checked
                  disabled
                  dataTestId={`${MODAL_NAME}-IncludeLogMinError`}
                />
                <Typography className={classes.rowLabel}>{t('logMinError.prefix')}</Typography>
                <YBSelect
                  sx={{ width: 120 }}
                  value={YSQLQueryLogConfigLogMinErrorStatement.ERROR}
                  disabled
                  renderValue={(value) => String(value).toLowerCase()}
                  dataTestId={`${MODAL_NAME}-LogMinError`}
                >
                  <MenuItem value={YSQLQueryLogConfigLogMinErrorStatement.ERROR}>
                    {YSQLQueryLogConfigLogMinErrorStatement.ERROR.toLowerCase()}
                  </MenuItem>
                </YBSelect>
                <Typography className={classes.rowLabel}>{t('logMinError.suffix')}</Typography>
                <span className={classes.spacer} />
                <InfoTooltip
                  title={t('tooltips.logMinError')}
                  testId={`${MODAL_NAME}-LogMinErrorInfo`}
                />
              </div>
              <div className={classes.row}>
                <YBCheckbox
                  label=""
                  className={classes.checkbox}
                  checked
                  disabled
                  dataTestId={`${MODAL_NAME}-IncludeLogErrorVerbosity`}
                />
                <Typography className={classes.rowLabel}>
                  {t('logErrorVerbosity.prefix')}
                </Typography>
                <YBSelectField
                  control={control}
                  name="logErrorVerbosity"
                  sx={{ width: 120 }}
                  renderValue={(value) => String(value).toLowerCase()}
                  dataTestId={`${MODAL_NAME}-LogErrorVerbosity`}
                >
                  {Object.values(YSQLQueryLogConfigLogErrorVerbosity).map((option) => (
                    <MenuItem key={option} value={option}>
                      {option.toLowerCase()}
                    </MenuItem>
                  ))}
                </YBSelectField>
                <span className={classes.spacer} />
                <InfoTooltip
                  title={t('tooltips.logErrorVerbosity')}
                  testId={`${MODAL_NAME}-LogErrorVerbosityInfo`}
                />
              </div>

              <Typography className={classes.groupLabel}>{t('groups.queryDuration')}</Typography>
              <div className={classes.row}>
                <YBCheckboxField
                  control={control}
                  name="logDuration"
                  label=""
                  className={classes.checkbox}
                  dataTestId={`${MODAL_NAME}-LogDuration`}
                />
                <Typography className={classes.rowLabel}>{t('logDuration')}</Typography>
                <span className={classes.spacer} />
                <InfoTooltip
                  title={t('tooltips.logDuration')}
                  testId={`${MODAL_NAME}-LogDurationInfo`}
                />
              </div>
              <div className={classes.checkboxRow}>
                <div className={classes.checkboxCell}>
                  <Controller
                    control={control}
                    name="includeLogMinDuration"
                    render={({ field: { value, onChange, onBlur, ref, name } }) => (
                      <YBCheckbox
                        name={name}
                        slotProps={{ input: { ref: ref } }}
                        checked={!!value}
                        onBlur={onBlur}
                        onChange={(event, checked) => {
                          onChange(event);
                          if (!checked) {
                            // Clear the field's validation when it no longer applies.
                            clearErrors('logMinDurationStatement');
                          } else {
                            // On re-check only surface errors for an existing value (e.g.
                            // out of range); don't flag an empty field until the user edits
                            // it or submits.
                            const durationValue = getValues('logMinDurationStatement') as
                              | number
                              | string
                              | null;
                            const isEmpty =
                              durationValue === null ||
                              durationValue === undefined ||
                              durationValue === '';
                            if (!isEmpty) {
                              trigger('logMinDurationStatement');
                            }
                          }
                        }}
                        label=""
                        className={classes.checkbox}
                        dataTestId={`${MODAL_NAME}-IncludeLogMinDuration`}
                      />
                    )}
                  />
                </div>
                <div className={classes.fieldColumn}>
                  <div className={classes.row}>
                    <Typography className={classes.rowLabel}>
                      {t('logMinDuration.prefix')}
                    </Typography>
                    <Controller
                      control={control}
                      name="logMinDurationStatement"
                      render={({ field: { ref, value, ...rest }, fieldState }) => {
                        const durationText =
                          value === null || value === undefined ? '' : String(value);
                        return (
                          <YBInput
                            {...rest}
                            value={value ?? ''}
                            inputRef={ref}
                            type="number"
                            placeholder="2500"
                            sx={{
                              minWidth: DURATION_INPUT_MIN_WIDTH,
                              width: `calc(${durationText.length}ch + ${DURATION_INPUT_EXTRA_WIDTH}px)`,
                              maxWidth: `calc(${DURATION_INPUT_MAX_CHARS}ch + ${DURATION_INPUT_EXTRA_WIDTH}px)`
                            }}
                            disabled={!includeLogMinDuration}
                            error={!!fieldState.error}
                            slotProps={{
                              input: {
                                inputProps: {
                                  min: LOG_MIN_DURATION_MIN_VALUE,
                                  max: LOG_MIN_DURATION_MAX_VALUE
                                }
                              }
                            }}
                            dataTestId={`${MODAL_NAME}-LogMinDurationStatement`}
                          />
                        );
                      }}
                    />
                    <Typography className={classes.rowLabel}>
                      {t('logMinDuration.suffix')}
                    </Typography>
                    <span className={classes.spacer} />
                    <InfoTooltip
                      title={t('tooltips.logMinDuration')}
                      testId={`${MODAL_NAME}-LogMinDurationInfo`}
                    />
                  </div>
                  {includeLogMinDuration && errors.logMinDurationStatement && (
                    <FormHelperText error className={classes.fieldError}>
                      {errors.logMinDurationStatement.message}
                    </FormHelperText>
                  )}
                </div>
              </div>

              <Typography className={classes.groupLabel}>{t('groups.debugging')}</Typography>
              <div className={classes.row}>
                <YBCheckboxField
                  control={control}
                  name="debugPrintPlan"
                  label=""
                  className={classes.checkbox}
                  dataTestId={`${MODAL_NAME}-DebugPrintPlan`}
                />
                <Typography className={classes.rowLabel}>{t('debugPrintPlan')}</Typography>
                <span className={classes.spacer} />
                <InfoTooltip
                  title={t('tooltips.debugPrintPlan')}
                  testId={`${MODAL_NAME}-DebugPrintPlanInfo`}
                />
              </div>

              {/* Connection and Disconnection */}
              <Typography className={classes.groupLabel}>
                {t('groups.connectionDisconnection')}
              </Typography>
              <div className={classes.row}>
                <YBCheckboxField
                  control={control}
                  name="logConnections"
                  label=""
                  className={classes.checkbox}
                  dataTestId={`${MODAL_NAME}-LogConnections`}
                />
                <Typography className={classes.rowLabel}>{t('logConnections')}</Typography>
                <span className={classes.spacer} />
                <InfoTooltip
                  title={t('tooltips.logConnections')}
                  testId={`${MODAL_NAME}-LogConnectionsInfo`}
                />
              </div>
              <div className={classes.row}>
                <YBCheckboxField
                  control={control}
                  name="logDisconnections"
                  label=""
                  className={classes.checkbox}
                  dataTestId={`${MODAL_NAME}-LogDisconnections`}
                />
                <Typography className={classes.rowLabel}>{t('logDisconnections')}</Typography>
                <span className={classes.spacer} />
                <InfoTooltip
                  title={t('tooltips.logDisconnections')}
                  testId={`${MODAL_NAME}-LogDisconnectionsInfo`}
                />
              </div>
            </div>
            <div className={classes.learnMore}>
              <DocumentationIcon width={14} height={14} />
              <Link
                className={classes.learnMoreLink}
                href={QUERY_LOG_DOCS_URL}
                target="_blank"
                rel="noopener noreferrer"
              >
                {t('learnMoreAboutParameters')}
              </Link>
            </div>
          </>
        )}
      </YBModal>
      {isConfirmationOpen && (
        <QueryLogConfirmationModal
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
