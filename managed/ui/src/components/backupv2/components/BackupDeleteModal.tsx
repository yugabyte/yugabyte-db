/*
 * Created on Tue Mar 01 2022
 *
 * Copyright 2021 YugabyteDB, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
import { FC, useMemo } from 'react';
import { Col, Row } from 'react-bootstrap';
import { BootstrapTable, TableHeaderColumn } from 'react-bootstrap-table';
import { AlertVariant, YBAlert } from '@app/redesign/components';
import { useMutation, useQueryClient } from 'react-query';
import { useSelector } from 'react-redux';
import { toast } from 'react-toastify';
import { cancelBackup, deleteBackup } from '../../backupv2/common/BackupAPI';
import { IBackup, Backup_States } from '..';
import {
  BACKUP_WITH_IMMUTABLE_STORAGE_MSG,
  hasImmutableStorageAmongBackups
} from '../common/BackupUtils';
import { groupStorageConfigs } from '../../../redesign/features/backup/scheduled/ScheduledBackupUtils';
import { ybFormatDate } from '../../../redesign/helpers/DateUtils';
import { StatusBadge } from '../../common/badge/StatusBadge';
import { YBModalForm } from '../../common/forms';
import { YBButton } from '../../common/forms/fields';
import { useInterceptBackupTaskLinks } from '../../../redesign/features/tasks/TaskUtils';
import { handleCACertErrMsg } from '../../customCACerts';

interface BackupDeleteProps {
  backupsList: IBackup[];
  visible: boolean;
  onHide: () => void;
}

const isIncrementalBackupInProgress = (backupList: IBackup[]) => {
  return backupList.some(
    (b) => b.hasIncrementalBackups && b.lastBackupState === Backup_States.IN_PROGRESS
  );
};

export const BackupDeleteModal: FC<BackupDeleteProps> = ({ backupsList, visible, onHide }) => {
  const queryClient = useQueryClient();
  const interceptBackupLink = useInterceptBackupTaskLinks();
  const storageConfigs = useSelector((state: any) => state.customer.configs);

  const groupedStorageConfigs = useMemo(
    () => groupStorageConfigs(storageConfigs?.data ?? []),
    [storageConfigs]
  );

  const showImmutableStorageWarning = hasImmutableStorageAmongBackups(
    groupedStorageConfigs,
    backupsList
  );

  const delBackup = useMutation((backupList: IBackup[]) => deleteBackup(backupList), {
    onSuccess: (resp: any) => {
      toast.success(
        <span>
          Backup is queued for deletion. Click &nbsp;
          {interceptBackupLink(<a href={`/tasks/${resp.data.taskUUID}`} target="_blank" rel="noopener noreferrer">
            here
          </a>)}
          &nbsp; for task details
        </span>
      );
      onHide();
      queryClient.invalidateQueries('backups');
    },
    onError: (resp: any) => {
      !handleCACertErrMsg(resp) && toast.error('Unable to delete backup');
      onHide();
    }
  });
  if (!visible) return null;
  return (
    <YBModalForm
      visible={visible}
      title="Delete Backup"
      className="backup-modal"
      showCancelButton={true}
      onHide={onHide}
      onFormSubmit={async (_values: any, { setSubmitting }: { setSubmitting: Function }) => {
        setSubmitting(false);
        if (isIncrementalBackupInProgress(backupsList)) {
          toast.error('Unable to delete backup while incremental backup is in progress');
          return;
        }
        await delBackup.mutateAsync(backupsList);
        onHide();
      }}
      submitLabel={
        <>
          <i className="fa fa-trash-o" />
          Delete Permanently
        </>
      }
    >
      <div>
        <span className="alert-message danger">
          <i className="fa fa-warning" /> You are about to permanently delete the following
          backups.This action can not be undone
        </span>
      </div>
      {showImmutableStorageWarning && <YBAlert open className='mt-8' variant={AlertVariant.Warning} text={BACKUP_WITH_IMMUTABLE_STORAGE_MSG()} />}
      <div className="delete-table-list">
        <BootstrapTable data={backupsList}>
          <TableHeaderColumn dataField="backupUUID" isKey={true} hidden={true} />
          <TableHeaderColumn dataField="universeName" dataFormat={(name) => name}>
            Source Universe Name
          </TableHeaderColumn>
          <TableHeaderColumn
            dataField="createTime"
            dataFormat={(_, row: IBackup) => ybFormatDate(row.commonBackupInfo.createTime)}
          >
            Created At
          </TableHeaderColumn>
          <TableHeaderColumn
            dataField="lastBackupState"
            dataFormat={(lastBackupState) => {
              return <StatusBadge statusType={lastBackupState} />;
            }}
          >
            Last Status
          </TableHeaderColumn>
        </BootstrapTable>
      </div>
    </YBModalForm>
  );
};
interface CancelModalProps {
  visible: boolean;
  backup: IBackup | null;
  onHide: () => void;
}
export const BackupCancelModal: FC<CancelModalProps> = ({ visible, backup, onHide }) => {
  const queryClient = useQueryClient();
  const execCancelBackup = useMutation(() => cancelBackup(backup as any), {
    onSuccess: () => {
      toast.success('Backup is being cancelled');
      onHide();
      queryClient.invalidateQueries(['backups']);
    },
    onError: (resp: any) => {
      onHide();
      toast.error(resp.response.data.error);
    }
  });
  if (!backup) return null;
  return (
    <YBModalForm
      visible={visible}
      title="Cancel Backup"
      onHide={onHide}
      className="backup-modal"
      onFormSubmit={async (_values: any, { setSubmitting }: { setSubmitting: Function }) => {
        await execCancelBackup.mutateAsync();
        setSubmitting(false);
        onHide();
      }}
      submitLabel="Cancel Backup"
      footerAccessory={
        <YBButton
          btnClass={`btn btn-default restore-wth-rename-but`}
          btnText="Cancel"
          onClick={() => onHide()}
        />
      }
    >
      <Row>
        <Col lg={12}>
          <span className="alert-message danger">
            <i className="fa fa-warning" /> You are about to cancel the backup from the source
            universe &nbsp;
            {backup.isUniversePresent ? backup.universeName : backup.universeUUID}
          </span>
        </Col>
      </Row>
    </YBModalForm>
  );
};
