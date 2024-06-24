import { FC, useState } from 'react';
import _ from 'lodash';
import { toast } from 'react-toastify';
import { useQuery, useMutation } from 'react-query';
import { useTranslation, Trans } from 'react-i18next';
import { useSelector, useDispatch } from 'react-redux';
import { Box, Typography, Link, Menu, MenuItem } from '@material-ui/core';
import { YBButton } from '../../../../components';
import { YBLoading } from '../../../../../components/common/indicators';
import { AuditLogSettings } from './components/AuditLogSettings';
import { DisableLogDialog } from './components/DisableLogDialog';
import { DisableExportDialog } from './components/DisableExportDialog';
import { YBLoadingCircleIcon } from '../../../../../components/common/indicators';
import { api, QUERY_KEY } from '../../../../utils/api';
import { disableLogPayload } from './utils/helper';
import { getPrimaryCluster } from '../../universe-form/utils/helpers';
import { createErrorMessage } from '../../universe-form/utils/helpers';
import { getUniversePendingTask } from '../../../../../components/universes/helpers/universeHelpers';
import { fetchUniverseInfo, fetchUniverseInfoResponse } from '../../../../../actions/universe';
import {
  fetchCustomerTasks,
  fetchCustomerTasksSuccess,
  fetchCustomerTasksFailure
} from '../../../../../actions/tasks';
import { AuditLogPayload } from './utils/types';
import { Universe } from '../../universe-form/utils/dto';
import { ExportLogResponse } from '../../../export-log/utils/types';
import { TP_FRIENDLY_NAMES } from '../../../export-log/utils/constants';
import { MODIFY_AUDITLOG_TASK_TYPE } from './utils/constants';

//styles
import { auditLogStyles } from './utils/AuditLogStyles';
//icons
import ArrowDropDownIcon from '@material-ui/icons/ArrowDropDown';
import EditLogIcon from '../../../../assets/edit.svg';
import DeleteIcon from '../../../../assets/delete.svg';
import AuditBackup from '../../../../assets/backup.svg';
import DisableExportIcon from '../../../../assets/disable_export.svg';

interface AuditLogProps {
  universeData: Universe;
  nodePrefix: string;
}

