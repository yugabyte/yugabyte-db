/*
 * Created on Mon Sep 09 2024
 *
 * Copyright 2021 YugabyteDB, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { FC, useState } from 'react';
import { useMutation, useQueryClient } from 'react-query';
import { Trans, useTranslation } from 'react-i18next';
import { toast } from 'react-toastify';
import clsx from 'clsx';
import cronstrue from 'cronstrue';

import { Grid, makeStyles, Typography } from '@material-ui/core';
import { DropdownButton, MenuItem } from 'react-bootstrap';
import { YBToggle } from '../../../../components';
import { YBTag, YBTag_Types } from '../../../../../components/common/YBTag';
import { YBConfirmModal } from '../../../../../components/modals';
import EditScheduledPolicyModal from './EditScheduledPolicyModal';
import ScheduledBackupShowIntervalsModal from './ScheduledBackupShowIntervalsModal';
import ScheduledPolicyShowTables from './ScheduledPolicyShowTables';
import { IBackupSchedule, IBackupScheduleStatus } from '../../../../../components/backupv2';
import { convertMsecToTimeFrame } from '../../../../../components/backupv2/scheduled/ScheduledBackupUtils';
import { ybFormatDate } from '../../../../helpers/DateUtils';
import { RbacValidator } from '../../../rbac/common/RbacApiPermValidator';
import { ApiPermissionMap } from '../../../rbac/ApiAndUserPermMapping';
import { deleteSchedulePolicy, toggleScheduledBackupPolicy } from '../api/api';
import { TableType, TableTypeLabel } from '../../../../helpers/dtos';

interface ScheduledCardProps {
  schedule: IBackupSchedule;
  universeUUID: string;
  storageConfig: Record<string, string> | undefined;
}
const useStyles = makeStyles((theme) => ({
  card: {
    borderRadius: '8px',
    border: `1px solid ${theme.palette.ybacolors.ybBorderGray}`,
    boxShadow: `0px 4px 10px 0px rgba(0, 0, 0, 0.05)`
  },
  header: {
    height: '56px',
    display: 'flex',
    justifyContent: 'space-between',
    padding: '8px 24px',
    borderBottom: `1px solid ${theme.palette.ybacolors.ybBorderGray}`,
    '&>.dropdown>button': {
      height: '40px'
    }
  },
  title: {
    fontWeight: 600,
    gap: '6px',
    '& .yb-tag': {
      margin: 0
    },
    '&>p': {
      fontSize: '16px'
    },
    '&>.MuiFormControl-root': {
      marginLeft: '24px'
    }
  },
  content: {
    padding: '24px',
    display: 'flex',
    justifyContent: 'space-between',
    borderBottom: `1px solid ${theme.palette.ybacolors.ybBorderGray}`
  },
  attribute: {
    textTransform: 'uppercase',
    fontWeight: 500,
    fontSize: '11.5px',
    color: theme.palette.grey[600],
    lineHeight: '16px'
  },
  value: {
    fontSize: '13px',
    fontWeight: 400,
    color: theme.palette.grey[900],
    marginTop: '6px'
  },
  details: {
    borderRadius: '8px',
    background: '#FBFBFB',
    border: `1px solid ${theme.palette.ybacolors.ybBorderGray}`,
    width: '600px',
    padding: '16px',
    display: 'flex',
    gap: '60px',
    '&>div': {
      display: 'flex',
      flexDirection: 'column',
      gap: '8px'
    },
    '&>div:nth-child(2)': {
      fontSize: '13px',
      fontWeight: 600,
      lineHeight: '16px'
    }
  },
  nextScheduledDates: {
    display: 'flex',
    gap: '55px',
    '&>div': {
      width: '130px'
    }
  },
  footer: {
    height: '72px',
    padding: '16px 24px',
    display: 'flex',
    gap: '40px'
  },
  link: {
    textDecoration: 'underline',
    color: theme.palette.ybacolors.labelBackground,
    '&:hover': {
      color: theme.palette.ybacolors.labelBackground
    },
    cursor: 'pointer'
  },
  inactive: {
    color: theme.palette.grey[500],
    '&:hover': {
      color: theme.palette.grey[500]
    }
  }
}));

type toogleScheduleProps = Partial<IBackupSchedule> & Pick<IBackupSchedule, 'scheduleUUID'>;

export const ScheduledCard: FC<ScheduledCardProps> = ({
  schedule,
  universeUUID,
  storageConfig
}) => {
  const classes = useStyles();
  const [showDeleteModal, setShowDeleteModal] = useState('');
  const [showEditModal, setShowEditModal] = useState(false);
  const [showIntervalsModal, setShowIntervalsModal] = useState(false);
  const [showTablesModal, setShowTablesModal] = useState(false);
  const { t } = useTranslation('translation', {
    keyPrefix: 'backup.scheduled.list'
  });
  let backupInterval = '';

  if (schedule.cronExpression) {
    backupInterval = cronstrue.toString(schedule.cronExpression);
  } else {
    backupInterval = convertMsecToTimeFrame(
      schedule.frequency,
      schedule.frequencyTimeUnit,
      'Every '
    );
  }

  const queryClient = useQueryClient();

  const toggleSchedule = useMutation(
    (val: toogleScheduleProps) => toggleScheduledBackupPolicy(universeUUID, val),
    {
      onSuccess: (_, params: any) => {
        toast.success(`Schedule policy is now ${params.status}`);
        queryClient.invalidateQueries('scheduled_backup_list');
      },
      onError: (resp: any) => {
        toast.error(resp.response.data.error);
      }
    }
  );

  const deleteSchedule = useMutation(
    () => deleteSchedulePolicy(universeUUID, schedule.scheduleUUID),

    {
      onSuccess: () => {
        toast.success(t('deleteMsg'));
        queryClient.invalidateQueries('scheduled_backup_list');
      },
      onError: (resp: any) => {
        toast.error(resp.response.data.error);
      }
    }
  );

  const isScheduleEnabled = schedule.status === IBackupScheduleStatus.ACTIVE;
  const isScheduleCreating = schedule.status === IBackupScheduleStatus.CREATING;

  const wrapTableName = (tablesList: string[] | undefined, isKeyspace = false) => {
    if (!Array.isArray(tablesList) || tablesList.length === 0) {
      return '-';
    }
    if (tablesList.length === 1) {
      return <span>{tablesList[0]}</span>;
    }
    return (
      <span
        className={clsx(classes.link, !isScheduleEnabled && classes.inactive)}
        onClick={() => {
          setShowTablesModal(true);
        }}
      >
        {tablesList.length} {isKeyspace ? ' Keyspaces' : ' Tables'}
      </span>
    );
  };

  return (
    <div className={clsx(classes.card, !isScheduleEnabled && classes.inactive)}>
      <div className={classes.header}>
        <Grid container className={classes.title} alignItems="center">
          <Typography variant="body1">{schedule.scheduleName}</Typography>
          <YBTag type={YBTag_Types.YB_GRAY}>
            {TableTypeLabel[schedule.backupInfo.backupType ?? '-']}
          </YBTag>
          <YBToggle
            checked={isScheduleEnabled}
            label={
              isScheduleEnabled ? t('enabled') : isScheduleCreating ? t('creating') : t('disabled')
            }
            onClick={(e: any) => {
              toggleSchedule.mutate({
                scheduleUUID: schedule.scheduleUUID,
                frequency: schedule.frequency,
                cronExpression: schedule.cronExpression,
                status: e.target.checked
                  ? IBackupScheduleStatus.ACTIVE
                  : IBackupScheduleStatus.STOPPED,
                frequencyTimeUnit: schedule.frequencyTimeUnit
              });
            }}
          />
        </Grid>
        <DropdownButton
          className="actions-btn"
          title="Actions"
          id="schedule-backup-actions-dropdown"
          pullRight
          onClick={(e) => e.stopPropagation()}
        >
          <RbacValidator
            accessRequiredOn={{
              ...ApiPermissionMap.MODIFY_SCHEDULE,
              onResource: universeUUID
            }}
            isControl
            overrideStyle={{
              display: 'unset'
            }}
          >
            <MenuItem
              onClick={() => {
                setShowEditModal(true);
              }}
            >
              <i className="fa fa-pencil"></i> {t('editPolicy')}
            </MenuItem>
          </RbacValidator>
          <RbacValidator
            accessRequiredOn={ApiPermissionMap.DELETE_SCHEDULE}
            isControl
            overrideStyle={{
              display: 'unset'
            }}
          >
            <MenuItem
              onClick={() => {
                setShowDeleteModal(schedule.scheduleUUID);
              }}
              className="action-danger"
            >
              <i className="fa fa-trash"></i> {t('deletePolicy')}
            </MenuItem>
          </RbacValidator>
        </DropdownButton>
      </div>
      <div className={classes.content}>
        <div className={classes.details}>
          <div className={clsx(classes.attribute, !isScheduleEnabled && classes.inactive)}>
            <span>{t('scope')}</span>
            <span>{t('keyspace')}</span>
            <span>{t('tables')}</span>
            {schedule.backupInfo.backupType === TableType.PGSQL_TABLE_TYPE && (
              <span>{t('rolesAndGrants')}</span>
            )}
          </div>
          <div>
            <span>{schedule.backupInfo?.fullBackup ? t('fullBackup') : t('tableBackup')}</span>
            <span>
              {wrapTableName(
                schedule.backupInfo?.keyspaceList?.map((k) => k.keyspace),
                true
              )}
            </span>
            <span>{wrapTableName(schedule.backupInfo?.keyspaceList?.[0]?.tablesList)}</span>
            {schedule.backupInfo.backupType === TableType.PGSQL_TABLE_TYPE && (
              <span>{schedule.backupInfo.useRoles ? 'Included' : 'Not Included'}</span>
            )}
          </div>
        </div>
        <div className={classes.nextScheduledDates}>
          <div>
            <div className={clsx(classes.attribute, !isScheduleEnabled && classes.inactive)}>
              {t('lastBackup')}
            </div>
            <div className={clsx(classes.value, !isScheduleEnabled && classes.inactive)}>
              {schedule.prevCompletedTask ? ybFormatDate(schedule.prevCompletedTask) : '-'}
            </div>
          </div>
          <div>
            <div className={clsx(classes.attribute, !isScheduleEnabled && classes.inactive)}>
              {t('nextBackup')}
            </div>
            <div className={clsx(classes.value, !isScheduleEnabled && classes.inactive)}>
              {schedule.nextExpectedTask ? ybFormatDate(schedule.nextExpectedTask) : '-'}
            </div>
          </div>
        </div>
      </div>
      <div className={classes.footer}>
        <div>
          <div className={clsx(classes.attribute, !isScheduleEnabled && classes.inactive)}>
            {t('backupIntervals')}
          </div>
          <div className={clsx(classes.value, !isScheduleEnabled && classes.inactive)}>
            {backupInterval} -{' '}
            <Trans
              t={t}
              i18nKey="details"
              components={{
                a: (
                  <a
                    className={clsx(classes.link, !isScheduleEnabled && classes.inactive)}
                    onClick={() => {
                      setShowIntervalsModal(true);
                    }}
                  />
                )
              }}
            />
          </div>
        </div>
        <div>
          <div className={clsx(classes.attribute, !isScheduleEnabled && classes.inactive)}>
            {t('incrementalBackup')}
          </div>
          <div className={clsx(classes.value, !isScheduleEnabled && classes.inactive)}>
            {schedule.incrementalBackupFrequency === 0 ? t('disabled') : t('enabled')}
          </div>
        </div>
        <div>
          <div className={clsx(classes.attribute, !isScheduleEnabled && classes.inactive)}>
            {t('pitr')}
          </div>
          <div className={clsx(classes.value, !isScheduleEnabled && classes.inactive)}>
            {schedule.backupInfo.pointInTimeRestoreEnabled ? t('enabled') : t('disabled')}
          </div>
        </div>
        <div>
          <div className={clsx(classes.attribute, !isScheduleEnabled && classes.inactive)}>
            {t('retentionPeriod')}
          </div>
          <div className={clsx(classes.value, !isScheduleEnabled && classes.inactive)}>
            {schedule.backupInfo?.timeBeforeDelete
              ? convertMsecToTimeFrame(
                  schedule.backupInfo.timeBeforeDelete,
                  schedule.backupInfo.expiryTimeUnit ?? 'DAYS'
                )
              : 'Indefinitely'}
          </div>
        </div>
        <div>
          <div className={clsx(classes.attribute, !isScheduleEnabled && classes.inactive)}>
            {t('storageConfig')}
          </div>
          <div className={clsx(classes.value, !isScheduleEnabled && classes.inactive)}>
            {storageConfig ? (
              <a
                target="_blank"
                className={clsx(classes.link, !isScheduleEnabled && classes.inactive)}
                href={`/config/backup/${storageConfig ? storageConfig.name.toLowerCase() : ''}`}
                rel="noreferrer"
              >
                {storageConfig.configName}
              </a>
            ) : (
              '-'
            )}
          </div>
        </div>
      </div>
      <YBConfirmModal
        name="delete-alert-destination"
        title="Confirm Delete"
        onConfirm={() => deleteSchedule.mutateAsync()}
        currentModal={schedule.scheduleUUID}
        visibleModal={showDeleteModal}
        hideConfirmModal={() => {
          setShowDeleteModal('');
        }}
      >
        {t('deleteModal.deleteMsg')}
      </YBConfirmModal>
      {showEditModal && (
        <EditScheduledPolicyModal
          visible={showEditModal}
          onHide={() => {
            setShowEditModal(false);
          }}
          universeUUID={universeUUID}
          schedule={schedule}
        />
      )}

      <ScheduledBackupShowIntervalsModal
        schedule={schedule}
        visible={showIntervalsModal}
        onHide={() => {
          setShowIntervalsModal(false);
        }}
      />
      <ScheduledPolicyShowTables
        schedule={schedule}
        visible={showTablesModal}
        onHide={() => {
          setShowTablesModal(false);
        }}
      />
    </div>
  );
};
