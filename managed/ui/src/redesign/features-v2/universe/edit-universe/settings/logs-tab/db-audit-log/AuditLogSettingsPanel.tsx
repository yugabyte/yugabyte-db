import { ChangeEvent, FC, useState } from 'react';
import { Link, makeStyles, Typography } from '@material-ui/core';
import {
  mui,
  YBInputField,
  YBSelectField,
  YBToggle,
  YBToggleField
} from '@yugabyte-ui-library/core';
import { useForm } from 'react-hook-form';
import { Trans, useTranslation } from 'react-i18next';
import { useQueryClient } from 'react-query';
import clsx from 'clsx';

import { YBLoadingCircleIcon } from '@app/components/common/indicators';
import { YBModal, YBTooltip } from '@app/redesign/components';
import { taskQueryKey, universeQueryKey } from '@app/redesign/helpers/api';
import {
  getGetExportTelemetryConfigQueryKey,
  getGetUniverseQueryKey,
  useConfigureExportTelemetryConfig,
  useGetExportTelemetryConfig
} from '@app/v2/api/universe/universe';
import {
  TelemetryConfig,
  YSQLAuditConfigClassesItem
} from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';
import { handleServerError } from '@app/utils/errorHandlingUtils';
import { AuditLogConfirmationModal } from './AuditLogConfirmationModal';
import {
  AUDIT_LOG_TRANSLATION_KEY_PREFIX,
  AuditLogFormValues,
  AuditLogOperation,
  DEFAULT_LOG_RETENTION_DAYS,
  buildTelemetryConfig,
  getDefaultFormValues
} from './auditLogHelpers';
import {
  AUDIT_LOG_DOCS_URL,
  PGAUDIT_DOCS_URL,
  YSQL_AUDIT_CLASSES,
  YSQL_LOG_LEVEL_OPTIONS
} from './constants';

import InfoIcon from '@app/redesign/assets/approved/info-new.svg';
import TreeStructureIcon from '@app/redesign/assets/approved/tree-structure.svg';

const { MenuItem } = mui;

const MODAL_NAME = 'AuditLogSettingsPanel';
const SIDE_PANEL_WIDTH = 736;

interface AuditLogSettingsPanelProps {
  open: boolean;
  operation: AuditLogOperation;
  universeUuid: string;
  universeName: string;
  replicationFactor: number;
  onClose: () => void;
}

