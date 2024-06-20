import { FC, useState } from 'react';
import _ from 'lodash';
import { toast } from 'react-toastify';
import { useDispatch } from 'react-redux';
import { browserHistory } from 'react-router';
import { useQuery, useMutation } from 'react-query';
import { useTranslation, Trans } from 'react-i18next';
import { useForm, FormProvider } from 'react-hook-form';
import { Box, Typography, Link, MenuItem } from '@material-ui/core';
import {
  YBSidePanel,
  YBLabel,
  YBTooltip,
  YBToggle,
  YBToggleField,
  YBSelectField,
  YBModal
} from '../../../../../components';
import { api, QUERY_KEY } from '../../../../../utils/api';
import { constructFormPayload } from '../utils/helper';
import { createErrorMessage } from '../../../universe-form/utils/helpers';
import { fetchUniverseInfo, fetchUniverseInfoResponse } from '../../../../../../actions/universe';
import {
  fetchCustomerTasks,
  fetchCustomerTasksSuccess,
  fetchCustomerTasksFailure
} from '../../../../../../actions/tasks';
import {
  AuditLogFormFields,
  AuditLogPayload,
  YSQLAuditLogLevel,
  AuditLogConfig
} from '../utils/types';
import { ExportLogResponse } from '../../../../export-log/utils/types';
import { TP_FRIENDLY_NAMES } from '../../../../export-log/utils/constants';
import { YSQL_AUDIT_CLASSES, YSQL_LOG_LEVEL_OPTIONS } from '../utils/constants';

//styles
import { auditLogStyles } from '../utils/AuditLogStyles';
//icons
import TreeIcon from '../../../../../assets/tree.svg';
import AddCircleIcon from '../.././../../../assets/add-circle.svg';
import InfoMessageIcon from '../../../../../assets/info-message.svg';

interface AuditLogSettingProps {
  open: boolean;
  onClose: () => void;
  universeUUID: string;
  auditLogInfo: AuditLogConfig | undefined;
  universeName: string;
}

interface LabelWithTooltipProps {
  label: string;
  tooltip: string;
}

export const LabelWithTooltip: FC<LabelWithTooltipProps> = ({ label, tooltip }) => {
  return (
    <YBLabel width="200px">
      {label} &nbsp;{' '}
      <YBTooltip title={tooltip}>
        <img src={InfoMessageIcon} alt="--" />
      </YBTooltip>
    </YBLabel>
  );
};

