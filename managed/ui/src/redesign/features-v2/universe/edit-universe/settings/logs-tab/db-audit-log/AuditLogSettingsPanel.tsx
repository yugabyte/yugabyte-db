import { FC, useEffect, useState } from 'react';
import { Link, makeStyles, Typography } from '@material-ui/core';
import { mui, YBSelectField, YBToggle, YBToggleField } from '@yugabyte-ui-library/core';
import { useForm } from 'react-hook-form';
import { Trans, useTranslation } from 'react-i18next';
import { useQueryClient } from 'react-query';
import { toast } from 'react-toastify';

import { YBLoadingCircleIcon } from '@app/components/common/indicators';
import { YBModal, YBTooltip } from '@app/redesign/components';
import { createErrorMessage } from '@app/redesign/features/universe/universe-form/utils/helpers';
import { taskQueryKey, universeQueryKey } from '@app/redesign/helpers/api';
import {
  getGetUniverseQueryKey,
  useConfigureExportTelemetryConfig,
  useGetExportTelemetryConfig
} from '@app/v2/api/universe/universe';
import { YSQLAuditConfigClassesItem } from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';
import { AuditLogConfirmationModal } from './AuditLogConfirmationModal';
import {
  AUDIT_LOG_TRANSLATION_KEY_PREFIX,
  AuditLogFormValues,
  AuditLogOperation,
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
import clsx from 'clsx';

const { MenuItem } = mui;

const MODAL_NAME = 'AuditLogSettingsPanel';
const SIDE_PANEL_WIDTH = 736;

interface AuditLogSettingsPanelProps {
  open: boolean;
  operation: AuditLogOperation;
  universeUuid: string;
  universeName: string;
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
    padding: theme.spacing(2, 2.5),

    borderBottom: `1px solid ${theme.palette.grey[300]}`
  },
  nestedLevelSelect: {
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

export const AuditLogSettingsPanel: FC<AuditLogSettingsPanelProps> = ({
  open,
  operation,
  universeUuid,
  universeName,
  onClose
}) => {
  const classes = useStyles();
  const queryClient = useQueryClient();
  const { t } = useTranslation('translation', { keyPrefix: AUDIT_LOG_TRANSLATION_KEY_PREFIX });
  const [isConfirmationOpen, setIsConfirmationOpen] = useState(false);

  const telemetryConfigQuery = useGetExportTelemetryConfig(universeUuid, undefined, {
    query: { enabled: open && !!universeUuid }
  });
  const currentTelemetryConfig = telemetryConfigQuery.data;

  const formMethods = useForm<AuditLogFormValues>({
    defaultValues: getDefaultFormValues(currentTelemetryConfig?.audit_logs?.ysql_audit_config),
    mode: 'onChange'
  });
  const { control, handleSubmit, watch, reset, setValue } = formMethods;

  useEffect(() => {
    if (telemetryConfigQuery.isSuccess) {
      reset(getDefaultFormValues(currentTelemetryConfig?.audit_logs?.ysql_audit_config));
    }
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [telemetryConfigQuery.isSuccess, currentTelemetryConfig]);

  const auditLogClasses = watch('classes');
  const logClient = watch('logClient');

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
                        className={classes.nestedLevelSelect}
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
              </div>
            </div>
          </div>
        )}
      </YBModal>
      {isConfirmationOpen && (
        <AuditLogConfirmationModal
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