export const AuditLog: FC<AuditLogProps> = ({ universeData, nodePrefix }) => {
  const classes = auditLogStyles();
  const { t } = useTranslation();
  const tasks = useSelector((state: any) => state.tasks);
  const dispatch = useDispatch();
  const [anchorEl, setAnchorEl] = useState(null);
  const [openModal, setOpenModal] = useState(false);
  const [openDisableLogDialog, setDisableLogDialog] = useState(false);
  const [openDisableExportDialog, setDisableExportDialog] = useState(false);

  const { universeDetails, universeUUID } = universeData;
  const primaryCluster = _.cloneDeep(getPrimaryCluster(universeDetails));
  const universeName = _.get(primaryCluster, 'userIntent.universeName', '');

  const universePendingTask = getUniversePendingTask(universeUUID, tasks?.customerTaskList);
  const isTaskInProgress =
    universePendingTask?.type === MODIFY_AUDITLOG_TASK_TYPE &&
    universePendingTask?.status === 'Running';
  const auditLogInfo =
    !_.isEmpty(universePendingTask) && universePendingTask?.type === MODIFY_AUDITLOG_TASK_TYPE
      ? _.get(universePendingTask, 'details.auditLogConfig', undefined)
      : _.get(primaryCluster, 'userIntent.auditLogConfig', undefined);
  const isAuditLogEnabled = auditLogInfo?.ysqlAuditConfig?.enabled;
  const isExportEnabled = auditLogInfo?.exportActive;
  const exporterID = _.get(auditLogInfo, 'universeLogsExporterConfig[0].exporterUuid');

  const { data: exportLogData, isLoading, refetch } = useQuery<ExportLogResponse>(
    [QUERY_KEY.getTelemetryProviderByID],
    () => api.getTelemetryProviderByID(exporterID)
  );

  const updateAuditLogConfig = useMutation(
    (values: AuditLogPayload) => {
      return api.createAuditLogConfig(universeUUID, values);
    },
    {
      onSuccess: () => {
        toast.info('Audit Log update is in progress');
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
      },
      onError: (error) => {
        toast.error(createErrorMessage(error));
      }
    }
  );

  const disableAuditLog = async () => {
    try {
      await updateAuditLogConfig.mutateAsync(disableLogPayload);
    } catch (e) {
      toast.error(createErrorMessage(e));
    }
  };

  const disableExport = async () => {
    try {
      const payload = {
        installOtelCollector: true,
        auditLogConfig: {
          ysqlAuditConfig: auditLogInfo?.ysqlAuditConfig,
          exportActive: false
        }
      };
      await updateAuditLogConfig.mutateAsync(payload);
    } catch (e) {
      toast.error(createErrorMessage(e));
    }
  };

  const handleMenuClick = (event: any) => {
    setAnchorEl(event.currentTarget);
  };

  const handleMenuClose = () => {
    setAnchorEl(null);
  };

  const renderStatus = () => {
    if (isTaskInProgress) {
      return (
        <Box display="inline-flex" alignItems={'center'}>
          <YBLoadingCircleIcon variant="primary" size="xsmall" />
          &nbsp;{' '}
          <Typography className={classes.inProgresText}>{t('dbAuitLog.configuring')}</Typography>
        </Box>
      );
    } else
      return (
        <span className={classes.successStatus}>
          <i className="fa fa-check-circle" />
          &nbsp;{t('common.active')}
        </span>
      );
  };

  if (isLoading) return <YBLoading />;

  return (
    <Box display="flex" flexDirection="column" width="100%" p={0.25}>
      <Box mb={4}>
        <Box display={'flex'} flexDirection={'row'} justifyContent={'space-between'}>
          <Typography className={classes.mainTitle}>{t('dbAuitLog.header')}</Typography>
          {isAuditLogEnabled && (
            <YBButton
              variant="primary"
              size="large"
              onClick={handleMenuClick}
              endIcon={<ArrowDropDownIcon />}
              aria-controls="edit-menu"
              aria-haspopup="true"
              disabled={isTaskInProgress}
            >
              {t('dbAuitLog.editMenuTitle')}
            </YBButton>
          )}
          <Menu
            id="edit-menu"
            anchorEl={anchorEl}
            getContentAnchorEl={null}
            keepMounted
            open={Boolean(anchorEl)}
            onClose={handleMenuClose}
            anchorOrigin={{
              vertical: 'bottom',
              horizontal: 'right'
            }}
            transformOrigin={{
              vertical: 'top',
              horizontal: 'right'
            }}
          >
            <MenuItem
              className={classes.editMenuItem}
              onClick={() => {
                setOpenModal(true);
                handleMenuClose();
              }}
            >
              <img src={EditLogIcon} alt="--" /> &nbsp; {t('dbAuitLog.editLogAndExport')}
            </MenuItem>
            <Box className={classes.divider}></Box>
            {isExportEnabled && (
              <MenuItem
                className={classes.editMenuItem}
                onClick={() => {
                  setDisableExportDialog(true);
                  handleMenuClose();
                }}
              >
                <img src={DisableExportIcon} alt="--" /> &nbsp; {t('dbAuitLog.disableAuditExport')}
              </MenuItem>
            )}
            {isAuditLogEnabled && (
              <MenuItem
                className={classes.editMenuItem}
                onClick={() => {
                  setDisableLogDialog(true);
                  handleMenuClose();
                }}
              >
                <img src={DeleteIcon} alt="--" /> &nbsp; {t('dbAuitLog.disableAuditLog')}
              </MenuItem>
            )}
          </Menu>
        </Box>
      </Box>
      {isAuditLogEnabled ? (
        <Box className={classes.auditLogCard}>
          <Typography className={classes.cardHeader}>{t('dbAuitLog.auditLogCardTitle')}</Typography>
          <Box mt={3}>
            <Typography className={classes.cardSubtitle}>
              {t('dbAuitLog.auditLogCardStatus')}
            </Typography>
            {renderStatus()}
          </Box>
          <Box mt={3}>
            <Typography className={classes.cardSubtitle}>
              {t('dbAuitLog.auditLogCardLocation')}
            </Typography>
            <Typography variant="body2">postgres.conf</Typography>
          </Box>
          {/* Divider */}
          <Box className={classes.divider} mt={4} mb={4}></Box>
          <Typography className={classes.cardHeader}>
            {t('dbAuitLog.auditExportCardTitle')}
          </Typography>
          {isExportEnabled ? (
            <>
              <Box mt={3}>
                <Typography className={classes.cardSubtitle}>
                  {t('dbAuitLog.auditExportCardStatus')}
                </Typography>
                {renderStatus()}
              </Box>
              <Box mt={3} display="flex" flexDirection="row">
                <Box mr={15}>
                  <Typography className={classes.cardSubtitle}>
                    {t('dbAuitLog.auditExportName')}
                  </Typography>
                  <Typography variant="body2">{exportLogData?.name}</Typography>
                </Box>
                <Box>
                  <Typography className={classes.cardSubtitle}>
                    {t('dbAuitLog.auditExportingTo')}
                  </Typography>
                  <Typography variant="body2">
                    {exportLogData ? TP_FRIENDLY_NAMES[exportLogData?.config?.type] : '--'}
                  </Typography>
                </Box>
              </Box>
            </>
          ) : (
            <>
              <Box className={classes.emptyExportContainer} mt={3}>
                <img src={AuditBackup} alt="--" height={48} width={48} />
                <Box mt={2} mb={2}>
                  <YBButton
                    variant="secondary"
                    size="large"
                    onClick={() => setOpenModal(true)}
                    disabled={isTaskInProgress}
                  >
                    {t('dbAuitLog.exportButton')}
                  </YBButton>
                </Box>
                <Typography variant="body2">{t('dbAuitLog.emptyExportLine1')}</Typography>
                <Typography variant="body2">{t('dbAuitLog.emptyExportLine2')}</Typography>
              </Box>
            </>
          )}
        </Box>
      ) : (
        <Box className={classes.emptyContainer}>
          <Box className={classes.innerEmptyContainer}>
            <Box display={'flex'} flexDirection={'column'} alignItems={'center'}>
              <img src={AuditBackup} alt="--" height={48} width={48} />
              <Box mt={3} mb={3}>
                <YBButton
                  variant="primary"
                  size="large"
                  onClick={() => setOpenModal(true)}
                  disabled={isTaskInProgress}
                >
                  {t('dbAuitLog.enableDatabaseLog')}
                </YBButton>
              </Box>
              <Typography variant="body2">{t('dbAuitLog.emptyMsg1')}</Typography>
              <Typography variant="body2">{t('dbAuitLog.emptyMsg2')}</Typography>
              <br />
              <Typography variant="body2">
                <Trans>
                  {t('dbAuitLog.learnMoreLog')}
                  <Link
                    target="_blank"
                    underline="always"
                    href="https://docs.yugabyte.com/preview/secure/audit-logging/audit-logging-ysql/"
                  ></Link>
                </Trans>
              </Typography>
            </Box>
            {/* Note */}
            <Box>
              <Typography variant="body2">
                <Trans i18nKey={'dbAuitLog.noteMsg'} />
              </Typography>
            </Box>
          </Box>
        </Box>
      )}
      {openModal && (
        <AuditLogSettings
          open={openModal}
          onClose={() => setOpenModal(false)}
          auditLogInfo={isAuditLogEnabled ? auditLogInfo : undefined}
          universeUUID={universeUUID}
          universeName={universeName}
        />
      )}
      <DisableLogDialog
        open={openDisableLogDialog}
        onClose={() => setDisableLogDialog(false)}
        universeName={universeName}
        onSubmit={() => {
          disableAuditLog();
          setDisableLogDialog(false);
        }}
      />
      <DisableExportDialog
        open={openDisableExportDialog}
        onClose={() => setDisableExportDialog(false)}
        universeName={universeName}
        onSubmit={() => {
          disableExport();
          setDisableExportDialog(false);
        }}
      />
    </Box>
  );
};
