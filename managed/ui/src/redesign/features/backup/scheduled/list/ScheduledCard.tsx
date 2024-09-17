/*
 * Created on Mon Sep 09 2024
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
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
import { YBConfirmModal } from '../../../../../components/modals';
import EditScheduledPolicyModal from './EditScheduledPolicyModal';
import ScheduledBackupShowIntervalsModal from './ScheduledBackupShowIntervalsModal';
import ScheduledPolicyShowTables from './ScheduledPolicyShowTables';
import { convertMsecToTimeFrame } from '../../../../../components/backupv2/scheduled/ScheduledBackupUtils';
import { ybFormatDate } from '../../../../helpers/DateUtils';
import { RbacValidator } from '../../../rbac/common/RbacApiPermValidator';
import { ApiPermissionMap } from '../../../rbac/ApiAndUserPermMapping';
import { deleteSchedulePolicy, toggleScheduledBackupPolicy } from '../api/api';
import { TableTypeLabel } from '../../../../helpers/dtos';
import { YBTag, YBTag_Types } from '../../../../../components/common/YBTag';
import { IBackupSchedule, IBackupScheduleStatus } from '../../../../../components/backupv2';

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
    opacity: 0.5
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

  const wrapTableName = (tablesList: string[] | undefined, isKeyspace = false) => {
    if (!Array.isArray(tablesList) || tablesList.length === 0) {
      return '-';
    }
    if (tablesList.length === 1) {
      return <span>{tablesList[0]}</span>;
    }
    return (
      <span
        className={classes.link}
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
            label={isScheduleEnabled ? t('enabled') : t('disabled')}
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
                if (!isScheduleEnabled) return;
                setShowEditModal(true);
              }}
              disabled={schedule.status !== IBackupScheduleStatus.ACTIVE}
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
          <div className={classes.attribute}>
            <span>{t('scope')}</span>
            <span>{t('keyspace')}</span>
            <span>{t('tables')}</span>
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
          </div>
        </div>
        <div className={classes.nextScheduledDates}>
          <div>
            <div className={classes.attribute}>{t('lastBackup')}</div>
            <div className={classes.value}>
              {schedule.prevCompletedTask ? ybFormatDate(schedule.prevCompletedTask) : '-'}
            </div>
          </div>
          <div>
            <div className={classes.attribute}>{t('nextBackup')}</div>
            <div className={classes.value}>
              {schedule.nextExpectedTask ? ybFormatDate(schedule.nextExpectedTask) : '-'}
            </div>
          </div>
        </div>
      </div>
      <div className={classes.footer}>
        <div>
          <div className={classes.attribute}>{t('backupIntervals')}</div>
          <div className={classes.value}>
            {backupInterval} -{' '}
            <Trans
              t={t}
              i18nKey="details"
              components={{
                a: (
                  <a
                    className={classes.link}
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
          <div className={classes.attribute}>{t('incrementalBackup')}</div>
          <div className={classes.value}>
            {schedule.incrementalBackupFrequency === 0 ? t('disabled') : t('enabled')}
          </div>
        </div>
        <div>
          <div className={classes.attribute}>{t('pitr')}</div>
          <div className={classes.value}>
            {schedule.backupInfo.pointInTimeRestoreEnabled ? t('enabled') : t('disabled')}
          </div>
        </div>
        <div>
          <div className={classes.attribute}>{t('retentionPeriod')}</div>
          <div className={classes.value}>
            {schedule.backupInfo?.timeBeforeDelete
              ? convertMsecToTimeFrame(
                  schedule.backupInfo.timeBeforeDelete,
                  schedule.backupInfo.expiryTimeUnit ?? 'DAYS'
                )
              : 'Indefinitely'}
          </div>
        </div>
        <div>
          <div className={classes.attribute}>{t('storageConfig')}</div>
          <div className={classes.value}>
            {storageConfig ? (
              <a
                target="_blank"
                className={classes.link}
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
      <EditScheduledPolicyModal
        visible={showEditModal}
        onHide={() => {
          setShowEditModal(false);
        }}
        universeUUID={universeUUID}
        schedule={schedule}
      />
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
