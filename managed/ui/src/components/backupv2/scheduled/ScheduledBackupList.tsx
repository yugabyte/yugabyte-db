/*
 * Created on Thu Mar 31 2022
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import React, { FC, useMemo, useState } from 'react';
import { Col, DropdownButton, MenuItem, Row } from 'react-bootstrap';
import { useInfiniteQuery, useMutation, useQueryClient } from 'react-query';
import { toast } from 'react-toastify';
import cronstrue from 'cronstrue';

import { Badge_Types, StatusBadge } from '../../common/badge/StatusBadge';
import { YBButton, YBToggle } from '../../common/forms/fields';
import { YBLoading } from '../../common/indicators';
import { YBConfirmModal } from '../../modals';
import {
  deleteBackupSchedule,
  editBackupSchedule,
  getScheduledBackupList
} from '../common/BackupScheduleAPI';
import { TABLE_TYPE_MAP } from '../common/IBackup';
import { IBackupSchedule } from '../common/IBackupSchedule';
import { BackupCreateModal } from '../components/BackupCreateModal';

import { convertScheduleToFormValues, getReadableTime } from './ScheduledBackupUtils';

import './ScheduledBackupList.scss';
import { useSelector } from 'react-redux';
import { Link } from 'react-router';
import { keyBy } from 'lodash';

export const ScheduledBackupList = ({ universeUUID }: { universeUUID: string }) => {
  const [page, setPage] = useState(0);
  const [showCreateModal, setShowCreateModal] = useState(false);
  const [editPolicyData, setEditPolicyData] = useState<Record<string, any> | undefined>(undefined);

  const storageConfigs = useSelector((reduxState: any) => reduxState.customer.configs);
  const storageConfigsMap = useMemo(() => keyBy(storageConfigs?.data ?? [], 'configUUID'), [
    storageConfigs
  ]);

  const {
    data: scheduledBackupList,
    isLoading,
    fetchNextPage,
    hasNextPage,
    isFetchingNextPage
  } = useInfiniteQuery(
    ['scheduled_backup_list'],
    ({ pageParam = 0 }) => getScheduledBackupList(pageParam),
    {
      getNextPageParam: (lastPage) => lastPage.data.hasNext
    }
  );

  if (isLoading) {
    return <YBLoading />;
  }

  const schedules = scheduledBackupList?.pages.flatMap((page) => {
    return page.data.entities;
  });

  const handleScroll = (e: any) => {
    const bottom = e.target.scrollHeight - e.target.scrollTop === e.target.clientHeight;
    if (bottom && hasNextPage) {
      fetchNextPage({ pageParam: page + 1 });
      setPage(page + 1);
    }
  };

  return (
    <div className="schedule-list-panel">
      <div className="schedule-action">
        <YBButton
          btnText="Create Scheduled Backup Policy"
          btnClass="btn btn-orange"
          onClick={() => setShowCreateModal(true)}
        />
      </div>
      <div className="schedule-backup-list" onScroll={handleScroll}>
        {schedules?.map((schedule) => (
          <ScheduledBackupCard
            schedule={schedule}
            key={schedule.scheduleUUID}
            doEditPolicy={() => {
              setEditPolicyData(convertScheduleToFormValues(schedule, storageConfigs?.data));
              setShowCreateModal(true);
            }}
            storageConfig={storageConfigsMap[schedule.taskParams.storageConfigUUID]}
          />
        ))}
        {isFetchingNextPage && <YBLoading />}
        <BackupCreateModal
          visible={showCreateModal}
          onHide={() => {
            setShowCreateModal(false);
            if (editPolicyData) {
              setEditPolicyData(undefined);
            }
          }}
          editValues={editPolicyData}
          currentUniverseUUID={universeUUID}
          isScheduledBackup
          isEditMode={editPolicyData !== undefined}
        />
      </div>
    </div>
  );
};

interface ScheduledBackupCardProps {
  schedule: IBackupSchedule;
  doEditPolicy: (schedule: IBackupSchedule) => void;
  storageConfig: Record<string, string> | undefined;
}

type toogleScheduleProps = Partial<IBackupSchedule> & Pick<IBackupSchedule, 'scheduleUUID'>;

const ScheduledBackupCard: FC<ScheduledBackupCardProps> = ({
  schedule,
  doEditPolicy,
  storageConfig
}) => {
  const queryClient = useQueryClient();
  const [showDeleteModal, setShowDeleteModal] = useState('');

  const toggleSchedule = useMutation((val: toogleScheduleProps) => editBackupSchedule(val), {
    onSuccess: (_, params) => {
      toast.success(`Schedule policy is now ${params.status}`);
      queryClient.invalidateQueries('scheduled_backup_list');
    },
    onError: (resp: any) => {
      toast.error(resp.response.data.error);
    }
  });

  const deleteSchedule = useMutation(
    () => deleteBackupSchedule(schedule.scheduleUUID),

    {
      onSuccess: () => {
        toast.success(`Schedule policy is now deleted`);
        queryClient.invalidateQueries('scheduled_backup_list');
      },
      onError: (resp: any) => {
        toast.error(resp.response.data.error);
      }
    }
  );

  let backupInterval = '';

  if (schedule.cronExpression) {
    backupInterval = cronstrue.toString(schedule.cronExpression);
  } else {
    backupInterval = getReadableTime(schedule.frequency, 'Every ');
  }
  return (
    <div className="schedule-item">
      <Row className="name-and-actions">
        <Col lg={6} md={6} className={`name-field ${schedule.status}`}>
          <span className="schedule-name">Some Name</span>
          <StatusBadge
            statusType={Badge_Types.DELETED}
            customLabel={TABLE_TYPE_MAP[schedule.taskParams.backupType]}
          />
          <YBToggle
            name="Enabled"
            input={{
              value: schedule.status === 'Active',
              onChange: (e: React.ChangeEvent<HTMLInputElement>) =>
                toggleSchedule.mutateAsync({
                  scheduleUUID: schedule.scheduleUUID,
                  frequency: schedule.frequency,
                  cronExpression: schedule.cronExpression,
                  status: e.target.checked ? 'Active' : 'Stopped'
                })
            }}
          />
          <span>Enabled</span>
        </Col>
        <Col lg={6} className="no-padding">
          <DropdownButton
            className="actions-btn"
            title="Actions"
            id="schedule-backup-actions-dropdown"
            pullRight
            onClick={(e) => e.stopPropagation()}
          >
            <MenuItem
              onClick={() => {
                doEditPolicy(schedule);
              }}
            >
              <i className="fa fa-pencil"></i> Edit Policy
            </MenuItem>
            <MenuItem
              onClick={() => {
                setShowDeleteModal(schedule.scheduleUUID);
              }}
              className="action-danger"
            >
              <i className="fa fa-trash"></i> Delete Policy
            </MenuItem>
          </DropdownButton>
        </Col>
      </Row>
      <div className="divider" />
      <Row className="info">
        <Col lg={6} md={6} className="schedule-info">
          <Row>
            <Col lg={3}>
              <div className="info-title">SCOPE</div>
              <div className="info-val">
                {schedule.taskType === 'MultiTableBackup' ? 'Full Backup' : 'Table Backup'}
              </div>
            </Col>
            <Col lg={3}>
              <div className="info-title">DATABASE NAME</div>
              <div className="info-val">{schedule.taskParams.keyspace ?? '-'}</div>
            </Col>
            <Col lg={3}>
              <div className="info-title">TABLES</div>
              <div className="info-val">{schedule.taskParams.keyspace ?? '-'}</div>
            </Col>
          </Row>
          <Row className="schedule-config-info">
            <Col lg={3}>
              <div className="info-title">BACKUP CONFIG</div>
              <div className="info-val">
                {storageConfig ? (
                  <Link
                    target="_blank"
                    className="universe-link"
                    to={`/config/backup/${storageConfig ? storageConfig.name.toLowerCase() : ''}`}
                  >
                    {storageConfig.configName}
                  </Link>
                ) : (
                  '-'
                )}
              </div>
            </Col>
            <Col lg={3}>
              <div className="info-title">INTERVALS</div>
              <div className="info-val">{backupInterval}</div>
            </Col>
            <Col lg={3}>
              <div className="info-title">RETENTION PERIOD</div>
              <div className="info-val">
                {schedule.taskParams?.timeBeforeDelete
                  ? getReadableTime(schedule.taskParams.timeBeforeDelete, '', 'second')
                  : 'Indefinately'}
              </div>
            </Col>
            <Col lg={3}>
              <div className="info-title">ENCRYPTION</div>
              <div className="info-val">
                {schedule.taskParams.encryptionAtRestConfig.encryptionAtRestEnabled
                  ? 'Enabled'
                  : '-'}
              </div>
            </Col>
          </Row>
        </Col>
        <Col lg={6} md={6}>
          <Row className="backup-timeline-info">
            <Col lg={3}>
              <div className="info-title">Last backup</div>
              <div className="info-val">-</div>
            </Col>
            <Col lg={3}>
              <div className="info-title">Next backup</div>
              <div className="info-val">-</div>
            </Col>
          </Row>
        </Col>
      </Row>
      {
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
          Are you sure you want to delete this schedule policy?
        </YBConfirmModal>
      }
    </div>
  );
};
