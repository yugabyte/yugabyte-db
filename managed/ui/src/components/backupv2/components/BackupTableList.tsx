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
import { Backup_States, fetchIncrementalBackup, IBackup, ICommonBackupInfo } from '..';
import { YBButton } from '../../common/forms/fields';
import copy from 'copy-to-clipboard';
import { toast } from 'react-toastify';
import { useQuery } from 'react-query';
import { YBLoadingCircleIcon } from '../../common/indicators';
import { calculateDuration, FormatUnixTimeStampTimeToTimezone } from '../common/BackupUtils';
import { formatBytes } from '../../xcluster/ReplicationUtils';
import { StatusBadge } from '../../common/badge/StatusBadge';
import { TableType } from '../../../redesign/helpers/dtos';
import Timer from '../../universes/images/timer.svg';
import './BackupTableList.scss';
export interface YSQLTableProps {
  keyspaceSearch?: string;
  onRestore: Function;
  backup: IBackup;
  hideRestore?: boolean;
}

const COLLAPSED_ICON = <i className="fa fa-caret-right expand-keyspace-icon" />;
const EXPANDED_ICON = <i className="fa fa-caret-down expand-keyspace-icon" />;

export const YSQLTableList: FC<YSQLTableProps> = ({
  backup,
  keyspaceSearch,
  onRestore,
  hideRestore = false
}) => {
  const databaseList = backup.commonBackupInfo.responseList
    .filter((e) => {
      return !(keyspaceSearch && e.keyspace.indexOf(keyspaceSearch) < 0);
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
      <BootstrapTable data={databaseList} tableHeaderClass="table-list-header">
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
                    onRestore([row]);
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
  keyspaceSearch,
  onRestore,
  hideRestore
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
  const dblist = backup.commonBackupInfo.responseList.filter((e) => {
    return !(keyspaceSearch && e.keyspace.indexOf(keyspaceSearch) < 0);
  });
  return (
    <div className="backup-table-list ycql-table" id="ycql-table">
      <BootstrapTable
        data={dblist}
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
        <TableHeaderColumn dataField="keyspace" isKey={true} hidden={true} />
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
                    onRestore([row]);
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
    () => fetchIncrementalBackup(backup.commonBackupInfo.baseBackupUUID)
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
            keyspaceSearch && e.responseList.some((t) => t.keyspace.indexOf(keyspaceSearch) === -1)
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
    incrementalBackup.backupUUID === incrementalBackup.baseBackupUUID ? 'FULL BACKUP' : 'INCREMENT';
  const [isExpanded, setIsExpanded] = useState(false);

  let listComponent = null;
  if (isExpanded) {
    if (
      backup.backupType === TableType.YQL_TABLE_TYPE ||
      backup.backupType === TableType.REDIS_TABLE_TYPE
    ) {
      listComponent = <YCQLTableList backup={backup} {...rest} />;
    } else {
      listComponent = <YSQLTableList backup={backup} {...rest} />;
    }
  }

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
        <FormatUnixTimeStampTimeToTimezone timestamp={incrementalBackup.createTime} />
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
        {!rest.hideRestore && (
          <YBButton
            btnText="Restore to this point"
            disabled={incrementalBackup.state !== Backup_States.COMPLETED}
            className="incremental-backup-restore-button"
            onClick={(e: React.MouseEvent<HTMLButtonElement>) => {
              const { onRestore } = rest;
              e.stopPropagation();
              onRestore(incrementalBackup.responseList);
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
