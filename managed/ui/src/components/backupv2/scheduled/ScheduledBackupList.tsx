/*
 * Created on Thu Mar 31 2022
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import React, { FC, useMemo, useState } from 'react';
import {
  Col,
  DropdownButton,
  MenuItem,
  OverlayTrigger,
  Popover,
  Row,
  Tooltip
} from 'react-bootstrap';
import { useInfiniteQuery, useMutation, useQuery, useQueryClient } from 'react-query';
import { toast } from 'react-toastify';
import cronstrue from 'cronstrue';
import { useSelector } from 'react-redux';
import { Link } from 'react-router';
import { find, keyBy } from 'lodash';
import { Badge_Types, StatusBadge } from '../../common/badge/StatusBadge';
import { YBButton, YBToggle } from '../../common/forms/fields';
import { YBLoading } from '../../common/indicators';
import { YBConfirmModal } from '../../modals';
import { ScheduledBackupEmpty } from '../components/BackupEmpty';
import { fetchTablesInUniverse } from '../../../actions/xClusterReplication';
import { ybFormatDate } from '../../../redesign/helpers/DateUtils';
import { ITable } from '../common/IBackup';
import {
  RbacValidator,
  hasNecessaryPerm
} from '../../../redesign/features/rbac/common/RbacApiPermValidator';
import {
  deleteBackupSchedule,
  editBackupSchedule,
  getScheduledBackupList
} from '../common/BackupScheduleAPI';
import { AllowedTasks, TableType, TableTypeLabel } from '../../../redesign/helpers/dtos';
import { convertScheduleToFormValues, convertMsecToTimeFrame } from './ScheduledBackupUtils';
import { IBackupSchedule, IBackupScheduleStatus } from '../common/IBackupSchedule';
import { ApiPermissionMap } from '../../../redesign/features/rbac/ApiAndUserPermMapping';
import { BackupCreateModal } from '../components/BackupCreateModal';
import './ScheduledBackupList.scss';

import WarningIcon from '../../users/icons/warning_icon';

const wrapTableName = (tablesList: string[] | undefined) => {
  if (!Array.isArray(tablesList) || tablesList.length === 0) {
    return '-';
  }
  const tables = tablesList?.slice(0);
  const firstTable = tables.splice(0, 1);
  return (
    <div>
      {firstTable}{' '}
      {tables.length > 0 && (
        <OverlayTrigger
          trigger="click"
          rootClose
          placement="top"
          overlay={
            <Popover id="more-tables-popover" title="Tables">
              <span>{tables.join('')}</span>
            </Popover>
          }
        >
          <span className="tables-more">+{tables.length} more</span>
        </OverlayTrigger>
      )}
    </div>
  );
};

export const ScheduledBackupList = ({
  universeUUID,
  allowedTasks
}: {
  universeUUID: string;
  allowedTasks: AllowedTasks;
}) => {
  const [page, setPage] = useState(0);
  const [showCreateModal, setShowCreateModal] = useState(false);
  const [editPolicyData, setEditPolicyData] = useState<Record<string, any> | undefined>(undefined);

  const storageConfigs = useSelector((reduxState: any) => reduxState.customer.configs);
  const currentUniverse = useSelector((reduxState: any) => reduxState.universe.currentUniverse);

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
    ({ pageParam = 0 }) => getScheduledBackupList(pageParam, universeUUID),
    {
      getNextPageParam: (lastPage) => lastPage.data.hasNext
    }
  );

  const { data: tablesInUniverse, isLoading: isTableListLoading } = useQuery(
    [universeUUID, 'tables'],
    () => fetchTablesInUniverse(universeUUID!)
  );

  if (isLoading) {
    return <YBLoading />;
  }
  const schedules = scheduledBackupList?.pages
    .flatMap((page) => {
      return page.data.entities;
    })
    .filter(
      (schedule) =>
        schedule.backupInfo !== undefined && schedule.backupInfo.universeUUID === universeUUID
    );

  const handleScroll = (e: any) => {
    const bottom = e.target.scrollHeight - e.target.scrollTop <= e.target.clientHeight;
    if (bottom && hasNextPage) {
      fetchNextPage({ pageParam: page + 1 });
      setPage(page + 1);
    }
  };

  const canCreateBackup = hasNecessaryPerm({
    onResource: universeUUID,
    ...ApiPermissionMap.CREATE_BACKUP_SCHEDULE
  });

  if (schedules?.length === 0) {
    return (
      <>
        <ScheduledBackupEmpty
          onActionButtonClick={() => {
            setShowCreateModal(true);
          }}
          disabled={
            tablesInUniverse?.data.length === 0 ||
            currentUniverse.data?.universeConfig?.takeBackups === 'false' ||
            currentUniverse?.data?.universeDetails?.universePaused ||
            !canCreateBackup
          }
          hasPerm={canCreateBackup}
        />
        <BackupCreateModal
          visible={showCreateModal}
          allowedTasks={allowedTasks}
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
      </>
    );
  }

  return (
    <div className="schedule-list-panel">
      <div className="schedule-action">
        <RbacValidator
          accessRequiredOn={{
            onResource: universeUUID,
            ...ApiPermissionMap.CREATE_BACKUP_SCHEDULE
          }}
          isControl
        >
          <YBButton
            btnText="Create Scheduled Backup Policy"
            btnClass="btn btn-orange"
            onClick={() => setShowCreateModal(true)}
            loading={isTableListLoading}
            disabled={tablesInUniverse?.data.length === 0}
          />
        </RbacValidator>
      </div>
      <div className="schedule-backup-list" onScroll={handleScroll}>
        {/* eslint-disable-next-line react/display-name */}
        {schedules?.map((schedule) => (
          <ScheduledBackupCard
            schedule={schedule}
            key={schedule.scheduleUUID}
            doEditPolicy={() => {
              setEditPolicyData(convertScheduleToFormValues(schedule, storageConfigs?.data));
              setShowCreateModal(true);
            }}
            storageConfig={storageConfigsMap[schedule.backupInfo.storageConfigUUID]}
            tablesInUniverse={tablesInUniverse?.data ?? []}
            universeUUID={universeUUID}
          />
        ))}
        {isFetchingNextPage && <YBLoading />}
        <BackupCreateModal
          visible={showCreateModal}
          allowedTasks={allowedTasks}
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
  tablesInUniverse: ITable[];
  universeUUID: string;
}

