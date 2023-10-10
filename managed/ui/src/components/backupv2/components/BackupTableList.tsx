/*
 * Created on Thu Feb 17 2022
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import React, { FC, useState } from 'react';
import { BootstrapTable, TableHeaderColumn } from 'react-bootstrap-table';
import { Backup_States, IBackup, ICommonBackupInfo, Keyspace_Table } from '..';
import { fetchIncrementalBackup, deleteIncrementalBackup } from '../../backupv2/common/BackupAPI';
import { YBButton, YBModal } from '../../common/forms/fields';
import copy from 'copy-to-clipboard';
import { toast } from 'react-toastify';
import { useMutation, useQuery, useQueryClient } from 'react-query';
import { YBLoadingCircleIcon } from '../../common/indicators';
import { BACKUP_REFETCH_INTERVAL, calculateDuration } from '../common/BackupUtils';
import { formatBytes } from '../../xcluster/ReplicationUtils';
import { StatusBadge } from '../../common/badge/StatusBadge';
import { TableType } from '../../../redesign/helpers/dtos';
import Timer from '../../universes/images/timer.svg';
import { createErrorMessage } from '../../../utils/ObjectUtils';
import { ybFormatDate } from '../../../redesign/helpers/DateUtils';
import { IncrementalBackupProps } from './BackupDetails';
import './BackupTableList.scss';

export enum BackupTypes {
  FULL_BACKUP = 'FULL BACKUP',
  INCREMENT_BACKUP = 'INCREMENT'
}
export interface YSQLTableProps {
  keyspaceSearch?: string;
  onRestore: (tablesList: Keyspace_Table[], incrementalBackupProps: IncrementalBackupProps) => void;
  backup: IBackup;
  backupType?: BackupTypes;
  hideRestore?: boolean;
  incrementalBackup?: ICommonBackupInfo;
}

const COLLAPSED_ICON = <i className="fa fa-caret-right expand-keyspace-icon" />;
const EXPANDED_ICON = <i className="fa fa-caret-down expand-keyspace-icon" />;

export const YSQLTableList: FC<YSQLTableProps> = ({
  backup,
  backupType,
  keyspaceSearch,
  onRestore,
  hideRestore = false,
  incrementalBackup
}) => {
  const dbList =
    backupType === BackupTypes.INCREMENT_BACKUP
      ? incrementalBackup?.responseList
      : backup.commonBackupInfo.responseList;
  const filteredDBList = (dbList ?? [])
    .filter((e) => {
      return !(keyspaceSearch && !e.keyspace.includes(keyspaceSearch));
    })
    .map((table, index) => {
      return {
        keyspace: table.keyspace,
        storageLocation: table.storageLocation,
        defaultLocation: table.defaultLocation,
        index
      };
    });

  return (
    <div className="backup-table-list">
      <BootstrapTable data={filteredDBList} tableHeaderClass="table-list-header">
        <TableHeaderColumn dataField="index" isKey={true} hidden={true} />
        <TableHeaderColumn dataField="keyspace">Database Name</TableHeaderColumn>
        <TableHeaderColumn
          dataField="actions"
          dataAlign="right"
          dataFormat={(_, row) => (
            <>
              {!hideRestore && (
                <YBButton
                  btnText="Restore"
                  className="restore-detail-button"
                  disabled={
                    backup.commonBackupInfo.state !== Backup_States.COMPLETED ||
                    !backup.isStorageConfigPresent
                  }
                  onClick={(e: React.MouseEvent<HTMLButtonElement>) => {
                    e.stopPropagation();
                    onRestore([row], {
                      isRestoreEntireBackup: false,
                      singleKeyspaceRestore: true,
                      incrementalBackupUUID:
                        backupType === BackupTypes.INCREMENT_BACKUP
                          ? incrementalBackup?.backupUUID
                          : backup.commonBackupInfo.backupUUID
                    });
                  }}
                />
              )}
              <YBButton
                className="copy-location-button"
                btnText="Copy Location"
                onClick={(e: React.MouseEvent<HTMLButtonElement>) => {
                  e.stopPropagation();
                  copy(row.storageLocation ?? row.defaultLocation);
                  toast.success(
                    <>
                      <i className="fa fa-check" /> Copied
                    </>,
                    {
                      style: {
                        width: '250px'
                      }
                    }
                  );
                }}
              />
            </>
          )}
        />
      </BootstrapTable>
    </div>
  );
};

export const YCQLTableList: FC<YSQLTableProps> = ({
  backup,
  backupType,
  keyspaceSearch,
  onRestore,
  hideRestore,
  incrementalBackup
}) => {
  const expandTables = (row: any) => {
    return (
      <div className="inset-table">
        <BootstrapTable
          tableHeaderClass="table-list-header"
          data={row.tablesList.map((t: string, i: number) => {
            return { t, i };
          })}
          headerStyle={{
            textTransform: 'capitalize'
          }}
        >
          <TableHeaderColumn dataField="i" isKey={true} hidden={true} />
          <TableHeaderColumn dataField="t">Tables</TableHeaderColumn>
        </BootstrapTable>
      </div>
    );
  };
  const dbList =
    backupType === BackupTypes.INCREMENT_BACKUP
      ? incrementalBackup?.responseList
      : backup.commonBackupInfo.responseList;
  const filteredDBList = (dbList ?? [])
    .filter((e) => {
      return !(keyspaceSearch && !e.keyspace.includes(keyspaceSearch));
    })
    .map((t, index) => {
      return { ...t, index };
    });
  return (
    <div className="backup-table-list ycql-table" id="ycql-table">
      <BootstrapTable
        data={filteredDBList}
        expandableRow={() => true}
        expandComponent={expandTables}
        expandColumnOptions={{
          expandColumnVisible: true,
          expandColumnComponent: ({ isExpanded }) => (isExpanded ? EXPANDED_ICON : COLLAPSED_ICON),
          expandColumnBeforeSelectColumn: true
        }}
        trClassName="clickable"
        tableHeaderClass="table-list-header"
      >
        <TableHeaderColumn dataField="index" isKey={true} hidden={true} />
        <TableHeaderColumn dataField="keyspace">Keyspace</TableHeaderColumn>
        <TableHeaderColumn dataField="tablesList" dataFormat={(cell) => cell.length}>
          Tables
        </TableHeaderColumn>
        <TableHeaderColumn
          dataField="actions"
          dataAlign="right"
          dataFormat={(_, row) => (
            <>
              {!hideRestore && (
                <YBButton
                  btnText="Restore"
                  disabled={
                    backup.commonBackupInfo.state !== Backup_States.COMPLETED ||
                    !backup.isStorageConfigPresent
                  }
                  className="restore-detail-button"
                  onClick={(e: React.MouseEvent<HTMLButtonElement>) => {
                    e.stopPropagation();
                    onRestore([row], {
                      isRestoreEntireBackup: false,
                      singleKeyspaceRestore: true,
                      incrementalBackupUUID:
                        backupType === BackupTypes.INCREMENT_BACKUP
                          ? incrementalBackup?.backupUUID
                          : backup.commonBackupInfo.backupUUID
                    });
                  }}
                />
              )}

              <YBButton
                className="copy-location-button"
                btnText="Copy Location"
                onClick={(e: React.MouseEvent<HTMLButtonElement>) => {
                  e.stopPropagation();
                  copy(row.storageLocation ?? row.defaultLocation);
                  toast.success(
                    <>
                      <i className="fa fa-check" /> Copied
                    </>,
                    {
                      style: {
                        width: '250px'
                      }
                    }
                  );
                }}
              />
            </>
          )}
        />
      </BootstrapTable>
    </div>
  );
};

export const IncrementalTableBackupList: FC<YSQLTableProps> = ({
  backup,
  keyspaceSearch,
  ...rest
}) => {
  const { data: incrementalBackups, isLoading, isError } = useQuery(
    ['incremental_backups', backup.commonBackupInfo.baseBackupUUID],
    () => fetchIncrementalBackup(backup.commonBackupInfo.baseBackupUUID),
    {
      refetchInterval: BACKUP_REFETCH_INTERVAL
    }
  );

  if (isLoading) {
    return <YBLoadingCircleIcon />;
  }

  if (isError) {
    return <div>Unable to fetch details</div>;
  }

  if (!incrementalBackups?.data) {
    return null;
  }

  return (
    <div className="incremental-backup-list">
      {incrementalBackups.data
        .filter((e) => {
          return !(
            keyspaceSearch && e.responseList.some((t) => !t.keyspace.includes(keyspaceSearch))
          );
        })
        .map((b) => (
          <IncrementalBackupCard
            key={b.backupUUID}
            incrementalBackup={b}
            backup={backup}
            {...rest}
          />
        ))}
    </div>
  );
};

const IncrementalBackupCard = ({
  incrementalBackup,
  backup,
  ...rest
}: { backup: IBackup; incrementalBackup: ICommonBackupInfo } & YSQLTableProps) => {
  const backup_type =
    incrementalBackup.backupUUID === incrementalBackup.baseBackupUUID
      ? BackupTypes.FULL_BACKUP
      : BackupTypes.INCREMENT_BACKUP;
  const [isExpanded, setIsExpanded] = useState(false);
  const [showDeleteConfirmDialog, setShowDeleteConfirmDialog] = useState(false);

  const queryClient = useQueryClient();

  let listComponent: any = null;
  if (isExpanded) {
    if (
      backup.backupType === TableType.YQL_TABLE_TYPE ||
      backup.backupType === TableType.REDIS_TABLE_TYPE
    ) {
      listComponent = (
        <YCQLTableList
          backup={backup}
          incrementalBackup={incrementalBackup}
          backupType={backup_type}
          {...rest}
        />
      );
    } else {
      listComponent = (
        <YSQLTableList
          backup={backup}
          incrementalBackup={incrementalBackup}
          backupType={backup_type}
          {...rest}
        />
      );
    }
  }

  const doDeleteBackup = useMutation(() => deleteIncrementalBackup(incrementalBackup), {
    onSuccess: () => {
      toast.success('Incremental backup deletion is in progress');
      queryClient.invalidateQueries([
        'incremental_backups',
        backup.commonBackupInfo.baseBackupUUID
      ]);
    },
    onError: (err: any) => {
      toast.error(createErrorMessage(err));
    }
  });

  return (
    <div className="incremental-card" key={incrementalBackup.backupUUID}>
      <div
        className="backup-info"
        onClick={(e) => {
          e.stopPropagation();
          setIsExpanded(!isExpanded);
        }}
      >
        {isExpanded ? EXPANDED_ICON : COLLAPSED_ICON}
        {ybFormatDate(incrementalBackup.createTime)}
        <span className="backup-type">{backup_type}</span>
        <span className="backup-pill">{formatBytes(incrementalBackup.totalBackupSizeInBytes)}</span>
        <span className="backup-pill backup-duration">
          <img alt="--" src={Timer} width="16" />
          <span>
            {calculateDuration(incrementalBackup.createTime, incrementalBackup.completionTime)}
          </span>
        </span>
      </div>
      <div className="incremental-backup-actions">
        <StatusBadge statusType={incrementalBackup.state as any} />
        {[Backup_States.FAILED, Backup_States.FAILED_TO_DELETE, Backup_States.STOPPED].includes(
          incrementalBackup.state
        ) && (
          <>
            <YBButton
              btnIcon="fa fa-trash-o"
              btnText="Delete"
              className="incremental-backup-action-button incremental-backup-delete-button"
              onClick={(e: React.MouseEvent<HTMLButtonElement>) => {
                e.stopPropagation();
                setShowDeleteConfirmDialog(true);
              }}
            />
            <YBModal
              name="delete-incremental-backup"
              title="Confirm Delete"
              className="backup-modal"
              showCancelButton
              onFormSubmit={() => doDeleteBackup.mutate()}
              onHide={() => setShowDeleteConfirmDialog(false)}
              visible={showDeleteConfirmDialog}
            >
              Are you sure you want to delete this incremental backup?
            </YBModal>
          </>
        )}
        {!rest.hideRestore && incrementalBackup.state === Backup_States.COMPLETED && (
          <YBButton
            btnText="Restore to this point"
            className="incremental-backup-action-button incremental-backup-restore-button"
            onClick={(e: React.MouseEvent<HTMLButtonElement>) => {
              const { onRestore } = rest;
              e.stopPropagation();
              const incrementalBackupProps: IncrementalBackupProps = {
                isRestoreEntireBackup: false,
                incrementalBackupUUID: incrementalBackup.backupUUID,
                singleKeyspaceRestore: false
              };
              if (incrementalBackup.kmsConfigUUID)
                incrementalBackupProps.kmsConfigUUID = incrementalBackup.kmsConfigUUID;
              onRestore(incrementalBackup.responseList, incrementalBackup);
            }}
          />
        )}
      </div>

      {isExpanded && (
        <>
          <div className="break" />
          {listComponent}
        </>
      )}
    </div>
  );
};
