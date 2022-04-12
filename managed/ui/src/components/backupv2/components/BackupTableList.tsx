/*
 * Created on Thu Feb 17 2022
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import React, { FC } from 'react';
import { BootstrapTable, TableHeaderColumn } from 'react-bootstrap-table';
import { Backup_States, IBackup } from '..';
import { YBButton } from '../../common/forms/fields';
import './BackupTableList.scss';
import copy from 'copy-to-clipboard';
import { toast } from 'react-toastify';

interface YSQLTableProps {
  keyspaceSearch?: string;
  onRestore: Function;
  backup: IBackup;
  hideRestore?: boolean;
}

const COLLAPSED_ICON = <i className="fa fa-caret-right" />;
const EXPANDED_ICON = <i className="fa fa-caret-down" />;

export const YSQLTableList: FC<YSQLTableProps> = ({
  backup,
  keyspaceSearch,
  onRestore,
  hideRestore = false
}) => {
  const databaseList = backup.responseList
    .filter((e) => {
      return !(keyspaceSearch && e.keyspace.indexOf(keyspaceSearch) < 0);
    })
    .map((table, index) => {
      return {
        keyspace: table.keyspace,
        storageLocation: table.storageLocation,
        index
      };
    });
  return (
    <div className="backup-table-list">
      <BootstrapTable data={databaseList}>
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
                    backup.state !== Backup_States.COMPLETED || !backup.isStorageConfigPresent
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
                  copy(row.storageLocation);
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
          data={row.tablesList.map((t: string, i: number) => {
            return { t, i };
          })}
        >
          <TableHeaderColumn dataField="i" isKey={true} hidden={true} />
          <TableHeaderColumn dataField="t">Tables</TableHeaderColumn>
        </BootstrapTable>
      </div>
    );
  };
  const dblist = backup.responseList.filter((e) => {
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
          expandColumnComponent: ({ isExpanded }) => (isExpanded ? EXPANDED_ICON : COLLAPSED_ICON)
        }}
        trClassName="clickable"
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
                    backup.state !== Backup_States.COMPLETED || !backup.isStorageConfigPresent
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
                  copy(row.storageLocation);
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