type toogleScheduleProps = Partial<IBackupSchedule> & Pick<IBackupSchedule, 'scheduleUUID'>;

const ScheduledBackupCard: FC<ScheduledBackupCardProps> = ({
  schedule,
  doEditPolicy,
  storageConfig,
  tablesInUniverse,
  universeUUID
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
    backupInterval = convertMsecToTimeFrame(
      schedule.frequency,
      schedule.frequencyTimeUnit,
      'Every '
    );
  }

  let isTableMissingToDoBackup = false;

  if (
    schedule.backupInfo.backupType === TableType.YQL_TABLE_TYPE &&
    !schedule.backupInfo.fullBackup
  ) {
    schedule.backupInfo.keyspaceList.forEach((keyspace) => {
      isTableMissingToDoBackup = !keyspace.tableUUIDList?.every((tableUUID) =>
        find(tablesInUniverse, { tableUUID, keySpace: keyspace.keyspace })
      );
    });
  }

  return (
    <div className="schedule-item">
      <Row className="name-and-actions">
        <Col lg={6} md={6} className={`name-field ${schedule.status}`}>
          <span className="schedule-name">{schedule.scheduleName}</span>
          <StatusBadge
            statusType={Badge_Types.DELETED}
            customLabel={TableTypeLabel[schedule.backupInfo.backupType ?? '-']}
          />
          <RbacValidator
            accessRequiredOn={{
              ...ApiPermissionMap.MODIFY_SCHEDULE,
              onResource: universeUUID
            }}
            isControl
            overrideStyle={{
              display: 'unset',
              pointerEvents: 'none'
            }}
          >
            <YBToggle
              name="Enabled"
              input={{
                value: schedule.status === IBackupScheduleStatus.ACTIVE,
                onChange: (e: React.ChangeEvent<HTMLInputElement>) =>
                  toggleSchedule.mutateAsync({
                    scheduleUUID: schedule.scheduleUUID,
                    frequency: schedule.frequency,
                    cronExpression: schedule.cronExpression,
                    status: e.target.checked
                      ? IBackupScheduleStatus.ACTIVE
                      : IBackupScheduleStatus.STOPPED,
                    frequencyTimeUnit: schedule.frequencyTimeUnit
                  })
              }}
            />
          </RbacValidator>
          <span>{schedule.status === IBackupScheduleStatus.ACTIVE ? 'Enabled' : 'Disabled'}</span>
          {isTableMissingToDoBackup && (
            <OverlayTrigger
              trigger={['hover', 'focus']}
              placement="top"
              overlay={
                <Popover id="more-tables-popover">
                  One or more of selected tables to backup does not exists in the keyspace.
                </Popover>
              }
            >
              <span>
                {' '}
                <WarningIcon />
              </span>
            </OverlayTrigger>
          )}
        </Col>
        <Col lg={6} className="no-padding">
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
                  if (schedule.status !== IBackupScheduleStatus.ACTIVE) return;
                  doEditPolicy(schedule);
                }}
                disabled={schedule.status !== IBackupScheduleStatus.ACTIVE}
              >
                <i className="fa fa-pencil"></i> Edit Policy
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
                <i className="fa fa-trash"></i> Delete Policy
              </MenuItem>
            </RbacValidator>
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
                {schedule.backupInfo?.fullBackup ? 'Full Backup' : 'Table Backup'}
              </div>
            </Col>
            <Col lg={3}>
              <div className="info-title">DATABASE NAME</div>
              <div className="info-val">
                {wrapTableName(schedule.backupInfo?.keyspaceList?.map((k) => k.keyspace))}
              </div>
            </Col>
            <Col lg={3}>
              <div className="info-title">TABLES</div>
              <div className="info-val">
                {wrapTableName(schedule.backupInfo?.keyspaceList?.[0]?.tablesList)}
              </div>
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
                {schedule.backupInfo?.timeBeforeDelete
                  ? convertMsecToTimeFrame(
                      schedule.backupInfo.timeBeforeDelete,
                      schedule.backupInfo.expiryTimeUnit ?? 'DAYS'
                    )
                  : 'Indefinitely'}
              </div>
            </Col>
            {/* <Col lg={3}>
              <div className="info-title">ENCRYPTION</div>
              <div className="info-val">
                {schedule.backupInfo.sse
                  ? 'Enabled'
                  : '-'}
              </div>
            </Col> */}
          </Row>
        </Col>
        <Col lg={6} md={6}>
          <Row className="backup-timeline-info">
            <Col lg={3}>
              <div className="info-title">Last backup</div>
              <div className="info-val">
                {schedule.prevCompletedTask ? ybFormatDate(schedule.prevCompletedTask) : '-'}
              </div>
            </Col>
            <Col lg={3}>
              <div className="info-title">Next backup</div>
              <div className="info-val">
                {schedule.nextExpectedTask ? ybFormatDate(schedule.nextExpectedTask) : '-'}
              </div>
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