export const AuditLogSettings: FC<AuditLogSettingProps> = ({
  open,
  onClose,
  universeUUID,
  auditLogInfo,
  universeName
}) => {
  const classes = auditLogStyles();
  const { t } = useTranslation();
  const dispatch = useDispatch();
  const [openConfirmationDialog, setConfirmationDialog] = useState(false);
  const [openExportNavDialog, setExportNavDialog] = useState(false);

  const defaultValues = constructFormPayload(auditLogInfo);

  const { data: telemetryProviders, isLoading } = useQuery<ExportLogResponse[]>(
    [QUERY_KEY.getAllTelemetryProviders],
    () => api.getAllTelemetryProviders()
  );

  const formMethods = useForm<AuditLogFormFields>({
    defaultValues: defaultValues,
    mode: 'onChange',
    reValidateMode: 'onChange'
  });

  const { control, handleSubmit, watch, setValue } = formMethods;

  const createAuditLogConfig = useMutation(
    (values: AuditLogPayload) => {
      return api.createAuditLogConfig(universeUUID, values);
    },
    {
      onSuccess: () => {
        dispatch(fetchCustomerTasks() as any).then((response: any) => {
          if (!response.error) {
            dispatch(fetchCustomerTasksSuccess(response.payload));
          } else {
            dispatch(fetchCustomerTasksFailure(response.payload));
          }
        });
        //Universe upgrade state is not updating immediately
        setTimeout(() => {
          dispatch(fetchUniverseInfo(universeUUID) as any).then((response: any) => {
            dispatch(fetchUniverseInfoResponse(response.payload));
          });
        }, 1000);
        onClose();
      },
      onError: (error) => {
        toast.error(createErrorMessage(error));
      }
    }
  );

  const auditLogClasses = watch('classes');
  const pgAuditClient = watch('logClient');
  const logLevelValue = watch('logLevel');
  const exportActiveValue = watch('exportActive');

  const handleFormSubmit = handleSubmit(async (values) => {
    const { exportActive, exporterUuid, ...ysqlAuditConfig } = values;
    try {
      const payload: AuditLogPayload = {
        installOtelCollector: true,
        auditLogConfig: {
          exportActive: exportActive,
          ysqlAuditConfig: { enabled: true, ...ysqlAuditConfig },
          ...(exportActive &&
            exporterUuid && {
              universeLogsExporterConfig: [
                {
                  exporterUuid
                }
              ]
            })
        }
      };
      await createAuditLogConfig.mutateAsync(payload);
    } catch (e) {
      toast.error(createErrorMessage(e));
    }
  });

  const renderOption = (
    name: keyof AuditLogFormFields,
    label: string,
    tooltip: string,
    border = true
  ) => {
    return (
      <Box className={border ? classes.logOption : classes.logOptionWithoutBorder}>
        <LabelWithTooltip label={label} tooltip={tooltip} />
        <YBToggleField control={control} name={name} />
      </Box>
    );
  };

  return (
    <YBSidePanel
      open={open}
      onClose={onClose}
      title={t('dbAuitLog.settingsModal.configureDBLog')}
      submitLabel={t('dbAuitLog.settingsModal.submitLabel')}
      cancelLabel="Cancel"
      overrideWidth={680}
      onSubmit={() => setConfirmationDialog(true)}
    >
      <FormProvider {...formMethods}>
        <Box height="100%" width="100%" display="flex" pl={1} pr={1} flexDirection={'column'}>
          <Box display={'flex'} flexDirection={'column'}>
            <Typography className={classes.modalTitle}>
              {t('dbAuitLog.settingsModal.title')}
            </Typography>
            <br />
            <Typography variant="body2">
              <Trans>
                {t('dbAuitLog.settingsModal.mainMsg')}
                <Link underline="always" target="_blank" href="https://www.pgaudit.org/"></Link>
                <Link
                  underline="always"
                  target="_blank"
                  href="https://docs.yugabyte.com/preview/secure/audit-logging/audit-logging-ysql/"
                ></Link>
              </Trans>
            </Typography>
          </Box>
          <Box display={'flex'} flexDirection={'column'} mt={2}>
            <Typography variant="body1">
              {t('dbAuitLog.settingsModal.ysqlStatementTitle')}
            </Typography>
            <Box mt={2} ml={2} className={classes.optionContainer}>
              {YSQL_AUDIT_CLASSES.map((ac, i) => {
                const currentIndex = auditLogClasses.findIndex((a) => a === ac.value);
                const isSelected = Boolean(currentIndex > -1);
                return (
                  <Box
                    key={i}
                    className={i == 0 ? classes.logOptionWithoutBorder : classes.logOption}
                  >
                    <LabelWithTooltip label={ac.title} tooltip={ac.tooltip} />
                    <YBToggle
                      checked={isSelected}
                      onChange={(e) => {
                        let exisitingArr = [...auditLogClasses];
                        if (isSelected) {
                          exisitingArr.splice(currentIndex, 1);
                          setValue('classes', exisitingArr);
                        } else setValue('classes', [...exisitingArr, ac.value]);
                      }}
                    />
                  </Box>
                );
              })}
            </Box>
          </Box>
          <Box display={'flex'} flexDirection={'column'} mt={4.5}>
            <Typography variant="body1">
              {t('dbAuitLog.settingsModal.ysqlLogSettingsTitle')}
            </Typography>
            <Box mt={2} ml={2} className={classes.optionContainer}>
              {renderOption(
                'logCatalog',
                'pgaudit.log_catalog',
                t('dbAuitLog.tooltips.logCatalog'),
                false
              )}
              <Box className={classes.auditLogClient}>
                <Box className={classes.clientToggleC}>
                  <LabelWithTooltip
                    label={'pgaudit.log_client'}
                    tooltip={t('dbAuitLog.tooltips.logClient')}
                  />
                  <YBToggleField control={control} name="logClient" />
                </Box>
                {pgAuditClient && (
                  <Box display="flex" flexDirection={'row'} width="100%" pt={2} pr={3}>
                    <img src={TreeIcon} alt="--" /> &nbsp;
                    <LabelWithTooltip label={'Level'} tooltip={t('dbAuitLog.tooltips.logLevel')} />
                    <YBSelectField
                      name={'logLevel'}
                      control={control}
                      fullWidth
                      defaultValue={logLevelValue ? logLevelValue : YSQLAuditLogLevel.LOG}
                      inputProps={{
                        'data-testid': 'DefaultRegionField-Select'
                      }}
                    >
                      {YSQL_LOG_LEVEL_OPTIONS.map((logOption) => (
                        <MenuItem key={logOption.value} value={logOption.value}>
                          {logOption.label}
                        </MenuItem>
                      ))}
                    </YBSelectField>
                  </Box>
                )}
              </Box>
              {renderOption(
                'logParameter',
                'pgaudit.log_parameter',
                t('dbAuitLog.tooltips.logParameter')
              )}
              {renderOption(
                'logRelation',
                'pgaudit.log_relation',
                t('dbAuitLog.tooltips.logRelation')
              )}
              {renderOption(
                'logStatement',
                'pgaudit.log_statement',
                t('dbAuitLog.tooltips.logStatement')
              )}
              {renderOption(
                'logStatementOnce',
                'pgaudit.log_statement_once',
                t('dbAuitLog.tooltips.logStatementOnce')
              )}
            </Box>
          </Box>
          <Box display={'flex'} flexDirection={'column'} mt={4.5}>
            <Typography variant="body1">{t('dbAuitLog.settingsModal.exportLogTitle')}</Typography>
            <Box mt={2} ml={2} mb={2} className={classes.optionContainer}>
              <Box className={classes.logOptionWithoutBorder}>
                <LabelWithTooltip
                  label={'Export Audit Log'}
                  tooltip={t('dbAuitLog.tooltips.exportLog')}
                />
                <YBToggleField control={control} name="exportActive" />
              </Box>
              {exportActiveValue && (
                <Box
                  display="flex"
                  flexDirection={'column'}
                  paddingX={3}
                  paddingY={2}
                  width="400px"
                >
                  <YBLabel>{t('dbAuitLog.settingsModal.logExportConfig')}</YBLabel>
                  {!isLoading && telemetryProviders && telemetryProviders?.length > 0 && (
                    <YBSelectField
                      name={'exporterUuid'}
                      control={control}
                      fullWidth
                      renderValue={(value) =>
                        telemetryProviders?.find((tp) => tp.uuid === value)?.name
                      }
                    >
                      {telemetryProviders?.map((tp) => {
                        return (
                          <MenuItem
                            className={classes.exportMenuItem}
                            key={tp.name}
                            value={tp?.uuid}
                          >
                            <Typography variant="body1">{tp?.name}</Typography>
                            <Typography variant="subtitle1" color="textSecondary">
                              {tp?.config?.type ? TP_FRIENDLY_NAMES[tp?.config?.type] : '--'}
                            </Typography>
                          </MenuItem>
                        );
                      })}
                      <Box className={classes.divider}></Box>
                      <MenuItem
                        value=""
                        className={classes.createExportMenuItem}
                        onClick={() => setExportNavDialog(true)}
                      >
                        <img src={AddCircleIcon} alt="--" /> &nbsp;{' '}
                        <Typography variant="inherit">
                          {t('dbAuitLog.settingsModal.createExportConfig')}
                        </Typography>
                      </MenuItem>
                    </YBSelectField>
                  )}
                </Box>
              )}
            </Box>
            {_.isEmpty(telemetryProviders) && (
              <Box ml={2} mb={3} className={classes.exportInfo}>
                <Typography className={classes.exportInfoText}>
                  <Trans>
                    {t('dbAuitLog.exportNavLink')}
                    <Link
                      component={'button'}
                      underline="always"
                      onClick={(e) => {
                        setExportNavDialog(true);
                        e.preventDefault();
                      }}
                    />
                  </Trans>
                </Typography>
              </Box>
            )}
          </Box>
        </Box>
        <LogconfirmationDialog
          open={openConfirmationDialog}
          onClose={() => setConfirmationDialog(false)}
          onSubmit={handleFormSubmit}
          isEditMode={Boolean(auditLogInfo)}
          universeName={universeName}
        />
        <ExportNavigateDialog
          open={openExportNavDialog}
          onClose={() => setExportNavDialog(false)}
        />
      </FormProvider>
    </YBSidePanel>
  );
};