const useStyles = makeStyles((theme) => ({
  introSection: {
    display: 'flex',
    flexDirection: 'column',
    gap: theme.spacing(1),

    width: 622
  },
  introTitle: {
    padding: theme.spacing(1, 0),

    color: theme.palette.grey[900],
    fontSize: 16,
    fontWeight: 700,
    lineHeight: 'normal'
  },
  introText: {
    color: theme.palette.grey[900],
    fontSize: 13,
    lineHeight: '24px'
  },
  introLink: {
    color: theme.palette.primary[600],
    fontSize: 13,
    lineHeight: '24px',
    textDecoration: 'underline'
  },
  sectionHeading: {
    color: theme.palette.grey[900],
    fontSize: 13,
    fontWeight: 600,
    lineHeight: '16px'
  },
  sectionContainer: {
    display: 'flex',
    flexDirection: 'column',
    gap: theme.spacing(2)
  },
  card: {
    display: 'flex',
    flexDirection: 'column',

    overflow: 'hidden',

    border: `1px solid ${theme.palette.grey[300]}`,
    borderRadius: theme.shape.borderRadius,

    '& > $row:last-child': {
      borderBottom: 'none'
    },

    '& > $expandableRowGroup:last-child > *:last-child': {
      borderBottom: 'none'
    },

    '& .yb-MuiFormControlLabel-root': {
      marginBottom: 0
    }
  },
  row: {
    display: 'flex',
    alignItems: 'center',
    justifyContent: 'space-between',
    gap: theme.spacing(1),

    minHeight: 56,
    padding: theme.spacing(2, 3),

    borderBottom: `1px solid ${theme.palette.grey[300]}`
  },
  rowLabelGroup: {
    display: 'flex',
    alignItems: 'center',
    gap: theme.spacing(0.5)
  },
  rowLabel: {
    color: theme.palette.grey[900],
    fontSize: 13,
    fontWeight: 400,
    lineHeight: 'normal',
    whiteSpace: 'nowrap'
  },
  expandedParentRow: {},
  nestedRow: {
    display: 'flex',
    alignItems: 'center',
    gap: theme.spacing(1),

    minHeight: 60,
    padding: theme.spacing(2, 3),

    borderBottom: `1px solid ${theme.palette.grey[300]}`
  },
  nestedField: {
    flex: 1
  },
  treeIcon: {
    flexShrink: 0,

    display: 'flex',
    alignItems: 'center',

    color: theme.palette.grey[500]
  },
  infoIcon: {
    flexShrink: 0,

    display: 'flex',
    alignItems: 'center',

    color: theme.palette.grey[500]
  },
  expandableRowGroup: {
    display: 'flex',
    flexDirection: 'column',

    '& $expandedParentRow': {
      borderBottom: 'none'
    }
  },
  loaderContainer: {
    display: 'flex',
    alignItems: 'center',
    justifyContent: 'center',

    minHeight: 200
  },
  panelContent: {
    display: 'flex',
    flexDirection: 'column',
    gap: theme.spacing(4)
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
        <InfoIcon width={16} height={16} />
      </span>
    </YBTooltip>
  );
};

interface AuditLogToggleRowProps {
  name: keyof Pick<
    AuditLogFormValues,
    | 'logCatalog'
    | 'logParameter'
    | 'logRelation'
    | 'logStatement'
    | 'logStatementOnce'
  >;
  label: string;
  tooltip: string;
  control: ReturnType<typeof useForm<AuditLogFormValues>>['control'];
}

const AuditLogToggleRow: FC<AuditLogToggleRowProps> = ({
  name,
  label,
  tooltip,
  control
}) => {
  const classes = useStyles();
  return (
    <div className={classes.row}>
      <div className={classes.rowLabelGroup}>
        <Typography className={classes.rowLabel}>{label}</Typography>
        <InfoTooltip title={tooltip} testId={`${MODAL_NAME}-${name}Info`} />
      </div>
      <YBToggleField
        control={control}
        name={name}
        label=""
        dataTestId={`${MODAL_NAME}-${name}`}
      />
    </div>
  );
};

interface AuditLogSettingsFormProps {
  operation: AuditLogOperation;
  universeUuid: string;
  universeName: string;
  replicationFactor: number;
  currentTelemetryConfig?: TelemetryConfig;
  onClose: () => void;
}

const AuditLogSettingsForm: FC<AuditLogSettingsFormProps> = ({
  operation,
  universeUuid,
  universeName,
  replicationFactor,
  currentTelemetryConfig,
  onClose
}) => {
  const classes = useStyles();
  const queryClient = useQueryClient();
  const { t } = useTranslation('translation', { keyPrefix: AUDIT_LOG_TRANSLATION_KEY_PREFIX });
  const [isConfirmationOpen, setIsConfirmationOpen] = useState(false);

  const formMethods = useForm<AuditLogFormValues>({
    defaultValues: getDefaultFormValues(currentTelemetryConfig?.audit_logs?.ysql_audit_config),
    mode: 'onChange'
  });
  const { control, handleSubmit, watch, setValue, getValues, setError, clearErrors, formState } =
    formMethods;
  const { errors } = formState;

  const auditLogClasses = watch('classes');
  const logClient = watch('logClient');
  const retainAuditLogArchive = watch('retainAuditLogArchive');

  const configureTelemetry = useConfigureExportTelemetryConfig();

  const validateLogRetentionDays = (value: unknown): true | string => {
    const numericValue = Number(value);
    if (
      value === null ||
      value === undefined ||
      Number.isNaN(numericValue) ||
      !Number.isInteger(numericValue) ||
      numericValue < DEFAULT_LOG_RETENTION_DAYS
    ) {
      return t('validation.logRetentionDaysMin');
    }
    return true;
  };

  const onConfirm = handleSubmit((values) => {
    configureTelemetry.mutate(
      {
        uniUUID: universeUuid,
        data: {
          telemetry_config: buildTelemetryConfig(values, currentTelemetryConfig)
        }
      },
      {
        onSuccess: () => {
          queryClient.invalidateQueries(getGetExportTelemetryConfigQueryKey(universeUuid));
          queryClient.invalidateQueries(universeQueryKey.detailsV2(universeUuid));
          queryClient.invalidateQueries(getGetUniverseQueryKey(universeUuid));
          queryClient.invalidateQueries(taskQueryKey.universe(universeUuid));
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

  const submitLabel =
    operation === 'create' ? t('enableAuditLogging') : t('applyChanges');

  const toggleClass = (classValue: YSQLAuditConfigClassesItem) => {
    const currentIndex = auditLogClasses.findIndex((auditClass) => auditClass === classValue);
    const isSelected = currentIndex > -1;
    if (isSelected) {
      setValue(
        'classes',
        auditLogClasses.filter((auditClass) => auditClass !== classValue)
      );
    } else {
      setValue('classes', [...auditLogClasses, classValue]);
    }
  };

  const onRetainAuditLogArchiveChange = (
    _event: ChangeEvent<HTMLInputElement>,
    checked: boolean
  ) => {
    setValue('retainAuditLogArchive', checked);
    if (!checked) {
      clearErrors('logRetentionDays');
      return;
    }

    const validationResult = validateLogRetentionDays(getValues('logRetentionDays'));
    if (validationResult === true) {
      clearErrors('logRetentionDays');
    } else {
      setError('logRetentionDays', { type: 'validate', message: validationResult });
    }
  };

  return (
    <>
      <YBModal
        open
        onClose={onClose}
        title={t('title')}
        isSidePanel
        overrideWidth={SIDE_PANEL_WIDTH}
        titleSeparator
        submitLabel={submitLabel}
        cancelLabel={t('cancel')}
        onSubmit={handleSubmit(() => setIsConfirmationOpen(true))}
        buttonProps={{
          primary: { disabled: !!errors.logRetentionDays }
        }}
        submitTestId={`${MODAL_NAME}-SubmitButton`}
        cancelTestId={`${MODAL_NAME}-CancelButton`}
      >
        <div className={classes.panelContent}>
          <div className={classes.introSection}>
            <Typography className={classes.introTitle}>{t('introTitle')}</Typography>
            <Typography className={classes.introText} variant="body2">
              <Trans
                t={t}
                i18nKey="introText"
                components={{
                  pgAuditLink: (
                    <Link
                      className={classes.introLink}
                      href={PGAUDIT_DOCS_URL}
                      target="_blank"
                      rel="noopener noreferrer"
                    />
                  ),
                  docsLink: (
                    <Link
                      className={classes.introLink}
                      href={AUDIT_LOG_DOCS_URL}
                      target="_blank"
                      rel="noopener noreferrer"
                    />
                  )
                }}
              />
            </Typography>
          </div>

          <div className={classes.sectionContainer}>
            <Typography className={classes.sectionHeading} component="div">
              {t('section1Title')}
            </Typography>
            <div className={classes.card}>
              {YSQL_AUDIT_CLASSES.map((auditClass) => {
                const isSelected = auditLogClasses.includes(auditClass.value);
                return (
                  <div key={auditClass.value} className={classes.row}>
                    <div className={classes.rowLabelGroup}>
                      <Typography className={classes.rowLabel}>{auditClass.title}</Typography>
                      <InfoTooltip
                        title={t(auditClass.tooltipKey)}
                        testId={`${MODAL_NAME}-Class-${auditClass.value}-Info`}
                      />
                    </div>
                    <YBToggle
                      checked={isSelected}
                      onChange={() => toggleClass(auditClass.value)}
                      dataTestId={`${MODAL_NAME}-Class-${auditClass.value}`}
                    />
                  </div>
                );
              })}
            </div>
          </div>

          <div className={classes.sectionContainer}>
            <Typography className={classes.sectionHeading} component="div">
              {t('section2Title')}
            </Typography>
            <div className={classes.card}>
              <AuditLogToggleRow
                name="logCatalog"
                label={t('fields.logCatalog')}
                tooltip={t('tooltips.logCatalog')}
                control={control}
              />
              <div className={classes.expandableRowGroup}>
                <div className={clsx(classes.row, logClient && classes.expandedParentRow)}>
                  <div className={classes.rowLabelGroup}>
                    <Typography className={classes.rowLabel}>{t('fields.logClient')}</Typography>
                    <InfoTooltip
                      title={t('tooltips.logClient')}
                      testId={`${MODAL_NAME}-logClientInfo`}
                    />
                  </div>
                  <YBToggleField
                    control={control}
                    name="logClient"
                    label=""
                    dataTestId={`${MODAL_NAME}-logClient`}
                  />
                </div>
                {logClient && (
                  <div className={classes.nestedRow}>
                    <span className={classes.treeIcon}>
                      <TreeStructureIcon width={16} height={16} />
                    </span>
                    <div className={classes.rowLabelGroup}>
                      <Typography className={classes.rowLabel}>{t('fields.logLevel')}</Typography>
                      <InfoTooltip
                        title={t('tooltips.logLevel')}
                        testId={`${MODAL_NAME}-logLevelInfo`}
                      />
                    </div>
                    <YBSelectField
                      control={control}
                      name="logLevel"
                      className={classes.nestedField}
                      dataTestId={`${MODAL_NAME}-logLevel`}
                    >
                      {YSQL_LOG_LEVEL_OPTIONS.map((logOption) => (
                        <MenuItem key={logOption.value} value={logOption.value}>
                          {logOption.label}
                        </MenuItem>
                      ))}
                    </YBSelectField>
                  </div>
                )}
              </div>
              <AuditLogToggleRow
                name="logParameter"
                label={t('fields.logParameter')}
                tooltip={t('tooltips.logParameter')}
                control={control}
              />
              <AuditLogToggleRow
                name="logRelation"
                label={t('fields.logRelation')}
                tooltip={t('tooltips.logRelation')}
                control={control}
              />
              <AuditLogToggleRow
                name="logStatement"
                label={t('fields.logStatement')}
                tooltip={t('tooltips.logStatement')}
                control={control}
              />
              <AuditLogToggleRow
                name="logStatementOnce"
                label={t('fields.logStatementOnce')}
                tooltip={t('tooltips.logStatementOnce')}
                control={control}
              />
              <div className={classes.expandableRowGroup}>
                <div
                  className={clsx(classes.row, retainAuditLogArchive && classes.expandedParentRow)}
                >
                  <div className={classes.rowLabelGroup}>
                    <Typography className={classes.rowLabel}>
                      {t('fields.retainAuditLogArchive')}
                    </Typography>
                    <InfoTooltip
                      title={t('tooltips.retainAuditLogArchive')}
                      testId={`${MODAL_NAME}-retainAuditLogArchiveInfo`}
                    />
                  </div>
                  <YBToggleField
                    control={control}
                    name="retainAuditLogArchive"
                    label=""
                    onChange={onRetainAuditLogArchiveChange}
                    dataTestId={`${MODAL_NAME}-retainAuditLogArchive`}
                  />
                </div>
                {retainAuditLogArchive && (
                  <div className={classes.nestedRow}>
                    <span className={classes.treeIcon}>
                      <TreeStructureIcon width={16} height={16} />
                    </span>
                    <div className={classes.rowLabelGroup}>
                      <Typography className={classes.rowLabel}>
                        {t('fields.logRetentionDays')}
                      </Typography>
                      <InfoTooltip
                        title={t('tooltips.logRetentionDays')}
                        testId={`${MODAL_NAME}-logRetentionDaysInfo`}
                      />
                    </div>
                    <YBInputField
                      control={control}
                      name="logRetentionDays"
                      type="number"
                      className={classes.nestedField}
                      dataTestId={`${MODAL_NAME}-logRetentionDays`}
                      slotProps={{ htmlInput: { min: DEFAULT_LOG_RETENTION_DAYS } }}
                      rules={{
                        validate: validateLogRetentionDays
                      }}
                    />
                  </div>
                )}
              </div>
            </div>
          </div>
        </div>
      </YBModal>
      {isConfirmationOpen && (
        <AuditLogConfirmationModal
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

export const AuditLogSettingsPanel: FC<AuditLogSettingsPanelProps> = ({
  open,
  operation,
  universeUuid,
  universeName,
  replicationFactor,
  onClose
}) => {
  const classes = useStyles();
  const { t } = useTranslation('translation', { keyPrefix: AUDIT_LOG_TRANSLATION_KEY_PREFIX });

  const telemetryConfigQuery = useGetExportTelemetryConfig(universeUuid, undefined, {
    query: { enabled: open && !!universeUuid }
  });

  if (!open) {
    return null;
  }

  if (!telemetryConfigQuery.isSuccess) {
    return (
      <YBModal
        open
        onClose={onClose}
        title={t('title')}
        isSidePanel
        overrideWidth={SIDE_PANEL_WIDTH}
        titleSeparator
        submitLabel={
          operation === 'create' ? t('enableAuditLogging') : t('applyChanges')
        }
        cancelLabel={t('cancel')}
        onSubmit={() => undefined}
        buttonProps={{ primary: { disabled: true } }}
        submitTestId={`${MODAL_NAME}-SubmitButton`}
        cancelTestId={`${MODAL_NAME}-CancelButton`}
      >
        <div className={classes.loaderContainer}>
          <YBLoadingCircleIcon />
        </div>
      </YBModal>
    );
  }

  return (
    <AuditLogSettingsForm
      operation={operation}
      universeUuid={universeUuid}
      universeName={universeName}
      replicationFactor={replicationFactor}
      currentTelemetryConfig={telemetryConfigQuery.data}
      onClose={onClose}
    />
  );
};