interface LogconfirmationDialogProps {
  open: boolean;
  onClose: () => void;
  onSubmit: () => void;
  isEditMode: boolean;
  universeName: string;
}

export const LogconfirmationDialog: FC<LogconfirmationDialogProps> = ({
  open,
  onClose,
  onSubmit,
  isEditMode,
  universeName
}) => {
  const classes = auditLogStyles();
  const { t } = useTranslation();
  return (
    <YBModal
      open={open}
      onClose={onClose}
      overrideHeight={'300px'}
      overrideWidth={'680px'}
      size="sm"
      title={
        isEditMode
          ? t('dbAuitLog.settingsModal.editAuditLog')
          : t('dbAuitLog.settingsModal.configureDBLog')
      }
      submitLabel={isEditMode ? t('common.applyChanges') : t('dbAuitLog.enableDatabaseLog')}
      cancelLabel={t('common.back')}
      onSubmit={onSubmit}
    >
      <Box pt={1} pl={1.5} pr={5.5}>
        <Typography className={classes.exportInfoText}>
          <Trans
            i18nKey={'dbAuitLog.settingsModal.confirmationDialogMsg'}
            values={{ universeName, typeVal: isEditMode ? 'Edit' : 'Enabling' }}
          />
        </Typography>
      </Box>
    </YBModal>
  );
};

interface ExportNavigateDialogProps {
  open: boolean;
  onClose: () => void;
}

export const ExportNavigateDialog: FC<ExportNavigateDialogProps> = ({ open, onClose }) => {
  const { t } = useTranslation();
  return (
    <YBModal
      open={open}
      onClose={onClose}
      overrideHeight={'260px'}
      overrideWidth={'520px'}
      size="sm"
      title={t('dbAuitLog.settingsModal.leavePageModalTitle')}
      submitLabel={t('dbAuitLog.settingsModal.leaveModalSubmit')}
      cancelLabel={t('common.back')}
      onSubmit={() => {
        browserHistory.push('/config/log');
      }}
    >
      <Box pt={1} pl={1.5} pr={1.5}>
        <Typography variant="body2">
          <Trans i18nKey={'dbAuitLog.exportNavDialogMsg'}></Trans>
        </Typography>
      </Box>
    </YBModal>
  );
};
